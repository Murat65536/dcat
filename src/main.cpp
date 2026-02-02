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
#include <string_view>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>

#include "camera.hpp"
#include "model.hpp"
#include "texture.hpp"
#include "vulkan_renderer.hpp"
#include "terminal.hpp"
#include "input_device.hpp"
#include "skydome.hpp"

struct Args {
    std::string modelPath;
    std::string texturePath;
    std::string normalMapPath;
    std::string skydomePath;
    int width = -1;
    int height = -1;
    float cameraDistance = -1.0f;
    float modelScale = 1.0f;
    int targetFps = 60;
    bool useSixel = false;
    bool useKitty = false;
    bool noLighting = false;
    bool fpsControls = false;
    bool showStatusBar = false;
    bool showHelp = false;
};

struct Option {
    std::string_view shortFlag;
    std::string_view longFlag;
    std::string_view valueName;
    std::string_view description;
    void (*callback)(Args&, const char*);
};

constexpr Option OPTIONS[] = {
    {"-t", "--texture", "PATH", "path to the texture file (defaults to gray)", [](Args& args, const char* v) { args.texturePath = v; }},
    {"-n", "--normal-map", "PATH", "path to normal image file", [](Args& args, const char* v) { args.normalMapPath = v; }},
    {"", "--skydome", "PATH", "path to skydome texture file", [](Args& args, const char* v) { args.skydomePath = v; }},
    {"-W", "--width", "WIDTH", "renderer width (defaults to terminal width)", [](Args& args, const char* v) { args.width = std::stoi(v); }},
    {"-H", "--height", "HEIGHT", "renderer height (defaults to terminal height)", [](Args& args, const char* v) { args.height = std::stoi(v); }},
    {"", "--camera-distance", "DIST", "camera distance from origin", [](Args& args, const char* v) { args.cameraDistance = std::stof(v); }},
    {"", "--model-scale", "SCALE", "scale multiplier for the model (e.g., 0.01 for cm to m)", [](Args& args, const char* v) { args.modelScale = std::stof(v); }},
    {"-f", "--fps", "FPS", "target frames per second (default: 60)", [](Args& args, const char* v) { args.targetFps = std::stoi(v); }},
    {"-S", "--sixel", "", "enable Sixel graphics mode", [](Args& args, const char*) { args.useSixel = true; }},
    {"-K", "--kitty", "", "enable Kitty graphics protocol mode", [](Args& args, const char*) { args.useKitty = true; }},
    {"", "--no-lighting", "", "disable lighting calculations", [](Args& args, const char*) { args.noLighting = true; }},
    {"", "--fps-controls", "", "enable first-person camera controls", [](Args& args, const char*) { args.fpsControls = true; }},
    {"-s", "--status-bar", "", "show status bar", [](Args& args, const char*) { args.showStatusBar = true; }},
    {"-h", "--help", "", "display this help and exit", [](Args& args, const char*) { args.showHelp = true; }}
};

static std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running.store(false);
}

void printUsage() {
    std::cout << "Usage: dcat [OPTION]... [MODEL]\n\n";

    size_t maxLen = 0;
    for (const auto& opt : OPTIONS) {
        size_t len = 0;
        if (!opt.shortFlag.empty()) len += opt.shortFlag.length() + 2; // "-s, "
        else len += 4; // "    "
        len += opt.longFlag.length();
        if (!opt.valueName.empty()) len += opt.valueName.length() + 1; // " VALUE"
        if (len > maxLen) maxLen = len;
    }

    for (const auto& opt : OPTIONS) {
        std::cout << "  ";
        std::string flagPart;
        if (!opt.shortFlag.empty()) {
            flagPart += std::string(opt.shortFlag) + ", ";
        } else {
            flagPart += "    ";
        }
        flagPart += opt.longFlag;
        if (!opt.valueName.empty()) {
            flagPart += " " + std::string(opt.valueName);
        }

        std::cout << std::left << std::setw(maxLen + 2) << flagPart << opt.description << "\n";
    }
    std::cout << "\n";
}

Args parseArgs(int argc, char* argv[]) {
    Args args;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        bool matched = false;

        if (arg[0] == '-') {
            for (const auto& opt : OPTIONS) {
                if (arg == opt.shortFlag || arg == opt.longFlag) {
                    matched = true;
                    if (!opt.valueName.empty()) {
                        if (i + 1 < argc) {
                            opt.callback(args, argv[++i]);
                        } else {
                            std::cerr << "Error: " << arg << " requires a value.\n";
                            exit(1);
                        }
                    } else {
                        opt.callback(args, nullptr);
                    }
                    break;
                }
            }
            if (!matched) {
                std::cerr << "Unknown option: " << arg << "\n";
                printUsage();
                exit(1);
            }
        } else {
            args.modelPath = arg;
        }
    }

    if (args.showHelp) {
        printUsage();
        exit(0);
    }
    
    return args;
}

int main(int argc, char* argv[]) {
    Args args = parseArgs(argc, argv);
    
    if (args.modelPath.empty()) {
        if (!args.showHelp) { // If help wasn't asked for but no model provided
            printUsage();
            return 1;
        }
    }
    
    // Validate input parameters
    if (args.width > 0 && (args.width <= 0 || args.width > 65535)) {
        std::cerr << "Invalid width: " << args.width << " (must be 1-65535)" << std::endl;
        return 1;
    }
    if (args.height > 0 && (args.height <= 0 || args.height > 65535)) {
        std::cerr << "Invalid height: " << args.height << " (must be 1-65535)" << std::endl;
        return 1;
    }
    if (args.targetFps <= 0) {
        std::cerr << "Invalid FPS: " << args.targetFps << " (must be greater than 0)" << std::endl;
        return 1;
    }
    if (args.modelScale <= 0) {
        std::cerr << "Invalid scale: " << args.modelScale << " (must be greater than 0)" << std::endl;
        return 1;
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

    AnimationState animState;
    std::vector<glm::mat4> boneMatrices(MAX_BONES, glm::mat4(1.0f));
    bool hasAnimations = mesh.hasAnimations && !mesh.animations.empty();

    std::string finalDiffusePath = args.texturePath;
    if (finalDiffusePath.empty() && !materialInfo.diffusePath.empty()) {
        finalDiffusePath = materialInfo.diffusePath;
    }

    std::string finalNormalPath = args.normalMapPath;
    if (finalNormalPath.empty() && !materialInfo.normalPath.empty()) {
        finalNormalPath = materialInfo.normalPath;
    }

    // Helper function to load embedded or file-based textures
    auto loadTexture = [&args](const std::string& texturePath) -> Texture {
        if (texturePath.empty()) return Texture();
        
        if (texturePath[0] == '*') {
            // Embedded texture
            Assimp::Importer importer;
            const aiScene* scene = importer.ReadFile(args.modelPath, 0);
            if (scene && scene->mNumTextures > 0) {
                int texIndex = std::stoi(texturePath.substr(1));
                if (texIndex >= 0 && texIndex < static_cast<int>(scene->mNumTextures)) {
                    const aiTexture* embeddedTex = scene->mTextures[texIndex];
                    if (embeddedTex->mHeight == 0) {
                        return Texture::fromMemory(
                            reinterpret_cast<const unsigned char*>(embeddedTex->pcData),
                            embeddedTex->mWidth
                        );
                    } else {
                        Texture tex;
                        tex.width = embeddedTex->mWidth;
                        tex.height = embeddedTex->mHeight;
                        tex.data.resize(embeddedTex->mWidth * embeddedTex->mHeight * 4);
                        for (unsigned int i = 0; i < embeddedTex->mWidth * embeddedTex->mHeight; i++) {
                            tex.data[i * 4 + 0] = embeddedTex->pcData[i].r;
                            tex.data[i * 4 + 1] = embeddedTex->pcData[i].g;
                            tex.data[i * 4 + 2] = embeddedTex->pcData[i].b;
                            tex.data[i * 4 + 3] = embeddedTex->pcData[i].a;
                        }
                        return tex;
                    }
                }
            }
        }
        return Texture::fromFile(texturePath);
    };

    // Load textures
    Texture diffuseTexture = loadTexture(finalDiffusePath);
    Texture normalTexture = finalNormalPath.empty() ? Texture::createFlatNormalMap() : loadTexture(finalNormalPath);
    
    // Load skydome if specified
    Mesh skydomeMesh;
    Texture skydomeTexture;
    if (!args.skydomePath.empty()) {
        skydomeMesh = generateSkydome(100.0f, 32, 16);
        skydomeTexture = Texture::fromFile(args.skydomePath);
        if (skydomeTexture.data.empty()) {
            std::cerr << "Warning: Failed to load skydome texture, skydome will be disabled" << std::endl;
        } else {
            renderer.setSkydome(&skydomeMesh, &skydomeTexture);
        }
    }
    
    // Calculate camera setup
    CameraSetup cameraSetup = calculateCameraSetup(mesh.vertices);
    
    // Scale and center the model to a fixed size
    const float TARGET_SIZE = 4.0f;
    float modelScaleFactor = 1.0f;
    glm::vec3 modelCenter = glm::vec3(0.0f);

    if (cameraSetup.modelScale > 0.0f) {
        modelScaleFactor = (TARGET_SIZE / cameraSetup.modelScale) * args.modelScale;  // Apply user scale
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
    constexpr float MOVE_SPEED_BASE = 0.5f;
    constexpr float ROTATION_SENSITIVITY = 2.0f;
    const auto TARGET_FRAME_TIME = std::chrono::microseconds(1000000 / args.targetFps);
    
    // Normalize movement speed based on target size
    float moveSpeed = MOVE_SPEED_BASE * TARGET_SIZE;
    
    // Initialize input devices
    KeyState keyState;
    KeyState lastKeyState;
    InputManager inputManager;
    bool inputDevicesReady = inputManager.initialize(true);

    if (args.fpsControls && !inputDevicesReady) {
        std::cerr << "Warning: Could not initialize input devices for FPS controls.\n"
                  << "Ensure you have permissions for /dev/input/ (add user to 'input' group).\n"
                  << "Falling back to rotation mode.\n";
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

            int ret = poll(&pfd, 1, 0);

            if (ret > 0 && (pfd.revents & POLLIN)) {
                ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));

                if (n > 0) {
                    for (ssize_t i = 0; i < n; i++) {
                        if (buffer[i] == 'q' || buffer[i] == 'Q') {
                            g_running.store(false);
                            return;
                        }

                        if (buffer[i] == 'm' || buffer[i] == 'M') {
                            renderer.setWireframeMode(!renderer.getWireframeMode());
                        }

                        if (!args.fpsControls) {
                            constexpr static float ROTATION_AMOUNT = M_PI / 8;
                            constexpr static float ZOOM_AMOUNT = 0.25f;
                            if (buffer[i] == 'a' || buffer[i] == 'A') camera.orbit(ROTATION_AMOUNT, 0.0f);
                            if (buffer[i] == 'd' || buffer[i] == 'D') camera.orbit(-ROTATION_AMOUNT, 0.0f);
                            if (buffer[i] == 'w' || buffer[i] == 'W') camera.orbit(0.0f, -ROTATION_AMOUNT);
                            if (buffer[i] == 's' || buffer[i] == 'S') camera.orbit(0.0f, ROTATION_AMOUNT);
                            if (buffer[i] == 'e' || buffer[i] == 'E') camera.zoom(ZOOM_AMOUNT);
                            if (buffer[i] == 'r' || buffer[i] == 'R') camera.zoom(-ZOOM_AMOUNT);
                        }

                        // Animation controls (only if model has animations)
                        if (hasAnimations) {
                            if (buffer[i] == '1') {
                                // Previous animation
                                animState.currentAnimationIndex--;
                                if (animState.currentAnimationIndex < 0) {
                                    animState.currentAnimationIndex = static_cast<int>(mesh.animations.size()) - 1;
                                }
                                animState.currentTime = 0.0f;
                            } else if (buffer[i] == '2') {
                                // Next animation
                                animState.currentAnimationIndex++;
                                if (animState.currentAnimationIndex >= static_cast<int>(mesh.animations.size())) {
                                    animState.currentAnimationIndex = 0;
                                }
                                animState.currentTime = 0.0f;
                            } else if (buffer[i] == 'p') {
                                // Toggle play/pause
                                animState.playing = !animState.playing;
                            }
                        }
                    }

                    // Check for sequences (arrows, focus)
                    for (ssize_t i = 0; i < n; i++) {
                        if (buffer[i] == '\x1b' && i + 2 < n && buffer[i + 1] == '[') {
                            if (buffer[i + 2] == 'I') {
                                isFocused.store(true);
                            } else if (buffer[i + 2] == 'O') {
                                isFocused.store(false);
                            }
                        }
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
        
        // Handle input if devices are ready and window is focused
        if (inputDevicesReady && isFocused.load()) {
            inputManager.processEvents(keyState);
            
            if (keyState.q) {
                g_running.store(false);
                break;
            }

            if (args.fpsControls) {
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
            }
            
            view = camera.viewMatrix();
        } else if (!args.fpsControls) {
            view = camera.viewMatrix();
        }
        
        // Calculate model matrix
        glm::mat4 model = glm::mat4(1.0f);
        model = model * mesh.coordinateSystemTransform;
        model = glm::scale(model, glm::vec3(modelScaleFactor));
        model = glm::translate(model, -modelCenter);
        
        glm::mat4 mvp = projection * view * model;

        // Use fixed light direction instead of camera-relative
        renderer.setLightDirection(glm::vec3(0.0f, -1.0f, -0.5f));  // Light coming from above and slightly behind

        // Update animation
        const glm::mat4* boneMatrixPtr = nullptr;
        uint32_t boneCount = 0;

        if (hasAnimations) {
            updateAnimation(mesh, animState, deltaTime, boneMatrices.data());
            boneMatrixPtr = boneMatrices.data();
            boneCount = static_cast<uint32_t>(mesh.skeleton.bones.size());
        }

        // Render frame and get latest completed framebuffer
        const uint8_t* framebuffer = renderer.render(
            mesh, mvp, model,
            diffuseTexture, normalTexture, !args.noLighting,
            camera.position,
            !hasUVs,
            materialInfo.alphaMode,
            boneMatrixPtr, boneCount,
            &view, &projection
        );
        
        // Output to terminal
        if (framebuffer != nullptr) {
            if (args.useKitty) {
                renderKittyShm(framebuffer, currentWidth, currentHeight);
            } else if (args.useSixel) {
                renderSixel(framebuffer, currentWidth, currentHeight);
            } else {
                renderTerminal(framebuffer, currentWidth, currentHeight);
            }
            
            if (args.showStatusBar) {
                std::string animName = "";
                if (hasAnimations && animState.currentAnimationIndex >= 0 && 
                    animState.currentAnimationIndex < static_cast<int>(mesh.animations.size())) {
                    animName = mesh.animations[animState.currentAnimationIndex].name;
                }
                drawStatusBar(deltaTime > 0 ? 1.0f / deltaTime : 0.0f, moveSpeed, camera.position, animName);
            }
        }
        
        // Frame rate limiting
        auto frameEnd = std::chrono::high_resolution_clock::now();
        auto frameDuration = frameEnd - frameStart;
        if (frameDuration < TARGET_FRAME_TIME) {
            std::this_thread::sleep_for(TARGET_FRAME_TIME - frameDuration);
        }
    }

    renderer.waitIdle();
    
    // Cleanup
    inputThread.join();
    
    disableRawMode();
    exitAlternateScreen();
    showCursor();
    disableFocusTracking();
    
    return 0;
}
