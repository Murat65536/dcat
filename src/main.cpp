#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <poll.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "camera.hpp"
#include "model.hpp"
#include "texture.hpp"
#include "vulkan_renderer.hpp"
#include "terminal.hpp"
#include "input_device.hpp"

struct Args {
    std::string modelPath;
    std::string texturePath;
    std::string normalMapPath;
    int width = -1;
    int height = -1;
    float cameraDistance = -1.0f;
    bool useSixel = false;
    bool useKitty = false;
    bool noLighting = false;
    bool fpsControls = false;
};

static std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running.store(false);
}

void printUsage(const char* programName) {
    std::cout << "dcat - A terminal-based 3D model viewer\n\n"
              << "Usage: " << programName << " [OPTIONS] MODEL\n\n"
              << "Arguments:\n"
              << "  MODEL                    Path to the model file\n\n"
              << "Options:\n"
              << "  -t, --texture PATH       Path to the texture file (defaults to gray)\n"
              << "  -n, --normal-map PATH    Path to normal image file\n"
              << "  -W, --width WIDTH        Renderer width (defaults to terminal width)\n"
              << "  -H, --height HEIGHT      Renderer height (defaults to terminal height)\n"
              << "  --camera-distance DIST   Camera distance from origin\n"
              << "  -S, --sixel              Enable Sixel graphics mode\n"
              << "  -K, --kitty              Enable Kitty graphics protocol mode\n"
              << "  --no-lighting            Disable lighting calculations\n"
              << "  --fps-controls           Enable first-person camera controls\n"
              << "  -h, --help               Print this help message\n";
}

Args parseArgs(int argc, char* argv[]) {
    Args args;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            exit(0);
        } else if (arg == "-t" || arg == "--texture") {
            if (i + 1 < argc) {
                args.texturePath = argv[++i];
            }
        } else if (arg == "-n" || arg == "--normal-map") {
            if (i + 1 < argc) {
                args.normalMapPath = argv[++i];
            }
        } else if (arg == "-W" || arg == "--width") {
            if (i + 1 < argc) {
                args.width = std::stoi(argv[++i]);
            }
        } else if (arg == "-H" || arg == "--height") {
            if (i + 1 < argc) {
                args.height = std::stoi(argv[++i]);
            }
        } else if (arg == "--camera-distance") {
            if (i + 1 < argc) {
                args.cameraDistance = std::stof(argv[++i]);
            }
        } else if (arg == "-S" || arg == "--sixel") {
            args.useSixel = true;
        } else if (arg == "-K" || arg == "--kitty") {
            args.useKitty = true;
        } else if (arg == "--no-lighting") {
            args.noLighting = true;
        } else if (arg == "--fps-controls") {
            args.fpsControls = true;
        } else if (arg[0] != '-') {
            args.modelPath = arg;
        }
    }
    
    return args;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    Args args = parseArgs(argc, argv);
    
    if (args.modelPath.empty()) {
        std::cerr << "Error: No model file specified\n";
        printUsage(argv[0]);
        return 1;
    }
    
    // Set up signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Enable focus tracking
    enableFocusTracking();
    
    // Calculate render dimensions
    auto [width, height] = calculateRenderDimensions(args.width, args.height, args.useSixel, args.useKitty);
    
    // Initialize Vulkan renderer
    VulkanRenderer renderer(width, height);
    if (!renderer.initialize()) {
        std::cerr << "Failed to initialize Vulkan renderer. Please ensure your system has Vulkan support." << std::endl;
        disableFocusTracking();
        return 1;
    }
    
    // Load model
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    if (!loadModel(args.modelPath, vertices, indices)) {
        std::cerr << "Failed to load model: " << args.modelPath << std::endl;
        disableFocusTracking();
        return 1;
    }
    
    // Load textures
    Texture diffuseTexture = args.texturePath.empty() ? Texture() : Texture::fromFile(args.texturePath);
    Texture normalTexture = args.normalMapPath.empty() ? Texture::createFlatNormalMap() : Texture::fromFile(args.normalMapPath);
    
    // Calculate camera setup
    CameraSetup cameraSetup = calculateCameraSetup(vertices);
    
    // Scale and center the model to a fixed size
    const float TARGET_SIZE = 4.0f;
    float modelScaleFactor = 1.0f;
    glm::vec3 modelCenter = glm::vec3(0.0f);

    if (cameraSetup.modelScale > 0.0f) {
        modelScaleFactor = TARGET_SIZE / cameraSetup.modelScale;
        modelCenter = cameraSetup.target;
    }

    // Recalculate camera for the standardized model size (centered at 0,0,0)
    glm::vec3 cameraOffset = (cameraSetup.position - cameraSetup.target) * modelScaleFactor;
    glm::vec3 cameraTarget = glm::vec3(0.0f); 
    glm::vec3 cameraPosition = cameraTarget + cameraOffset;
    
    if (args.cameraDistance > 0) {
        glm::vec3 direction = glm::normalize(cameraOffset);
        cameraPosition = cameraTarget + direction * args.cameraDistance;
    }
    
    // Constants
    constexpr float MODEL_ROTATION_SPEED = 0.6f;
    constexpr float MOVE_SPEED_BASE = 0.5f;
    constexpr float ROTATION_SENSITIVITY = 2.0f;
    constexpr auto TARGET_FRAME_TIME = std::chrono::microseconds(1000000 / 60);
    
    // Normalize movement speed based on target size
    float moveSpeed = MOVE_SPEED_BASE * TARGET_SIZE;
    
    // Initialize input devices for FPS controls
    bool inputDevicesReady = false;
    KeyState keyState;
    if (args.fpsControls) {
        inputDevicesReady = initialize_devices(true);
        if (!inputDevicesReady) {
            std::cerr << "Warning: Could not initialize input devices for FPS controls.\n"
                      << "Ensure you have permissions for /dev/input/ (add user to 'input' group).\n"
                      << "Falling back to rotation mode.\n";
        }
    }
    
    // Enter alternate screen and raw mode
    hideCursor();
    enterAlternateScreen();
    enableRawMode();
    
    uint32_t currentWidth = width;
    uint32_t currentHeight = height;
    Camera camera(currentWidth, currentHeight, cameraPosition, cameraTarget, 60.0f);
    glm::mat4 view = camera.viewMatrix();
    glm::mat4 projection = camera.projectionMatrix();
    
    float accumulatedTime = 0.0f;
    auto lastFrameTime = std::chrono::high_resolution_clock::now();
    
    std::atomic<bool> isFocused{true};
    
    // Input handling thread
    std::thread inputThread([&]() {
        char buffer[64];
        while (g_running.load()) {
            struct pollfd pfd;
            pfd.fd = STDIN_FILENO;
            pfd.events = POLLIN;
            
            int ret = poll(&pfd, 1, 16);  // 16ms timeout
            
            if (ret > 0 && (pfd.revents & POLLIN)) {
                ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
                
                if (n > 0) {
                    // Check for quit key
                    for (ssize_t i = 0; i < n; i++) {
                        if (buffer[i] == 'q' || buffer[i] == 'Q') {
                            g_running.store(false);
                            return;
                        }
                    }
                    
                    // Check for focus sequences
                    for (ssize_t i = 0; i < n - 2; i++) {
                        if (buffer[i] == '\x1b' && buffer[i + 1] == '[') {
                            if (buffer[i + 2] == 'I') {
                                isFocused.store(true);
                            } else if (buffer[i + 2] == 'O') {
                                isFocused.store(false);
                            }
                        }
                    }
                }
            }
        }
    });
    
    // Main render loop
    while (g_running.load()) {
        // Check for terminal resize
        auto [newWidth, newHeight] = calculateRenderDimensions(args.width, args.height, args.useSixel, args.useKitty);
        if (newWidth != currentWidth || newHeight != currentHeight) {
            currentWidth = newWidth;
            currentHeight = newHeight;
            renderer.resize(currentWidth, currentHeight);
            camera = Camera(currentWidth, currentHeight, camera.position, camera.target, 60.0f);
            view = camera.viewMatrix();
            projection = camera.projectionMatrix();
        }
        
        auto frameStart = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(frameStart - lastFrameTime).count();
        lastFrameTime = frameStart;
        
        accumulatedTime += deltaTime;
        
        // Handle FPS controls using Linux Input Subsystem
        if (args.fpsControls && inputDevicesReady && isFocused.load()) {
            process_input_events(keyState);
            
            if (keyState.q) {
                g_running.store(false);
                break;
            }
            
            // Movement
            float speed = moveSpeed * deltaTime;
            if (keyState.ctrl) speed *= 0.25f;
            
            if (keyState.w) camera.moveForward(speed);
            if (keyState.s) camera.moveBackward(speed);
            if (keyState.a) camera.moveLeft(speed);
            if (keyState.d) camera.moveRight(speed);
            if (keyState.space) camera.moveUp(speed);
            if (keyState.shift) camera.moveDown(speed);
            
            // Mouse look
            if (keyState.mouse_dx != 0 || keyState.mouse_dy != 0) {
                float sensitivity = ROTATION_SENSITIVITY * 0.001f;
                camera.rotate(keyState.mouse_dx * sensitivity, -keyState.mouse_dy * sensitivity);
            }
            
            // Keyboard look (IJKL)
            float rotSpeed = ROTATION_SENSITIVITY * deltaTime;
            if (keyState.i) camera.rotate(0.0f, rotSpeed);   // Look up
            if (keyState.k) camera.rotate(0.0f, -rotSpeed);  // Look down
            if (keyState.j) camera.rotate(-rotSpeed, 0.0f);  // Look left
            if (keyState.l) camera.rotate(rotSpeed, 0.0f);   // Look right
            
            view = camera.viewMatrix();
        }
        
        // Calculate model matrix
        glm::mat4 model = glm::mat4(1.0f);
        
        if (!args.fpsControls || !inputDevicesReady) {
            float angle = accumulatedTime * MODEL_ROTATION_SPEED * 0.7f;
            model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f));
        }

        // Apply normalization (Center at origin, then Scale)
        model = glm::scale(model, glm::vec3(modelScaleFactor));
        model = glm::translate(model, -modelCenter);
        
        glm::mat4 mvp = projection * view * model;
        
        // Update light direction based on camera
        glm::vec3 forward = camera.forwardDirection();
        renderer.setLightDirection(-forward);
        
        // Render
        std::vector<uint8_t> framebuffer = renderer.render(
            vertices, indices, mvp, model,
            diffuseTexture, normalTexture, !args.noLighting
        );
        
        // Output to terminal
        if (args.useKitty) {
            renderKittyShm(framebuffer, currentWidth, currentHeight);
        } else if (args.useSixel) {
            renderSixel(framebuffer, currentWidth, currentHeight);
        } else {
            renderTerminal(framebuffer, currentWidth, currentHeight);
        }
        
        // Frame rate limiting
        auto frameEnd = std::chrono::high_resolution_clock::now();
        auto frameDuration = frameEnd - frameStart;
        if (frameDuration < TARGET_FRAME_TIME) {
            std::this_thread::sleep_for(TARGET_FRAME_TIME - frameDuration);
        }
    }
    
    // Cleanup
    inputThread.join();
    
    if (inputDevicesReady) {
        finalize_devices();
    }
    
    disableRawMode();
    exitAlternateScreen();
    showCursor();
    disableFocusTracking();
    
    return 0;
}
