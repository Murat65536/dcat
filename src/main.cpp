#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <vector>
#include <functional>
#include <iomanip>

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
    int targetFps = 60;
    bool useSixel = false;
    bool useKitty = false;
    bool noLighting = false;
    bool fpsControls = false;
    bool showStatusBar = false;
    bool showHelp = false;
};

struct Option {
    std::string shortFlag;
    std::string longFlag;
    std::string valueName;
    std::string description;
    std::function<void(const char*)> callback;
};

static std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running.store(false);
}

std::vector<Option> defineOptions(Args& args) {
    return {
        {"-t", "--texture", "PATH", "path to the texture file (defaults to gray)", [&](const char* v) { args.texturePath = v; }},
        {"-n", "--normal-map", "PATH", "path to normal image file", [&](const char* v) { args.normalMapPath = v; }},
        {"-W", "--width", "WIDTH", "renderer width (defaults to terminal width)", [&](const char* v) { args.width = std::stoi(v); }},
        {"-H", "--height", "HEIGHT", "renderer height (defaults to terminal height)", [&](const char* v) { args.height = std::stoi(v); }},
        {"", "--camera-distance", "DIST", "camera distance from origin", [&](const char* v) { args.cameraDistance = std::stof(v); }},
        {"-f", "--fps", "FPS", "target frames per second (default: 60)", [&](const char* v) { args.targetFps = std::stoi(v); }},
        {"-S", "--sixel", "", "enable Sixel graphics mode", [&](const char*) { args.useSixel = true; }},
        {"-K", "--kitty", "", "enable Kitty graphics protocol mode", [&](const char*) { args.useKitty = true; }},
        {"", "--no-lighting", "", "disable lighting calculations", [&](const char*) { args.noLighting = true; }},
        {"", "--fps-controls", "", "enable first-person camera controls", [&](const char*) { args.fpsControls = true; }},
        {"-s", "--status-bar", "", "show status bar", [&](const char*) { args.showStatusBar = true; }},
        {"-h", "--help", "", "display this help and exit", [&](const char*) { args.showHelp = true; }}
    };
}

void printUsage(const char* programName, const std::vector<Option>& options) {
    std::cout << "Usage: " << programName << " [OPTION]... [MODEL]\n\n";

    size_t maxLen = 0;
    for (const auto& opt : options) {
        size_t len = 0;
        if (!opt.shortFlag.empty()) len += opt.shortFlag.length() + 2; // "-s, "
        else len += 4; // "    "
        len += opt.longFlag.length();
        if (!opt.valueName.empty()) len += opt.valueName.length() + 1; // " VALUE"
        if (len > maxLen) maxLen = len;
    }

    for (const auto& opt : options) {
        std::cout << "  ";
        std::string flagPart;
        if (!opt.shortFlag.empty()) {
            flagPart += opt.shortFlag + ", ";
        } else {
            flagPart += "    ";
        }
        flagPart += opt.longFlag;
        if (!opt.valueName.empty()) {
            flagPart += " " + opt.valueName;
        }

        std::cout << std::left << std::setw(maxLen + 2) << flagPart << opt.description << "\n";
    }
    std::cout << "\n";
}

Args parseArgs(int argc, char* argv[]) {
    Args args;
    auto options = defineOptions(args);
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        bool matched = false;

        if (arg[0] == '-') {
            for (const auto& opt : options) {
                if (arg == opt.shortFlag || arg == opt.longFlag) {
                    matched = true;
                    if (!opt.valueName.empty()) {
                        if (i + 1 < argc) {
                            opt.callback(argv[++i]);
                        } else {
                            std::cerr << "Error: " << arg << " requires a value.\n";
                            exit(1);
                        }
                    } else {
                        opt.callback(nullptr);
                    }
                    break;
                }
            }
            if (!matched) {
                std::cerr << "Unknown option: " << arg << "\n";
                printUsage(argv[0], options);
                exit(1);
            }
        } else {
            args.modelPath = arg;
        }
    }

    if (args.showHelp) {
        printUsage(argv[0], options);
        exit(0);
    }
    
    return args;
}

int main(int argc, char* argv[]) {
    Args args = parseArgs(argc, argv);
    
    if (args.modelPath.empty()) {
        if (!args.showHelp) { // If help wasn't asked for but no model provided
             // Create a temp args just to get options for printing usage
            Args tempArgs;
            printUsage(argv[0], defineOptions(tempArgs));
            return 1;
        }
    }
    
    // Set up signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Enable focus tracking
    enableFocusTracking();
    
    // Calculate render dimensions
    auto [width, height] = calculateRenderDimensions(args.width, args.height, args.useSixel, args.useKitty, args.showStatusBar);
    
    // Initialize Vulkan renderer
    VulkanRenderer renderer(width, height);
    if (!renderer.initialize()) {
        std::cerr << "Failed to initialize Vulkan renderer. Please ensure your system has Vulkan support." << std::endl;
        disableFocusTracking();
        return 1;
    }
    
    // Load model
    Mesh mesh;
    bool hasUVs = false;
    MaterialInfo materialInfo;
    if (!loadModel(args.modelPath, mesh, hasUVs, materialInfo)) {
        std::cerr << "Failed to load model: " << args.modelPath << std::endl;
        disableFocusTracking();
        return 1;
    }
    
    // Resolve textures
    std::string finalDiffusePath = args.texturePath;
    if (finalDiffusePath.empty() && !materialInfo.diffusePath.empty()) {
        finalDiffusePath = materialInfo.diffusePath;
    }

    std::string finalNormalPath = args.normalMapPath;
    if (finalNormalPath.empty() && !materialInfo.normalPath.empty()) {
        finalNormalPath = materialInfo.normalPath;
    }

    // Load textures
    Texture diffuseTexture = finalDiffusePath.empty() ? Texture() : Texture::fromFile(finalDiffusePath);
    Texture normalTexture = finalNormalPath.empty() ? Texture::createFlatNormalMap() : Texture::fromFile(finalNormalPath);
    
    // Calculate camera setup
    CameraSetup cameraSetup = calculateCameraSetup(mesh.vertices);
    
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
    const auto TARGET_FRAME_TIME = std::chrono::microseconds(1000000 / args.targetFps);
    
    // Normalize movement speed based on target size
    float moveSpeed = MOVE_SPEED_BASE * TARGET_SIZE;
    
    // Initialize input devices for FPS controls
    bool inputDevicesReady = false;
    KeyState keyState;
    InputManager inputManager;

    if (args.fpsControls) {
        inputDevicesReady = inputManager.initialize(true);
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
    bool lastMState = false;
    
    // Input handling thread
    std::thread inputThread([&]() {
        char buffer[64];
        while (g_running.load()) {
            struct pollfd pfd;
            pfd.fd = STDIN_FILENO;
            pfd.events = POLLIN;
            
            int ret = poll(&pfd, 1, 0);

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
        auto [newWidth, newHeight] = calculateRenderDimensions(args.width, args.height, args.useSixel, args.useKitty, args.showStatusBar);
        if (newWidth != currentWidth || newHeight != currentHeight) {
            currentWidth = newWidth;
            currentHeight = newHeight;
            renderer.resize(currentWidth, currentHeight);
            camera = Camera(currentWidth, currentHeight, camera.position, camera.target, 90.0f);
            view = camera.viewMatrix();
            projection = camera.projectionMatrix();
        }
        
        auto frameStart = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(frameStart - lastFrameTime).count();
        lastFrameTime = frameStart;
        
        accumulatedTime += deltaTime;
        
        // Handle FPS controls using Linux Input Subsystem
        if (args.fpsControls && inputDevicesReady && isFocused.load()) {
            inputManager.processEvents(keyState);
            
            if (keyState.q) {
                g_running.store(false);
                break;
            }

            if (keyState.m && !lastMState) {
                static bool wireframe = false;
                wireframe = !wireframe;
                renderer.setWireframeMode(wireframe);
            }
            lastMState = keyState.m;

            // Speed Control
            if (keyState.v) moveSpeed /= (1.0f + deltaTime);
            if (keyState.b) moveSpeed *= (1.0f + deltaTime);
            
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

        // Use fixed light direction instead of camera-relative
        renderer.setLightDirection(glm::vec3(0.0f, -1.0f, -0.5f));  // Light coming from above and slightly behind
        
        // Render
        const uint8_t* framebuffer = renderer.render(
            mesh, mvp, model,
            diffuseTexture, normalTexture, !args.noLighting,
            camera.position,
            !hasUVs
        );
        
        // Output to terminal
        if (args.useKitty) {
            renderKittyShm(framebuffer, currentWidth, currentHeight);
        } else if (args.useSixel) {
            renderSixel(framebuffer, currentWidth, currentHeight);
        } else {
            renderTerminal(framebuffer, currentWidth, currentHeight);
        }
        
        if (args.showStatusBar) {
            drawStatusBar(deltaTime > 0 ? 1.0f / deltaTime : 0.0f, moveSpeed, camera.position);
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
    
    disableRawMode();
    exitAlternateScreen();
    showCursor();
    disableFocusTracking();
    
    return 0;
}
