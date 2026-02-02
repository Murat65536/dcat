#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <math.h>
#include <time.h>

#include <assimp/cimport.h>
#include <assimp/scene.h>

#include "camera.h"
#include "model.h"
#include "texture.h"
#include "vulkan_renderer.h"
#include "terminal.h"
#include "input_device.h"
#include "skydome.h"

typedef struct Args {
    char* model_path;
    char* texture_path;
    char* normal_map_path;
    char* skydome_path;
    int width;
    int height;
    float camera_distance;
    float model_scale;
    int target_fps;
    bool use_sixel;
    bool use_kitty;
    bool no_lighting;
    bool fps_controls;
    bool show_status_bar;
    bool show_help;
} Args;

static atomic_bool g_running = true;

static void signal_handler(int sig) {
    (void)sig;
    atomic_store(&g_running, false);
}

static void print_usage(void) {
    printf("Usage: dcat [OPTION]... [MODEL]\n\n");
    printf("  -t, --texture PATH       path to the texture file (defaults to gray)\n");
    printf("  -n, --normal-map PATH    path to normal image file\n");
    printf("      --skydome PATH       path to skydome texture file\n");
    printf("  -W, --width WIDTH        renderer width (defaults to terminal width)\n");
    printf("  -H, --height HEIGHT      renderer height (defaults to terminal height)\n");
    printf("      --camera-distance DIST  camera distance from origin\n");
    printf("      --model-scale SCALE  scale multiplier for the model\n");
    printf("  -f, --fps FPS            target frames per second (default: 60)\n");
    printf("  -S, --sixel              enable Sixel graphics mode\n");
    printf("  -K, --kitty              enable Kitty graphics protocol mode\n");
    printf("      --no-lighting        disable lighting calculations\n");
    printf("      --fps-controls       enable first-person camera controls\n");
    printf("  -s, --status-bar         show status bar\n");
    printf("  -h, --help               display this help and exit\n\n");
}

static Args parse_args(int argc, char* argv[]) {
    Args args = {0};
    args.width = -1;
    args.height = -1;
    args.camera_distance = -1.0f;
    args.model_scale = 1.0f;
    args.target_fps = 60;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--texture") == 0) {
            if (++i < argc) args.texture_path = argv[i];
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--normal-map") == 0) {
            if (++i < argc) args.normal_map_path = argv[i];
        } else if (strcmp(argv[i], "--skydome") == 0) {
            if (++i < argc) args.skydome_path = argv[i];
        } else if (strcmp(argv[i], "-W") == 0 || strcmp(argv[i], "--width") == 0) {
            if (++i < argc) args.width = atoi(argv[i]);
        } else if (strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--height") == 0) {
            if (++i < argc) args.height = atoi(argv[i]);
        } else if (strcmp(argv[i], "--camera-distance") == 0) {
            if (++i < argc) args.camera_distance = atof(argv[i]);
        } else if (strcmp(argv[i], "--model-scale") == 0) {
            if (++i < argc) args.model_scale = atof(argv[i]);
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fps") == 0) {
            if (++i < argc) args.target_fps = atoi(argv[i]);
        } else if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--sixel") == 0) {
            args.use_sixel = true;
        } else if (strcmp(argv[i], "-K") == 0 || strcmp(argv[i], "--kitty") == 0) {
            args.use_kitty = true;
        } else if (strcmp(argv[i], "--no-lighting") == 0) {
            args.no_lighting = true;
        } else if (strcmp(argv[i], "--fps-controls") == 0) {
            args.fps_controls = true;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--status-bar") == 0) {
            args.show_status_bar = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            args.show_help = true;
        } else if (argv[i][0] != '-') {
            args.model_path = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage();
            exit(1);
        }
    }
    
    if (args.show_help) {
        print_usage();
        exit(0);
    }
    
    return args;
}

typedef struct InputThreadData {
    Camera* camera;
    VulkanRenderer* renderer;
    AnimationState* anim_state;
    Mesh* mesh;
    atomic_bool* is_focused;
    bool fps_controls;
    bool has_animations;
} InputThreadData;

static void* input_thread_func(void* arg) {
    InputThreadData* data = (InputThreadData*)arg;
    char buffer[64];
    
    while (atomic_load(&g_running)) {
        struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
        int ret = poll(&pfd, 1, 0);
        
        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
            
            if (n > 0) {
                for (ssize_t i = 0; i < n; i++) {
                    if (buffer[i] == 'q' || buffer[i] == 'Q') {
                        atomic_store(&g_running, false);
                        return NULL;
                    }
                    
                    if (buffer[i] == 'm' || buffer[i] == 'M') {
                        vulkan_renderer_set_wireframe_mode(data->renderer, !vulkan_renderer_get_wireframe_mode(data->renderer));
                    }
                    
                    if (!data->fps_controls) {
                        const float ROTATION_AMOUNT = GLM_PI / 8;
                        const float ZOOM_AMOUNT = 0.25f;
                        if (buffer[i] == 'a' || buffer[i] == 'A') camera_orbit(data->camera, ROTATION_AMOUNT, 0.0f);
                        if (buffer[i] == 'd' || buffer[i] == 'D') camera_orbit(data->camera, -ROTATION_AMOUNT, 0.0f);
                        if (buffer[i] == 'w' || buffer[i] == 'W') camera_orbit(data->camera, 0.0f, -ROTATION_AMOUNT);
                        if (buffer[i] == 's' || buffer[i] == 'S') camera_orbit(data->camera, 0.0f, ROTATION_AMOUNT);
                        if (buffer[i] == 'e' || buffer[i] == 'E') camera_zoom(data->camera, ZOOM_AMOUNT);
                        if (buffer[i] == 'r' || buffer[i] == 'R') camera_zoom(data->camera, -ZOOM_AMOUNT);
                    }
                    
                    // Animation controls
                    if (data->has_animations) {
                        if (buffer[i] == '1') {
                            data->anim_state->current_animation_index--;
                            if (data->anim_state->current_animation_index < 0) {
                                data->anim_state->current_animation_index = (int)data->mesh->animations.count - 1;
                            }
                            data->anim_state->current_time = 0.0f;
                        } else if (buffer[i] == '2') {
                            data->anim_state->current_animation_index++;
                            if (data->anim_state->current_animation_index >= (int)data->mesh->animations.count) {
                                data->anim_state->current_animation_index = 0;
                            }
                            data->anim_state->current_time = 0.0f;
                        } else if (buffer[i] == 'p') {
                            data->anim_state->playing = !data->anim_state->playing;
                        }
                    }
                }
                
                // Check for focus sequences
                for (ssize_t i = 0; i < n; i++) {
                    if (buffer[i] == '\x1b' && i + 2 < n && buffer[i + 1] == '[') {
                        if (buffer[i + 2] == 'I') {
                            atomic_store(data->is_focused, true);
                        } else if (buffer[i + 2] == 'O') {
                            atomic_store(data->is_focused, false);
                        }
                    }
                }
            }
        }
        
        usleep(1000);
    }
    
    return NULL;
}

static double get_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char* argv[]) {
    Args args = parse_args(argc, argv);
    
    if (!args.model_path) {
        print_usage();
        return 1;
    }
    
    // Validate parameters
    if (args.width > 0 && (args.width <= 0 || args.width > 65535)) {
        fprintf(stderr, "Invalid width: %d (must be 1-65535)\n", args.width);
        return 1;
    }
    if (args.height > 0 && (args.height <= 0 || args.height > 65535)) {
        fprintf(stderr, "Invalid height: %d (must be 1-65535)\n", args.height);
        return 1;
    }
    if (args.target_fps <= 0) {
        fprintf(stderr, "Invalid FPS: %d (must be greater than 0)\n", args.target_fps);
        return 1;
    }
    if (args.model_scale <= 0) {
        fprintf(stderr, "Invalid scale: %f (must be greater than 0)\n", args.model_scale);
        return 1;
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    enable_focus_tracking();
    
    // Calculate render dimensions
    uint32_t width, height;
    calculate_render_dimensions(args.width, args.height, args.use_sixel, args.use_kitty, args.show_status_bar, &width, &height);
    
    // Initialize Vulkan renderer
    VulkanRenderer* renderer = vulkan_renderer_create(width, height);
    if (!renderer || !vulkan_renderer_initialize(renderer)) {
        fprintf(stderr, "Failed to initialize Vulkan renderer. Please ensure your system has Vulkan support.\n");
        disable_focus_tracking();
        return 1;
    }
    
    // Load model
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
    
    // Determine texture paths
    char* final_diffuse_path = args.texture_path ? args.texture_path : material_info.diffuse_path;
    char* final_normal_path = args.normal_map_path ? args.normal_map_path : material_info.normal_path;
    
    // Helper function to load embedded or file-based textures
    Texture diffuse_texture;
    if (final_diffuse_path && final_diffuse_path[0] == '*') {
        // Embedded texture
        const struct aiScene* scene = aiImportFile(args.model_path, 0);
        if (scene && scene->mNumTextures > 0) {
            int tex_index = atoi(final_diffuse_path + 1);
            if (tex_index >= 0 && tex_index < (int)scene->mNumTextures) {
                const struct aiTexture* embedded_tex = scene->mTextures[tex_index];
                if (embedded_tex->mHeight == 0) {
                    texture_from_memory(&diffuse_texture,
                                        (const unsigned char*)embedded_tex->pcData,
                                        embedded_tex->mWidth);
                } else {
                    diffuse_texture.width = embedded_tex->mWidth;
                    diffuse_texture.height = embedded_tex->mHeight;
                    diffuse_texture.data_size = embedded_tex->mWidth * embedded_tex->mHeight * 4;
                    diffuse_texture.data = malloc(diffuse_texture.data_size);
                    for (unsigned int i = 0; i < embedded_tex->mWidth * embedded_tex->mHeight; i++) {
                        diffuse_texture.data[i * 4 + 0] = embedded_tex->pcData[i].r;
                        diffuse_texture.data[i * 4 + 1] = embedded_tex->pcData[i].g;
                        diffuse_texture.data[i * 4 + 2] = embedded_tex->pcData[i].b;
                        diffuse_texture.data[i * 4 + 3] = embedded_tex->pcData[i].a;
                    }
                }
            } else {
                texture_init_default(&diffuse_texture);
            }
        } else {
            texture_init_default(&diffuse_texture);
        }
        aiReleaseImport(scene);
    } else if (final_diffuse_path) {
        texture_from_file(&diffuse_texture, final_diffuse_path);
    } else {
        texture_init_default(&diffuse_texture);
    }
    
    Texture normal_texture;
    if (final_normal_path && final_normal_path[0]) {
        texture_from_file(&normal_texture, final_normal_path);
    } else {
        texture_create_flat_normal_map(&normal_texture);
    }
    
    // Load skydome if specified
    Mesh skydome_mesh;
    Texture skydome_texture;
    bool has_skydome = false;
    if (args.skydome_path) {
        generate_skydome(&skydome_mesh, 100.0f, 32, 16);
        if (texture_from_file(&skydome_texture, args.skydome_path)) {
            vulkan_renderer_set_skydome(renderer, &skydome_mesh, &skydome_texture);
            has_skydome = true;
        } else {
            fprintf(stderr, "Warning: Failed to load skydome texture, skydome will be disabled\n");
            mesh_free(&skydome_mesh);
        }
    }
    
    // Calculate camera setup
    CameraSetup camera_setup;
    calculate_camera_setup(&mesh.vertices, &camera_setup);
    
    // Scale and center the model
    const float TARGET_SIZE = 4.0f;
    float model_scale_factor = 1.0f;
    vec3 model_center;
    glm_vec3_zero(model_center);
    
    if (camera_setup.model_scale > 0.0f) {
        model_scale_factor = (TARGET_SIZE / camera_setup.model_scale) * args.model_scale;
        glm_vec3_copy(camera_setup.target, model_center);
    }
    
    // Calculate camera position
    vec3 camera_offset;
    glm_vec3_sub(camera_setup.position, camera_setup.target, camera_offset);
    glm_vec3_scale(camera_offset, model_scale_factor, camera_offset);
    
    vec3 camera_target;
    glm_vec3_zero(camera_target);
    
    vec3 camera_position;
    glm_vec3_add(camera_target, camera_offset, camera_position);
    
    if (args.camera_distance > 0) {
        vec3 direction;
        glm_vec3_normalize_to(camera_offset, direction);
        glm_vec3_scale(direction, args.camera_distance, camera_offset);
        glm_vec3_add(camera_target, camera_offset, camera_position);
    }
    
    // Constants
    const float MOVE_SPEED_BASE = 0.5f;
    const float ROTATION_SENSITIVITY = 2.0f;
    double target_frame_time = 1.0 / args.target_fps;
    
    float move_speed = MOVE_SPEED_BASE * TARGET_SIZE;
    
    // Initialize input
    KeyState key_state = {0};
    InputManager* input_manager = input_manager_create();
    bool input_devices_ready = input_manager_initialize(input_manager, true);
    
    if (args.fps_controls && !input_devices_ready) {
        fprintf(stderr, "Warning: Could not initialize input devices for FPS controls.\n"
                       "Ensure you have permissions for /dev/input/ (add user to 'input' group).\n"
                       "Falling back to rotation mode.\n");
    }
    
    // Enter alternate screen
    hide_cursor();
    enter_alternate_screen();
    enable_raw_mode();
    
    uint32_t current_width = width;
    uint32_t current_height = height;
    Camera camera;
    camera_init(&camera, current_width, current_height, camera_position, camera_target, 60.0f);
    
    mat4 view, projection;
    camera_view_matrix(&camera, view);
    camera_projection_matrix(&camera, projection);
    
    double last_frame_time = get_time_seconds();
    
    atomic_bool is_focused = true;
    
    // Start input thread
    InputThreadData input_data = {
        &camera, renderer, &anim_state, &mesh, &is_focused, args.fps_controls, has_animations
    };
    pthread_t input_thread;
    pthread_create(&input_thread, NULL, input_thread_func, &input_data);
    
    // Main render loop
    while (atomic_load(&g_running)) {
        // Check for terminal resize
        uint32_t new_width, new_height;
        calculate_render_dimensions(args.width, args.height, args.use_sixel, args.use_kitty, args.show_status_bar, &new_width, &new_height);
        if (new_width != current_width || new_height != current_height) {
            current_width = new_width;
            current_height = new_height;
            vulkan_renderer_resize(renderer, current_width, current_height);
            camera_init(&camera, current_width, current_height, camera.position, camera.target, 90.0f);
            camera_view_matrix(&camera, view);
            camera_projection_matrix(&camera, projection);
        }
        
        double frame_start = get_time_seconds();
        float delta_time = (float)(frame_start - last_frame_time);
        last_frame_time = frame_start;
        
        // Handle input
        if (input_devices_ready && atomic_load(&is_focused)) {
            input_manager_process_events(input_manager, &key_state);
            
            if (key_state.q) {
                atomic_store(&g_running, false);
                break;
            }
            
            if (args.fps_controls) {
                if (key_state.v) move_speed /= (1.0f + delta_time);
                if (key_state.b) move_speed *= (1.0f + delta_time);
                
                float speed = move_speed * delta_time;
                if (key_state.ctrl) speed *= 0.25f;
                
                if (key_state.w) camera_move_forward(&camera, speed);
                if (key_state.s) camera_move_backward(&camera, speed);
                if (key_state.a) camera_move_left(&camera, speed);
                if (key_state.d) camera_move_right(&camera, speed);
                if (key_state.space) camera_move_up(&camera, speed);
                if (key_state.shift) camera_move_down(&camera, speed);
                
                if (key_state.mouse_dx != 0 || key_state.mouse_dy != 0) {
                    float sensitivity = ROTATION_SENSITIVITY * 0.001f;
                    camera_rotate(&camera, key_state.mouse_dx * sensitivity, -key_state.mouse_dy * sensitivity);
                }
                
                float rot_speed = ROTATION_SENSITIVITY * delta_time;
                if (key_state.i) camera_rotate(&camera, 0.0f, rot_speed);
                if (key_state.k) camera_rotate(&camera, 0.0f, -rot_speed);
                if (key_state.j) camera_rotate(&camera, -rot_speed, 0.0f);
                if (key_state.l) camera_rotate(&camera, rot_speed, 0.0f);
            }
            
            camera_view_matrix(&camera, view);
        } else if (!args.fps_controls) {
            camera_view_matrix(&camera, view);
        }
        
        // Calculate model matrix
        mat4 model_matrix;
        glm_mat4_identity(model_matrix);
        glm_mat4_mul(model_matrix, mesh.coordinate_system_transform, model_matrix);
        
        mat4 scale_mat;
        glm_scale_make(scale_mat, (vec3){model_scale_factor, model_scale_factor, model_scale_factor});
        glm_mat4_mul(model_matrix, scale_mat, model_matrix);
        
        mat4 translate_mat;
        vec3 neg_center;
        glm_vec3_negate_to(model_center, neg_center);
        glm_translate_make(translate_mat, neg_center);
        glm_mat4_mul(model_matrix, translate_mat, model_matrix);
        
        mat4 mvp;
        glm_mat4_mul(projection, view, mvp);
        glm_mat4_mul(mvp, model_matrix, mvp);
        
        // Set light direction
        vulkan_renderer_set_light_direction(renderer, (vec3){0.0f, -1.0f, -0.5f});
        
        // Update animation
        const mat4* bone_matrix_ptr = NULL;
        uint32_t bone_count = 0;
        
        if (has_animations) {
            update_animation(&mesh, &anim_state, delta_time, bone_matrices);
            bone_matrix_ptr = (const mat4*)bone_matrices;
            bone_count = (uint32_t)mesh.skeleton.bones.count;
        }
        
        // Render
        const uint8_t* framebuffer = vulkan_renderer_render(
            renderer, &mesh, mvp, model_matrix,
            &diffuse_texture, &normal_texture, !args.no_lighting,
            camera.position, !has_uvs,
            material_info.alpha_mode,
            bone_matrix_ptr, bone_count,
            &view, &projection
        );
        
        // Output to terminal
        if (framebuffer != NULL) {
            if (args.use_kitty) {
                render_kitty_shm(framebuffer, current_width, current_height);
            } else if (args.use_sixel) {
                render_sixel(framebuffer, current_width, current_height);
            } else {
                render_terminal(framebuffer, current_width, current_height);
            }
            
            if (args.show_status_bar) {
                const char* anim_name = "";
                if (has_animations && anim_state.current_animation_index >= 0 &&
                    anim_state.current_animation_index < (int)mesh.animations.count) {
                    anim_name = mesh.animations.data[anim_state.current_animation_index].name;
                }
                draw_status_bar(delta_time > 0 ? 1.0f / delta_time : 0.0f, move_speed, camera.position, anim_name);
            }
        }
        
        // Frame rate limiting
        double frame_end = get_time_seconds();
        double frame_duration = frame_end - frame_start;
        if (frame_duration < target_frame_time) {
            usleep((useconds_t)((target_frame_time - frame_duration) * 1e6));
        }
    }
    
    vulkan_renderer_wait_idle(renderer);
    
    // Cleanup
    pthread_join(input_thread, NULL);
    
    disable_raw_mode();
    exit_alternate_screen();
    show_cursor();
    disable_focus_tracking();
    
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
    
    return 0;
}
