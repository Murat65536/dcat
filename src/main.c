#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

#include <vips/vips.h>

#include "core/args.h"
#include "graphics/camera.h"
#include "graphics/model.h"
#include "terminal/terminal.h"
#include "terminal/terminal_pixels.h"
#include "terminal/sixel.h"
#include "terminal/kitty.h"
#include "graphics/texture_loader.h"
#include "renderer/vulkan_renderer.h"
#include "input/input_device.h"
#include "input/input_handler.h"

// Global state for signal handlers
static atomic_bool g_running = true;
static volatile sig_atomic_t g_resize_pending = 1;

static void signal_handler(int sig) {
    (void)sig;
    atomic_store(&g_running, false);
}

static void resize_handler(int sig) {
    (void)sig;
    g_resize_pending = 1;
}

static double get_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

typedef struct RenderContext {
    VulkanRenderer* renderer;
    mat4 model_matrix;
    Texture* diffuse_texture;
    Texture* normal_texture;
    bool enable_lighting;
    bool has_uvs;
    AlphaMode alpha_mode;
} RenderContext;

typedef struct AnimationContext {
    mat4* bone_matrices;
    bool has_animations;
} AnimationContext;

static void setup_model_transform(const Mesh* mesh, const CameraSetup* camera_setup,
                                   float model_scale_arg, mat4 out_matrix) {
    const float TARGET_SIZE = 4.0f;
    float model_scale_factor = 1.0f;
    vec3 model_center;
    glm_vec3_zero(model_center);
    
    if (camera_setup->model_scale > 0.0f) {
        model_scale_factor = (TARGET_SIZE / camera_setup->model_scale) * model_scale_arg;
        glm_vec3_copy((float*)camera_setup->target, model_center);
    }
    
    glm_mat4_identity(out_matrix);
    glm_mat4_mul(out_matrix, (vec4*)mesh->coordinate_system_transform, out_matrix);
    
    mat4 scale_mat;
    glm_scale_make(scale_mat, (vec3){model_scale_factor, model_scale_factor, 
                                      model_scale_factor});
    glm_mat4_mul(out_matrix, scale_mat, out_matrix);
    
    mat4 translate_mat;
    vec3 neg_center;
    glm_vec3_negate_to(model_center, neg_center);
    glm_translate_make(translate_mat, neg_center);
    glm_mat4_mul(out_matrix, translate_mat, out_matrix);
}

static void setup_camera_position(const CameraSetup* camera_setup, float model_scale_arg,
                                   float camera_distance_arg, vec3 out_position) {
    const float TARGET_SIZE = 4.0f;
    float model_scale_factor = 1.0f;
    
    if (camera_setup->model_scale > 0.0f) {
        model_scale_factor = (TARGET_SIZE / camera_setup->model_scale) * model_scale_arg;
    }
    
    vec3 camera_offset;
    glm_vec3_sub((float*)camera_setup->position, (float*)camera_setup->target, camera_offset);
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

static void process_input_devices(InputManager* input_manager, KeyState* key_state,
                                   Camera* camera, float delta_time, float* move_speed,
                                   bool is_focused) {
    if (!input_manager || !is_focused) {
        return;
    }
    
    input_manager_process_events(input_manager, key_state);
    
    if (key_state->q) {
        atomic_store(&g_running, false);
        return;
    }
    
    if (key_state->v) *move_speed /= (1.0f + delta_time);
    if (key_state->b) *move_speed *= (1.0f + delta_time);
    
    float speed = (*move_speed) * delta_time;
    if (key_state->ctrl) speed *= 0.25f;
    
    if (key_state->w) camera_move_forward(camera, speed);
    if (key_state->s) camera_move_backward(camera, speed);
    if (key_state->a) camera_move_left(camera, speed);
    if (key_state->d) camera_move_right(camera, speed);
    if (key_state->space) camera_move_up(camera, speed);
    if (key_state->shift) camera_move_down(camera, speed);
    
    if (key_state->mouse_dx != 0 || key_state->mouse_dy != 0) {
        const float ROTATION_SENSITIVITY = 2.0f;
        float sensitivity = ROTATION_SENSITIVITY * 0.001f;
        camera_rotate(camera, key_state->mouse_dx * sensitivity,
                     -key_state->mouse_dy * sensitivity);
    }
    
    float rot_speed = 2.0f * delta_time;
    if (key_state->i) camera_rotate(camera, 0.0f, rot_speed);
    if (key_state->k) camera_rotate(camera, 0.0f, -rot_speed);
    if (key_state->j) camera_rotate(camera, -rot_speed, 0.0f);
    if (key_state->l) camera_rotate(camera, rot_speed, 0.0f);
}

static void render_frame(const RenderContext* ctx, const AnimationContext* anim_ctx,
                         const Mesh* mesh, mat4 view, mat4 projection,
                         const Args* args, uint32_t width, uint32_t height, 
                         float fps, float move_speed, const vec3 camera_position,
                         int current_animation_index) {
    mat4 mvp;
    glm_mat4_mul((vec4*)projection, (vec4*)view, mvp);
    glm_mat4_mul(mvp, (vec4*)ctx->model_matrix, mvp);
    
    const mat4* bone_matrix_ptr = NULL;
    uint32_t bone_count = 0;
    
    if (anim_ctx->has_animations) {
        bone_matrix_ptr = (const mat4*)anim_ctx->bone_matrices;
        bone_count = (uint32_t)mesh->skeleton.bones.count;
    }
    
    const uint8_t* framebuffer = vulkan_renderer_render(
        ctx->renderer, mesh, mvp, ctx->model_matrix,
        ctx->diffuse_texture, ctx->normal_texture, ctx->enable_lighting,
        camera_position, ctx->has_uvs, ctx->alpha_mode,
        bone_matrix_ptr, bone_count, (const mat4*)&view, (const mat4*)&projection
    );
    
    if (framebuffer) {
        if (args->use_kitty) {
            render_kitty(framebuffer, width, height);
        } else if (args->use_sixel) {
            render_sixel(framebuffer, width, height);
        } else {
            render_terminal(framebuffer, width, height);
        }
        
        if (args->show_status_bar) {
            const char* anim_name = "";
            if (anim_ctx->has_animations && current_animation_index >= 0 &&
                current_animation_index < (int)mesh->animations.count) {
                anim_name = mesh->animations.data[current_animation_index].name;
            }
            draw_status_bar(fps, move_speed, camera_position, anim_name);
        }
    }
}

int main(int argc, char* argv[]) {
    if (VIPS_INIT(argv[0])) {
        fprintf(stderr, "Failed to initialize libvips\n");
        return 1;
    }

    Args args = parse_args(argc, argv);
    if (!validate_args(&args)) {
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGWINCH, resize_handler);
    enable_focus_tracking();
    
    uint32_t width, height;
    calculate_render_dimensions(args.width, args.height, args.use_sixel, 
                                args.use_kitty, args.show_status_bar, &width, &height);
    g_resize_pending = 0;
    
    VulkanRenderer* renderer = vulkan_renderer_create(width, height);
    if (!renderer || !vulkan_renderer_initialize(renderer)) {
        fprintf(stderr, "Failed to initialize Vulkan renderer\n");
        disable_focus_tracking();
        return 1;
    }
    vulkan_renderer_set_light_direction(renderer, (vec3){0.0f, -1.0f, -0.5f});
    
    Mesh mesh;
    mesh_init(&mesh);
    bool has_uvs = false;
    MaterialInfo material_info;
    material_info_init(&material_info);
    
    if (!load_model(args.model_path, &mesh, &has_uvs, &material_info)) {
        fprintf(stderr, "Failed to load model: %s\n", args.model_path);
        vulkan_renderer_destroy(renderer);
        disable_focus_tracking();
        return 1;
    }
    
    AnimationState anim_state;
    animation_state_init(&anim_state);
    mat4* bone_matrices = aligned_malloc(MAX_BONES * sizeof(mat4));
    for (int i = 0; i < MAX_BONES; i++) {
        glm_mat4_identity(bone_matrices[i]);
    }
    bool has_animations = mesh.has_animations && mesh.animations.count > 0;
    
    Texture diffuse_texture, normal_texture;
    load_diffuse_texture(args.model_path, args.texture_path,
                         &material_info, &diffuse_texture);
    load_normal_texture(args.normal_map_path, &material_info, &normal_texture);

    AlphaMode alpha_mode = material_info.alpha_mode;
    if (alpha_mode == ALPHA_MODE_OPAQUE && diffuse_texture.has_transparency) {
        alpha_mode = ALPHA_MODE_BLEND;
    }
    
    Mesh skydome_mesh;
    Texture skydome_texture;
    bool has_skydome = load_skydome(args.skydome_path, &skydome_mesh, &skydome_texture);
    if (has_skydome) {
        vulkan_renderer_set_skydome(renderer, &skydome_mesh, &skydome_texture);
    }
    
    CameraSetup camera_setup;
    calculate_camera_setup(&mesh.vertices, &camera_setup);
    
    mat4 model_matrix;
    setup_model_transform(&mesh, &camera_setup, args.model_scale, model_matrix);
    
    vec3 camera_position;
    setup_camera_position(&camera_setup, args.model_scale, args.camera_distance, 
                          camera_position);
    
    vec3 camera_target;
    glm_vec3_zero(camera_target);
    
    const float TARGET_SIZE = 4.0f;
    const float MOVE_SPEED_BASE = 0.5f;
    float move_speed = MOVE_SPEED_BASE * TARGET_SIZE;
    double target_frame_time = 1.0 / args.target_fps;
    
    KeyState key_state = {0};
    InputManager* input_manager = input_manager_create();
    bool input_devices_ready = input_manager_initialize(input_manager, true);
    
    if (args.fps_controls && !input_devices_ready) {
        fprintf(stderr, "Warning: Could not initialize input devices for FPS controls\n");
    }
    
    hide_cursor();
    enter_alternate_screen();
    enable_raw_mode();
    
    Camera camera;
    camera_init(&camera, width, height, camera_position, camera_target, 60.0f);
    
    mat4 view, projection;
    camera_view_matrix(&camera, view);
    camera_projection_matrix(&camera, projection);
    pthread_mutex_t shared_state_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    double last_frame_time = get_time_seconds();
    atomic_bool is_focused = true;
    
    InputThreadData input_data = {
        &camera, renderer, &anim_state, &mesh, &shared_state_mutex, &is_focused, &g_running,
        args.fps_controls, has_animations
    };
    pthread_t input_thread;
    pthread_create(&input_thread, NULL, input_thread_func, &input_data);
    
    RenderContext render_ctx = {
        .renderer = renderer,
        .model_matrix = {{0}},
        .diffuse_texture = &diffuse_texture,
        .normal_texture = &normal_texture,
        .enable_lighting = !args.no_lighting,
        .has_uvs = !has_uvs,
        .alpha_mode = alpha_mode
    };
    glm_mat4_copy(model_matrix, render_ctx.model_matrix);
    
    AnimationContext anim_ctx = {
        bone_matrices, has_animations
    };
    
    while (atomic_load(&g_running)) {
        if (g_resize_pending) {
            g_resize_pending = 0;
            uint32_t new_width, new_height;
            calculate_render_dimensions(args.width, args.height, args.use_sixel, 
                                       args.use_kitty, args.show_status_bar, 
                                       &new_width, &new_height);
            if (new_width != width || new_height != height) {
                width = new_width;
                height = new_height;
                vulkan_renderer_resize(renderer, width, height);
                pthread_mutex_lock(&shared_state_mutex);
                camera_init(&camera, width, height, camera.position, camera.target, 90.0f);
                camera_view_matrix(&camera, view);
                camera_projection_matrix(&camera, projection);
                pthread_mutex_unlock(&shared_state_mutex);
            }
        }
        
        double frame_start = get_time_seconds();
        float delta_time = (float)(frame_start - last_frame_time);
        last_frame_time = frame_start;
        vec3 camera_position_snapshot;
        int current_animation_index_snapshot = -1;

        pthread_mutex_lock(&shared_state_mutex);
        if (input_devices_ready && atomic_load(&is_focused) && args.fps_controls) {
            process_input_devices(input_manager, &key_state, &camera, delta_time, 
                                &move_speed, atomic_load(&is_focused));
        }
        camera_view_matrix(&camera, view);
        glm_vec3_copy(camera.position, camera_position_snapshot);
        if (has_animations) {
            update_animation(&mesh, &anim_state, delta_time, bone_matrices);
            current_animation_index_snapshot = anim_state.current_animation_index;
        }
        pthread_mutex_unlock(&shared_state_mutex);
        
        render_frame(&render_ctx, &anim_ctx, &mesh, view, projection, &args, 
                    width, height, 1.0f / delta_time, move_speed,
                    camera_position_snapshot, current_animation_index_snapshot);
        
        double frame_end = get_time_seconds();
        double frame_duration = frame_end - frame_start;
        if (frame_duration < target_frame_time) {
            usleep((useconds_t)((target_frame_time - frame_duration) * 1e6));
        }
    }
    
    vulkan_renderer_wait_idle(renderer);
    pthread_join(input_thread, NULL);
    
    disable_raw_mode();
    exit_alternate_screen();
    show_cursor();
    disable_focus_tracking();
    pthread_mutex_destroy(&shared_state_mutex);
    
    free(bone_matrices);
    texture_free(&diffuse_texture);
    texture_free(&normal_texture);
    if (has_skydome) {
        mesh_free(&skydome_mesh);
        texture_free(&skydome_texture);
    }
    mesh_free(&mesh);
    material_info_free(&material_info);
    input_manager_destroy(input_manager);
    vulkan_renderer_destroy(renderer);

    vips_shutdown();
    return 0;
}
