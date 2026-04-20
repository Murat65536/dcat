#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <vips/vips.h>

#include "core/args.h"
#include "core/platform_compat.h"
#include "core/threading.h"
#include "graphics/camera.h"
#include "graphics/model.h"
#include "graphics/texture_loader.h"
#include "input/input_handler.h"
#include "renderer/vulkan_renderer.h"
#include "terminal/block_characters.h"
#include "terminal/kitty.h"
#include "terminal/kitty_shm.h"
#include "terminal/palette_characters.h"
#include "terminal/sixel.h"
#include "terminal/terminal.h"
#include "terminal/truecolor_characters.h"

// Global state for signal handlers
static atomic_bool g_running = true;
static volatile sig_atomic_t g_resize_pending = 1;
static volatile sig_atomic_t g_terminal_session_active = 0;

static const float TARGET_SIZE = 4.0f;
static const float MAX_SIMULATION_DELTA_SECONDS = 0.1f;

static void set_atomic_flag(atomic_bool *flag, const bool value) {
    *flag = value;
}

static bool get_atomic_flag(const atomic_bool *flag) {
    return *flag;
}

typedef struct FatalReport {
    bool active;
    char message[512];
} FatalReport;

static void signal_handler(const int sig) {
    (void)sig;
    set_atomic_flag(&g_running, false);
}

#ifdef SIGWINCH
static void resize_handler(int sig) {
    (void)sig;
    g_resize_pending = 1;
}
#endif

static void write_signal_literal(int fd, const char *data, size_t size) {
    while (size > 0) {
        ssize_t written = write(fd, data, size);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        data += written;
        size -= (size_t)written;
    }
}

static void write_signal_name(int sig) {
    switch (sig) {
    case SIGABRT:
        write_signal_literal(STDERR_FILENO, "SIGABRT", 7);
        break;
#ifdef SIGBUS
    case SIGBUS:
        write_signal_literal(STDERR_FILENO, "SIGBUS", 6);
        break;
#endif
    case SIGFPE:
        write_signal_literal(STDERR_FILENO, "SIGFPE", 6);
        break;
    case SIGILL:
        write_signal_literal(STDERR_FILENO, "SIGILL", 6);
        break;
    case SIGSEGV:
        write_signal_literal(STDERR_FILENO, "SIGSEGV", 7);
        break;
    default:
        write_signal_literal(STDERR_FILENO, "unknown", 7);
        break;
    }
}

static void fatal_signal_handler(int sig) {
    static const char prefix[] = "\r\nFatal signal: ";
    static const char suffix[] = " while rendering. Terminal recovery was attempted.\r\n";

    if (g_terminal_session_active) {
        terminal_restore_after_crash();
        g_terminal_session_active = 0;
    }

    write_signal_literal(STDERR_FILENO, prefix, sizeof(prefix) - 1);
    write_signal_name(sig);
    write_signal_literal(STDERR_FILENO, suffix, sizeof(suffix) - 1);
    _Exit(128 + sig);
}

static void record_fatal_report(FatalReport *report, const char *format, ...) {
    if (report->active) {
        return;
    }

    report->active = true;

    va_list args;
    va_start(args, format);
    vsnprintf(report->message, sizeof(report->message), format, args);
    va_end(args);
}

static inline double get_time_seconds(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency = {0};
    LARGE_INTEGER counter;
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

typedef struct RenderContext {
    VulkanRenderer *renderer;
    mat4 model_matrix;
    RenderMaterial *materials;
    uint32_t material_count;
    bool enable_lighting;
    bool use_triplanar_mapping;
} RenderContext;

typedef struct AnimationContext {
    mat4 *bone_matrices;
    bool has_animations;
} AnimationContext;

typedef enum OutputMode {
    OUTPUT_MODE_AUTO = 0,
    OUTPUT_MODE_KITTY_SHM,
    OUTPUT_MODE_KITTY_DIRECT,
    OUTPUT_MODE_SIXEL,
    OUTPUT_MODE_TRUECOLOR_CHARACTERS,
    OUTPUT_MODE_PALETTE_CHARACTERS,
    OUTPUT_MODE_BLOCK_CHARACTERS,
} OutputMode;

typedef struct TerminalSession {
    bool active;
    bool mouse_orbit_enabled;
} TerminalSession;

static float compute_model_scale_factor(const CameraSetup *camera_setup, float model_scale_arg) {
    if (camera_setup->model_scale <= 0.0f) {
        return 1.0f;
    }

    return (TARGET_SIZE / camera_setup->model_scale) * model_scale_arg;
}

static OutputMode output_mode_from_args(const Args *args) {
    if (args->use_kitty_shm) {
        return OUTPUT_MODE_KITTY_SHM;
    }
    if (args->use_kitty) {
        return OUTPUT_MODE_KITTY_DIRECT;
    }
    if (args->use_sixel) {
        return OUTPUT_MODE_SIXEL;
    }
    if (args->use_truecolor_characters) {
        return OUTPUT_MODE_TRUECOLOR_CHARACTERS;
    }
    if (args->use_palette_characters) {
        return OUTPUT_MODE_PALETTE_CHARACTERS;
    }
    if (args->use_block_characters) {
        return OUTPUT_MODE_BLOCK_CHARACTERS;
    }
    return OUTPUT_MODE_AUTO;
}

static OutputMode detect_output_mode(void) {
    if (detect_kitty_shm_support()) {
        return OUTPUT_MODE_KITTY_SHM;
    }
    if (detect_kitty_support()) {
        return OUTPUT_MODE_KITTY_DIRECT;
    }
    if (detect_sixel_support()) {
        return OUTPUT_MODE_SIXEL;
    }
    if (detect_truecolor_support()) {
        return OUTPUT_MODE_TRUECOLOR_CHARACTERS;
    }
    return OUTPUT_MODE_PALETTE_CHARACTERS;
}

static bool output_mode_uses_kitty(const OutputMode output_mode) {
    return output_mode == OUTPUT_MODE_KITTY_SHM || output_mode == OUTPUT_MODE_KITTY_DIRECT;
}

static bool output_mode_uses_character_cells(const OutputMode output_mode) {
    return output_mode == OUTPUT_MODE_TRUECOLOR_CHARACTERS ||
           output_mode == OUTPUT_MODE_PALETTE_CHARACTERS ||
           output_mode == OUTPUT_MODE_BLOCK_CHARACTERS;
}

static bool output_mode_supported_on_platform(const OutputMode output_mode) {
#ifdef _WIN32
    if (output_mode == OUTPUT_MODE_KITTY_SHM || output_mode == OUTPUT_MODE_KITTY_DIRECT ||
        output_mode == OUTPUT_MODE_SIXEL) {
        return false;
    }
#endif
    return true;
}

static const char *output_mode_flag_name(const OutputMode output_mode) {
    switch (output_mode) {
    case OUTPUT_MODE_KITTY_SHM:
        return "--kitty";
    case OUTPUT_MODE_KITTY_DIRECT:
        return "--kitty-direct";
    case OUTPUT_MODE_SIXEL:
        return "--sixel";
    default:
        return "auto";
    }
}

static void calculate_output_dimensions(const Args *args, OutputMode output_mode, uint32_t *width,
                                        uint32_t *height) {
    bool use_hash_characters =
        args->use_hash_characters && output_mode_uses_character_cells(output_mode);
    calculate_render_dimensions(args->width, args->height, output_mode == OUTPUT_MODE_SIXEL,
                                output_mode_uses_kitty(output_mode), use_hash_characters,
                                args->show_status_bar, width, height);
}

static void render_output_frame(OutputMode output_mode, const uint8_t *framebuffer, uint32_t width,
                                uint32_t height, bool use_hash_characters) {
    bool hash_for_characters = use_hash_characters && output_mode_uses_character_cells(output_mode);
    switch (output_mode) {
    case OUTPUT_MODE_KITTY_SHM:
        render_kitty_shm(framebuffer, width, height);
        break;
    case OUTPUT_MODE_KITTY_DIRECT:
        render_kitty(framebuffer, width, height);
        break;
    case OUTPUT_MODE_SIXEL:
        render_sixel(framebuffer, width, height);
        break;
    case OUTPUT_MODE_PALETTE_CHARACTERS:
        render_palette_characters(framebuffer, width, height, hash_for_characters);
        break;
    case OUTPUT_MODE_BLOCK_CHARACTERS:
        render_block_characters(framebuffer, width, height, hash_for_characters);
        break;
    case OUTPUT_MODE_TRUECOLOR_CHARACTERS:
    case OUTPUT_MODE_AUTO:
        render_truecolor_characters(framebuffer, width, height, hash_for_characters);
        break;
    }
}

static void terminal_session_begin(TerminalSession *session, bool mouse_orbit) {
    session->active = true;
    session->mouse_orbit_enabled = mouse_orbit;
    g_terminal_session_active = 1;
    terminal_arm_recovery();

    hide_cursor();
    enter_alternate_screen();
    enable_raw_mode();
    terminal_set_mouse_input_enabled(mouse_orbit);
    enable_kitty_keyboard();
    if (mouse_orbit) {
        enable_mouse_orbit_tracking();
    }
}

static void terminal_session_end(TerminalSession *session) {
    if (!session->active) {
        return;
    }

    terminal_restore_default_state();
    g_terminal_session_active = 0;
    session->active = false;
    session->mouse_orbit_enabled = false;
}

static void refresh_camera_matrices(Camera *camera, mat4 view, mat4 projection) {
    camera_view_matrix(camera, view);
    camera_projection_matrix(camera, projection);
}

static bool initialize_bone_matrices(mat4 **out_bone_matrices) {
    *out_bone_matrices = aligned_malloc(MAX_BONES * sizeof(mat4));
    if (!*out_bone_matrices) {
        return false;
    }

    for (int i = 0; i < MAX_BONES; i++) {
        glm_mat4_identity((*out_bone_matrices)[i]);
    }
    return true;
}

static float calculate_frame_fps(float delta_time) {
    return delta_time > 0.0f ? 1.0f / delta_time : 0.0f;
}

static float clamp_simulation_delta(const double frame_delta_seconds) {
    if (frame_delta_seconds <= 0.0) {
        return 0.0f;
    }

    if (frame_delta_seconds > MAX_SIMULATION_DELTA_SECONDS) {
        return MAX_SIMULATION_DELTA_SECONDS;
    }

    return (float)frame_delta_seconds;
}

static void pace_frame(const double frame_start_time, const double target_frame_time) {
    if (target_frame_time <= 0.0) {
        return;
    }

    const double frame_deadline = frame_start_time + target_frame_time;
    for (;;) {
        const double remaining = frame_deadline - get_time_seconds();
        if (remaining <= 0.0) {
            return;
        }

        if (remaining > 0.003) {
            const unsigned int sleep_ms = (unsigned int)((remaining - 0.001) * 1000.0);
            if (sleep_ms > 0) {
                dcat_sleep_ms(sleep_ms);
                continue;
            }
        }

        dcat_sleep_ms(0);
    }
}

static bool resize_renderer_if_needed(const Args *args, OutputMode output_mode,
                                      VulkanRenderer *renderer, DcatMutex *shared_state_mutex,
                                      Camera *camera, uint32_t *width, uint32_t *height, mat4 view,
                                      mat4 projection) {
#ifdef _WIN32
    g_resize_pending = 1;
#endif
    if (!g_resize_pending) {
        return true;
    }

    g_resize_pending = 0;

    uint32_t new_width = 0;
    uint32_t new_height = 0;
    calculate_output_dimensions(args, output_mode, &new_width, &new_height);
    if (new_width == *width && new_height == *height) {
        return true;
    }

    *width = new_width;
    *height = new_height;
    if (!vulkan_renderer_resize(renderer, *width, *height)) {
        return false;
    }

    dcat_mutex_lock(shared_state_mutex);
    camera_init(camera, *width, *height, camera->position, camera->target, 60.0f);
    refresh_camera_matrices(camera, view, projection);
    dcat_mutex_unlock(shared_state_mutex);
    return true;
}

static const char *get_animation_name(const AnimationContext *anim_ctx, const Mesh *mesh,
                                      int current_animation_index) {
    if (!anim_ctx->has_animations || current_animation_index < 0 ||
        current_animation_index >= (int)mesh->animations.count) {
        return "";
    }

    return mesh->animations.data[current_animation_index].name;
}

static void setup_model_transform(const Mesh *mesh, const CameraSetup *camera_setup,
                                  float model_scale_arg, mat4 out_matrix) {
    float model_scale_factor = compute_model_scale_factor(camera_setup, model_scale_arg);
    vec3 model_center;
    glm_vec3_zero(model_center);

    if (camera_setup->model_scale > 0.0f) {
        glm_vec3_copy((float *)camera_setup->target, model_center);
    }

    glm_mat4_identity(out_matrix);
    glm_mat4_mul(out_matrix, (vec4 *)mesh->coordinate_system_transform, out_matrix);

    mat4 scale_mat;
    glm_scale_make(scale_mat, (vec3){model_scale_factor, model_scale_factor, model_scale_factor});
    glm_mat4_mul(out_matrix, scale_mat, out_matrix);

    mat4 translate_mat;
    vec3 neg_center;
    glm_vec3_negate_to(model_center, neg_center);
    glm_translate_make(translate_mat, neg_center);
    glm_mat4_mul(out_matrix, translate_mat, out_matrix);
}

static void setup_camera_position(const CameraSetup *camera_setup, float model_scale_arg,
                                  float camera_distance_arg, vec3 out_position) {
    const float model_scale_factor = compute_model_scale_factor(camera_setup, model_scale_arg);
    vec3 camera_offset;
    glm_vec3_sub((float *)camera_setup->position, (float *)camera_setup->target, camera_offset);
    glm_vec3_scale(camera_offset, model_scale_factor, camera_offset);

    vec3 camera_target;
    glm_vec3_zero(camera_target);

    if (camera_distance_arg > 0) {
        vec3 direction;
        glm_vec3_normalize_to(camera_offset, direction);
        glm_vec3_scale(direction, camera_distance_arg, camera_offset);
    }

    glm_vec3_add(camera_target, camera_offset, out_position);
}

static void process_input_devices(const KeyState *key_state, Camera *camera, const float delta_time,
                                  float *move_speed) {
    if (key_state->q) {
        set_atomic_flag(&g_running, false);
        return;
    }

    if (key_state->v)
        *move_speed /= (1.0f + delta_time);
    if (key_state->b)
        *move_speed *= (1.0f + delta_time);

    float speed = (*move_speed) * delta_time;
    if (key_state->ctrl)
        speed *= 0.25f;

    if (key_state->w)
        camera_move_forward(camera, speed);
    if (key_state->s)
        camera_move_backward(camera, speed);
    if (key_state->a)
        camera_move_left(camera, speed);
    if (key_state->d)
        camera_move_right(camera, speed);
    if (key_state->space)
        camera_move_up(camera, speed);
    if (key_state->shift)
        camera_move_down(camera, speed);

    if (key_state->mouse_dx != 0 || key_state->mouse_dy != 0) {
        const float ROTATION_SENSITIVITY = 2.0f;
        const float sensitivity = ROTATION_SENSITIVITY * 0.001f;
        camera_rotate(camera, key_state->mouse_dx * sensitivity,
                      -key_state->mouse_dy * sensitivity);
    }

    float rot_speed = 2.0f * delta_time;
    if (key_state->i)
        camera_rotate(camera, 0.0f, rot_speed);
    if (key_state->k)
        camera_rotate(camera, 0.0f, -rot_speed);
    if (key_state->j)
        camera_rotate(camera, -rot_speed, 0.0f);
    if (key_state->l)
        camera_rotate(camera, rot_speed, 0.0f);
}

static bool render_frame(RenderContext *ctx, const AnimationContext *anim_ctx, const Mesh *mesh,
                         mat4 *view, mat4 *projection, OutputMode output_mode, bool show_status_bar,
                         bool use_hash_characters, uint32_t width, uint32_t height, float fps,
                         float move_speed, const vec3 camera_position,
                         int current_animation_index) {
    mat4 mvp;
    glm_mat4_mul(*projection, *view, mvp);
    glm_mat4_mul(mvp, ctx->model_matrix, mvp);

    const uint8_t *framebuffer = NULL;
    const mat4 *bone_matrix_ptr = NULL;
    uint32_t bone_count = 0;

    if (anim_ctx->has_animations) {
        bone_matrix_ptr = (const mat4 *)anim_ctx->bone_matrices;
        bone_count = (uint32_t)mesh->skeleton.bones.count;
    }

    if (!vulkan_renderer_render(ctx->renderer, mesh, &mvp, &ctx->model_matrix, ctx->materials,
                                ctx->material_count, ctx->enable_lighting, camera_position,
                                ctx->use_triplanar_mapping, bone_matrix_ptr, bone_count, view,
                                projection, &framebuffer)) {
        return false;
    }

    if (framebuffer) {
        render_output_frame(output_mode, framebuffer, width, height, use_hash_characters);

        if (show_status_bar) {
            draw_status_bar(fps, move_speed, camera_position,
                            get_animation_name(anim_ctx, mesh, current_animation_index));
        }
    }

    return true;
}

int main(int argc, char *argv[]) {
    Args args = parse_args(argc, argv);
    if (!validate_args(&args)) {
        return 1;
    }

    if (VIPS_INIT(argv[0])) {
        fprintf(stderr, "Failed to initialize libvips\n");
        return 1;
    }

    set_atomic_flag(&g_running, true);

    int exit_code = 1;
    OutputMode output_mode = output_mode_from_args(&args);
    if (output_mode == OUTPUT_MODE_AUTO) {
        output_mode = detect_output_mode();
    }
    if (!output_mode_supported_on_platform(output_mode)) {
        fprintf(stderr,
                "Output mode %s is not supported on native Windows. "
                "Use --truecolor-characters, --palette-characters, or "
                "--block-characters.\n",
                output_mode_flag_name(output_mode));
        vips_shutdown();
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifdef SIGWINCH
    signal(SIGWINCH, resize_handler);
#endif

#ifdef _WIN32
    signal(SIGABRT, fatal_signal_handler);
#ifdef SIGFPE
    signal(SIGFPE, fatal_signal_handler);
#endif
#ifdef SIGILL
    signal(SIGILL, fatal_signal_handler);
#endif
#ifdef SIGSEGV
    signal(SIGSEGV, fatal_signal_handler);
#endif
#else
    struct sigaction fatal_action = {0};
    fatal_action.sa_handler = fatal_signal_handler;
    sigemptyset(&fatal_action.sa_mask);
    sigaction(SIGABRT, &fatal_action, NULL);
#ifdef SIGBUS
    sigaction(SIGBUS, &fatal_action, NULL);
#endif
    sigaction(SIGFPE, &fatal_action, NULL);
    sigaction(SIGILL, &fatal_action, NULL);
    sigaction(SIGSEGV, &fatal_action, NULL);
#endif

    uint32_t width = 0;
    uint32_t height = 0;
    calculate_output_dimensions(&args, output_mode, &width, &height);
    g_resize_pending = 0;

    VulkanRenderer *renderer = NULL;
    Mesh mesh;
    mesh_init(&mesh);
    MaterialInfo *model_materials = NULL;
    size_t model_material_count = 0;
    Texture *diffuse_textures = NULL;
    Texture *normal_textures = NULL;
    RenderMaterial *render_materials = NULL;
    Mesh skydome_mesh;
    mesh_init(&skydome_mesh);
    Texture skydome_texture = {0};
    mat4 *bone_matrices = NULL;
    bool has_uvs = false;
    bool has_animations = false;
    bool has_skydome = false;
    DcatMutex shared_state_mutex;
    bool shared_state_mutex_initialized = false;
    DcatThread input_thread;
    bool input_thread_started = false;
    TerminalSession terminal_session = {0};
    FatalReport fatal_report = {0};

    renderer = vulkan_renderer_create(width, height);
    if (!renderer || !vulkan_renderer_initialize(renderer)) {
        const char *renderer_error = renderer ? vulkan_renderer_get_last_error(renderer) : NULL;
        fprintf(stderr, "%s\n",
                renderer_error ? renderer_error : "Failed to initialize Vulkan renderer");
        goto cleanup;
    }
    vulkan_renderer_set_light_direction(renderer, (vec3){0.0f, -1.0f, -0.5f});

    if (!load_model(args.model_path, &mesh, &has_uvs, &model_materials, &model_material_count)) {
        fprintf(stderr, "Failed to load model: %s\n", args.model_path);
        goto cleanup;
    }

    AnimationState anim_state;
    animation_state_init(&anim_state);
    if (!initialize_bone_matrices(&bone_matrices)) {
        fprintf(stderr, "Failed to allocate bone matrices\n");
        goto cleanup;
    }
    has_animations = mesh.has_animations && mesh.animations.count > 0;

    // Load textures for each material
    diffuse_textures = calloc(model_material_count, sizeof(Texture));
    normal_textures = calloc(model_material_count, sizeof(Texture));
    render_materials = calloc(model_material_count, sizeof(RenderMaterial));
    if (!diffuse_textures || !normal_textures || !render_materials) {
        fprintf(stderr, "Failed to allocate material resources\n");
        goto cleanup;
    }

    for (size_t i = 0; i < model_material_count; i++) {
        load_diffuse_texture(args.model_path, args.texture_path, &model_materials[i],
                             &diffuse_textures[i]);
        load_normal_texture(args.normal_map_path, &model_materials[i], &normal_textures[i]);

        render_materials[i].diffuse = &diffuse_textures[i];
        render_materials[i].normal = &normal_textures[i];
        render_materials[i].alpha_mode = model_materials[i].alpha_mode;
        render_materials[i].specular_strength = model_materials[i].specular_strength;
        render_materials[i].shininess = model_materials[i].shininess;
        memcpy(render_materials[i].base_color, model_materials[i].base_color, sizeof(float) * 4);
        render_materials[i].use_diffuse_alpha_as_luster =
            render_materials[i].alpha_mode == ALPHA_MODE_OPAQUE &&
            diffuse_textures[i].has_transparency;
    }

    has_skydome = load_skydome(args.skydome_path, &skydome_mesh, &skydome_texture);
    if (has_skydome) {
        if (!vulkan_renderer_set_skydome(renderer, &skydome_mesh, &skydome_texture)) {
            const char *renderer_error = vulkan_renderer_get_last_error(renderer);
            fprintf(stderr, "%s\n",
                    renderer_error ? renderer_error : "Failed to upload skydome resources");
            goto cleanup;
        }
    }

    CameraSetup camera_setup;
    calculate_camera_setup(&mesh.vertices, &camera_setup);

    mat4 model_matrix;
    setup_model_transform(&mesh, &camera_setup, args.model_scale, model_matrix);

    vec3 camera_position;
    setup_camera_position(&camera_setup, args.model_scale, args.camera_distance, camera_position);

    vec3 camera_target;
    glm_vec3_zero(camera_target);

    const float MOVE_SPEED_BASE = 0.5f;
    float move_speed = MOVE_SPEED_BASE * TARGET_SIZE;
    double target_frame_time = 1.0 / args.target_fps;

    KeyState key_state = {0};

    Camera camera;
    camera_init(&camera, width, height, camera_position, camera_target, 60.0f);

    mat4 view, projection;
    refresh_camera_matrices(&camera, view, projection);

    if (!dcat_mutex_init(&shared_state_mutex)) {
        fprintf(stderr, "Failed to initialize shared state mutex\n");
        goto cleanup;
    }
    shared_state_mutex_initialized = true;

    terminal_session_begin(&terminal_session, args.mouse_orbit);

    double last_frame_time = get_time_seconds();

    InputThreadData input_data = {&camera,
                                  renderer,
                                  &anim_state,
                                  &mesh,
                                  &shared_state_mutex,
                                  &g_running,
                                  args.fps_controls,
                                  args.mouse_orbit,
                                  args.mouse_sensitivity,
                                  has_animations,
                                  &key_state};
    if (!dcat_thread_create(&input_thread, input_thread_func, &input_data)) {
        record_fatal_report(&fatal_report, "Failed to start input thread");
        goto cleanup;
    }
    input_thread_started = true;

    RenderContext render_ctx = {
        .renderer = renderer,
        .model_matrix = {{0}},
        .materials = render_materials,
        .material_count = (uint32_t)model_material_count,
        .enable_lighting = !args.no_lighting,
        .use_triplanar_mapping = !has_uvs,
    };
    glm_mat4_copy(model_matrix, render_ctx.model_matrix);

    AnimationContext anim_ctx = {bone_matrices, has_animations};

    float total_spin = 0.0f;
    mat4 base_model_matrix;
    glm_mat4_copy(render_ctx.model_matrix, base_model_matrix);

    while (get_atomic_flag(&g_running)) {
        if (!resize_renderer_if_needed(&args, output_mode, renderer, &shared_state_mutex, &camera,
                                       &width, &height, view, projection)) {
            const char *renderer_error = vulkan_renderer_get_last_error(renderer);
            record_fatal_report(&fatal_report, "%s",
                                renderer_error ? renderer_error
                                               : "Failed to resize Vulkan renderer");
            goto cleanup;
        }

        double frame_start = get_time_seconds();
        double frame_delta = frame_start - last_frame_time;
        last_frame_time = frame_start;
        float delta_time = clamp_simulation_delta(frame_delta);
        float display_fps = calculate_frame_fps((float)frame_delta);
        vec3 camera_position_snapshot;
        int current_animation_index_snapshot = -1;

        if (args.spin_speed != 0.0f && !args.fps_controls) {
            total_spin += args.spin_speed * delta_time;
            mat4 rotation_mat;
            glm_rotate_make(rotation_mat, total_spin, (vec3){0.0f, 1.0f, 0.0f});
            glm_mat4_mul(rotation_mat, base_model_matrix, render_ctx.model_matrix);
        }

        dcat_mutex_lock(&shared_state_mutex);
        if (args.fps_controls) {
            process_input_devices(&key_state, &camera, delta_time, &move_speed);
        }
        vec3 camera_forward;
        camera_forward_direction(&camera, camera_forward);
        glm_vec3_negate(camera_forward);
        vulkan_renderer_set_light_direction(renderer, camera_forward);
        camera_view_matrix(&camera, view);
        glm_vec3_copy(camera.position, camera_position_snapshot);
        if (has_animations) {
            update_animation(&mesh, &anim_state, delta_time, bone_matrices);
            current_animation_index_snapshot = anim_state.current_animation_index;
        }
        dcat_mutex_unlock(&shared_state_mutex);

        if (!render_frame(&render_ctx, &anim_ctx, &mesh, &view, &projection, output_mode,
                           args.show_status_bar, args.use_hash_characters, width, height,
                           display_fps, move_speed, camera_position_snapshot,
                           current_animation_index_snapshot)) {
            const char *renderer_error = vulkan_renderer_get_last_error(renderer);
            record_fatal_report(&fatal_report, "%s",
                                renderer_error ? renderer_error : "Rendering failed");
            goto cleanup;
        }

        pace_frame(frame_start, target_frame_time);
    }

    exit_code = 0;

cleanup:
    set_atomic_flag(&g_running, false);
    if (input_thread_started) {
        dcat_thread_join(input_thread);
    }
    terminal_session_end(&terminal_session);
    if (fatal_report.active) {
        fprintf(stderr, "%s\n", fatal_report.message);
    }
    vulkan_renderer_wait_idle(renderer);
    if (shared_state_mutex_initialized) {
        dcat_mutex_destroy(&shared_state_mutex);
    }

    aligned_free(bone_matrices);
    if (diffuse_textures) {
        for (size_t i = 0; i < model_material_count; i++) {
            texture_free(&diffuse_textures[i]);
        }
        free(diffuse_textures);
    }
    if (normal_textures) {
        for (size_t i = 0; i < model_material_count; i++) {
            texture_free(&normal_textures[i]);
        }
        free(normal_textures);
    }
    free(render_materials);
    texture_free(&skydome_texture);
    mesh_free(&skydome_mesh);
    mesh_free(&mesh);
    materials_free(model_materials, model_material_count);
    vulkan_renderer_destroy(renderer);

    vips_shutdown();
    return exit_code;
}
