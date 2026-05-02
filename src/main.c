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
    OUTPUT_MODE_AUTO,
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
    if (output_mode == OUTPUT_MODE_KITTY_SHM || output_mode == OUTPUT_MODE_KITTY_DIRECT) {
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
    const bool use_hash_characters =
        args->use_hash_characters && output_mode_uses_character_cells(output_mode);
    calculate_render_dimensions(args->width, args->height, output_mode == OUTPUT_MODE_SIXEL,
                                output_mode_uses_kitty(output_mode), use_hash_characters,
                                args->show_status_bar, width, height);
}

static void render_output_frame(const OutputMode output_mode, const uint8_t *framebuffer, const uint32_t width,
                                const uint32_t height, const bool use_hash_characters) {
    const bool hash_for_characters = use_hash_characters && output_mode_uses_character_cells(output_mode);
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

static void terminal_session_begin(TerminalSession *session, const bool mouse_orbit) {
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

static void refresh_camera_matrices(const Camera *camera, mat4 view, mat4 projection) {
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

static float calculate_frame_fps(const float delta_time) {
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

static bool resize_renderer_if_needed(const Args *args, const OutputMode output_mode,
                                      VulkanRenderer *renderer, DcatMutex *shared_state_mutex,
                                      Camera *camera, uint32_t *width, uint32_t *height, mat4 view,
                                      mat4 projection) {
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

typedef struct AppContext {
    Args args;
    OutputMode output_mode;
    uint32_t width;
    uint32_t height;

    VulkanRenderer *renderer;
    Mesh mesh;
    bool has_uvs;
    bool has_animations;

    MaterialInfo *model_materials;
    size_t model_material_count;
    Texture *diffuse_textures;
    Texture *normal_textures;
    RenderMaterial *render_materials;

    Mesh skydome_mesh;
    Texture skydome_texture;
    bool has_skydome;

    mat4 *bone_matrices;
    AnimationState anim_state;

    DcatMutex shared_state_mutex;
    bool shared_state_mutex_initialized;

    DcatThread input_thread;
    bool input_thread_started;
    InputThreadData input_data;

    TerminalSession terminal_session;
    FatalReport fatal_report;

    Camera camera;
    mat4 model_matrix;
    float move_speed;
    double target_frame_time;
    KeyState key_state;
} AppContext;

static void app_cleanup(AppContext *app) {
    set_atomic_flag(&g_running, false);
    if (app->input_thread_started) {
        dcat_thread_join(app->input_thread);
    }
    terminal_session_end(&app->terminal_session);
    if (app->fatal_report.active) {
        fprintf(stderr, "%s\n", app->fatal_report.message);
    }
    if (app->renderer) {
        vulkan_renderer_wait_idle(app->renderer);
    }
    if (app->shared_state_mutex_initialized) {
        dcat_mutex_destroy(&app->shared_state_mutex);
    }

    aligned_free(app->bone_matrices);
    if (app->diffuse_textures) {
        for (size_t i = 0; i < app->model_material_count; i++) {
            texture_free(&app->diffuse_textures[i]);
        }
        free(app->diffuse_textures);
    }
    if (app->normal_textures) {
        for (size_t i = 0; i < app->model_material_count; i++) {
            texture_free(&app->normal_textures[i]);
        }
        free(app->normal_textures);
    }
    free(app->render_materials);
    texture_free(&app->skydome_texture);
    mesh_free(&app->skydome_mesh);
    mesh_free(&app->mesh);
    materials_free(app->model_materials, app->model_material_count);
    if (app->renderer) {
        vulkan_renderer_destroy(app->renderer);
    }

    vips_shutdown();
}

static bool app_init(AppContext *app, int argc, char *argv[]) {
    memset(app, 0, sizeof(AppContext));

    app->args = parse_args(argc, argv);
    if (!validate_args(&app->args)) {
        return false;
    }

    if (VIPS_INIT(argv[0])) {
        fprintf(stderr, "Failed to initialize libvips\n");
        return false;
    }

    set_atomic_flag(&g_running, true);

    app->output_mode = output_mode_from_args(&app->args);
    if (app->output_mode == OUTPUT_MODE_AUTO) {
        app->output_mode = detect_output_mode();
    }
    if (!output_mode_supported_on_platform(app->output_mode)) {
        fprintf(stderr,
                "Output mode %s is not supported on native Windows. "
                "Use --truecolor-characters, --palette-characters, or "
                "--block-characters.\n",
                output_mode_flag_name(app->output_mode));
        return false;
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

    calculate_output_dimensions(&app->args, app->output_mode, &app->width, &app->height);
    g_resize_pending = 0;

    mesh_init(&app->mesh);
    mesh_init(&app->skydome_mesh);

    app->renderer = vulkan_renderer_create(app->width, app->height);
    if (!app->renderer || !vulkan_renderer_initialize(app->renderer)) {
        const char *renderer_error = app->renderer ? vulkan_renderer_get_last_error(app->renderer) : NULL;
        fprintf(stderr, "%s\n",
                renderer_error ? renderer_error : "Failed to initialize Vulkan renderer");
        return false;
    }
    vulkan_renderer_set_light_direction(app->renderer, (vec3){0.0f, -1.0f, -0.5f});

    if (!load_model(app->args.model_path, &app->mesh, &app->has_uvs, &app->model_materials, &app->model_material_count)) {
        fprintf(stderr, "Failed to load model: %s\n", app->args.model_path);
        return false;
    }

    animation_state_init(&app->anim_state);
    if (!initialize_bone_matrices(&app->bone_matrices)) {
        fprintf(stderr, "Failed to allocate bone matrices\n");
        return false;
    }
    app->has_animations = app->mesh.has_animations && app->mesh.animations.count > 0;

    app->diffuse_textures = calloc(app->model_material_count, sizeof(Texture));
    app->normal_textures = calloc(app->model_material_count, sizeof(Texture));
    app->render_materials = calloc(app->model_material_count, sizeof(RenderMaterial));
    if (!app->diffuse_textures || !app->normal_textures || !app->render_materials) {
        fprintf(stderr, "Failed to allocate material resources\n");
        return false;
    }

    for (size_t i = 0; i < app->model_material_count; i++) {
        load_diffuse_texture(app->args.model_path, app->args.texture_path, &app->model_materials[i],
                             &app->diffuse_textures[i]);
        load_normal_texture(app->args.normal_map_path, &app->model_materials[i], &app->normal_textures[i]);

        app->render_materials[i].diffuse = &app->diffuse_textures[i];
        app->render_materials[i].normal = &app->normal_textures[i];
        app->render_materials[i].alpha_mode = app->model_materials[i].alpha_mode;
        app->render_materials[i].specular_strength = app->model_materials[i].specular_strength;
        app->render_materials[i].shininess = app->model_materials[i].shininess;
        memcpy(app->render_materials[i].base_color, app->model_materials[i].base_color, sizeof(float) * 4);
        app->render_materials[i].use_diffuse_alpha_as_luster =
            app->render_materials[i].alpha_mode == ALPHA_MODE_OPAQUE &&
            app->diffuse_textures[i].has_transparency;
    }

    app->has_skydome = load_skydome(app->args.skydome_path, &app->skydome_mesh, &app->skydome_texture);
    if (app->has_skydome) {
        if (!vulkan_renderer_set_skydome(app->renderer, &app->skydome_mesh, &app->skydome_texture)) {
            const char *renderer_error = vulkan_renderer_get_last_error(app->renderer);
            fprintf(stderr, "%s\n",
                    renderer_error ? renderer_error : "Failed to upload skydome resources");
            return false;
        }
    }

    CameraSetup camera_setup;
    calculate_camera_setup(&app->mesh.vertices, &camera_setup);

    setup_model_transform(&app->mesh, &camera_setup, app->args.model_scale, app->model_matrix);

    vec3 camera_position;
    setup_camera_position(&camera_setup, app->args.model_scale, app->args.camera_distance, camera_position);

    vec3 camera_target;
    glm_vec3_zero(camera_target);

    const float MOVE_SPEED_BASE = 0.5f;
    app->move_speed = MOVE_SPEED_BASE * TARGET_SIZE;
    app->target_frame_time = 1.0 / app->args.target_fps;

    camera_init(&app->camera, app->width, app->height, camera_position, camera_target, 60.0f);

    if (!dcat_mutex_init(&app->shared_state_mutex)) {
        fprintf(stderr, "Failed to initialize shared state mutex\n");
        return false;
    }
    app->shared_state_mutex_initialized = true;

    terminal_session_begin(&app->terminal_session, app->args.mouse_orbit);

    app->input_data = (InputThreadData){
        &app->camera,
        app->renderer,
        &app->anim_state,
        &app->mesh,
        &app->shared_state_mutex,
        &g_running,
        app->args.fps_controls,
        app->args.mouse_orbit,
        app->args.mouse_sensitivity,
        app->has_animations,
        &app->key_state,
        &g_resize_pending
    };

    if (!dcat_thread_create(&app->input_thread, input_thread_func, &app->input_data)) {
        record_fatal_report(&app->fatal_report, "Failed to start input thread");
        return false;
    }
    app->input_thread_started = true;

    return true;
}

static int app_run_loop(AppContext *app) {
    RenderContext render_ctx = {
        .renderer = app->renderer,
        .model_matrix = {{0}},
        .materials = app->render_materials,
        .material_count = (uint32_t)app->model_material_count,
        .enable_lighting = !app->args.no_lighting,
        .use_triplanar_mapping = !app->has_uvs,
    };
    glm_mat4_copy(app->model_matrix, render_ctx.model_matrix);

    AnimationContext anim_ctx = {app->bone_matrices, app->has_animations};

    float total_spin = 0.0f;
    mat4 base_model_matrix;
    glm_mat4_copy(render_ctx.model_matrix, base_model_matrix);

    mat4 view, projection;
    refresh_camera_matrices(&app->camera, view, projection);

    double last_frame_time = get_time_seconds();
    unsigned long long frame_count = 0;

    while (get_atomic_flag(&g_running)) {
        if (!resize_renderer_if_needed(&app->args, app->output_mode, app->renderer, &app->shared_state_mutex, &app->camera,
                                       &app->width, &app->height, view, projection)) {
            const char *renderer_error = vulkan_renderer_get_last_error(app->renderer);
            record_fatal_report(&app->fatal_report, "%s",
                                renderer_error ? renderer_error
                                               : "Failed to resize Vulkan renderer");
            return 1;
        }

        double frame_start = get_time_seconds();
        double frame_delta = frame_start - last_frame_time;
        last_frame_time = frame_start;
        float delta_time = clamp_simulation_delta(frame_delta);
        float display_fps = calculate_frame_fps((float)frame_delta);
        vec3 camera_position_snapshot;
        int current_animation_index_snapshot = -1;

        if (app->args.spin_speed != 0.0f && !app->args.fps_controls) {
            total_spin += app->args.spin_speed * delta_time;
            mat4 rotation_mat;
            glm_rotate_make(rotation_mat, total_spin, (vec3){0.0f, 1.0f, 0.0f});
            glm_mat4_mul(rotation_mat, base_model_matrix, render_ctx.model_matrix);
        }

        dcat_mutex_lock(&app->shared_state_mutex);
        if (app->args.fps_controls) {
            process_input_devices(&app->key_state, &app->camera, delta_time, &app->move_speed);
        }
        vec3 camera_forward;
        camera_forward_direction(&app->camera, camera_forward);
        glm_vec3_negate(camera_forward);
        vulkan_renderer_set_light_direction(app->renderer, camera_forward);
        camera_view_matrix(&app->camera, view);
        glm_vec3_copy(app->camera.position, camera_position_snapshot);
        if (app->has_animations) {
            update_animation(&app->mesh, &app->anim_state, delta_time, app->bone_matrices);
            current_animation_index_snapshot = app->anim_state.current_animation_index;
        }
        dcat_mutex_unlock(&app->shared_state_mutex);

        if (!render_frame(&render_ctx, &anim_ctx, &app->mesh, &view, &projection, app->output_mode,
                           app->args.show_status_bar, app->args.use_hash_characters, app->width, app->height,
                           display_fps, app->move_speed, camera_position_snapshot,
                           current_animation_index_snapshot)) {
            const char *renderer_error = vulkan_renderer_get_last_error(app->renderer);
            record_fatal_report(&app->fatal_report, "%s",
                                renderer_error ? renderer_error : "Rendering failed");
            return 1;
        }

        pace_frame(frame_start, app->target_frame_time);

        frame_count++;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    AppContext app;
    int exit_code = 1;

    if (app_init(&app, argc, argv)) {
        exit_code = app_run_loop(&app);
    }

    app_cleanup(&app);
    return exit_code;
}
