#define VMA_IMPLEMENTATION
#include "vulkan_renderer.hpp"
#include "model.hpp"
#include "texture.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <unistd.h>

VulkanRenderer::VulkanRenderer(uint32_t width, uint32_t height)
    : width_(width)
    , height_(height)
    , normalizedLightDir_(glm::normalize(glm::vec3(0.0f, -1.0f, -0.5f)))  // Light from above and slightly behind
{
}

VulkanRenderer::~VulkanRenderer() {
    cleanup();
}

bool VulkanRenderer::initialize() {
    if (createInstance() && selectPhysicalDevice() && createLogicalDevice() && createAllocator() && createCommandPool() &&
        createDescriptorPool() && createDescriptorSetLayout() && createPipelineLayout() && createRenderPass() &&
        createGraphicsPipeline() && createRenderTargets() && createFramebuffer() && createStagingBuffers() &&
        createUniformBuffers() && createSampler() && createCommandBuffers() && createSyncObjects() && createDescriptorSets()) {
        
        // Initialize readback buffers
        readbackBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
        frameReady_.resize(MAX_FRAMES_IN_FLIGHT, false);
        size_t frameSize = getFrameSize();
        for (auto& buffer : readbackBuffers_) {
            buffer.resize(frameSize);
        }
        
        return true;
    }
    else {
        return false;
    }
}

bool VulkanRenderer::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "dcat";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_4;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

#ifndef NDEBUG
    // Enable validation layers in debug builds
    const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    // Check if validation layers are available
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    bool validationAvailable = false;
    for (const auto& layerProperties : availableLayers) {
        if (strcmp("VK_LAYER_KHRONOS_validation", layerProperties.layerName) == 0) {
            validationAvailable = true;
            break;
        }
    }

    if (validationAvailable) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        std::cerr << "Debug: Vulkan validation layers enabled" << std::endl;
    } else {
        std::cerr << "Debug: Vulkan validation layers requested but not available" << std::endl;
    }
#endif

    if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan instance" << std::endl;
        return false;
    }
    return true;
}

bool VulkanRenderer::selectPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    
    if (deviceCount == 0) {
        std::cerr << "No Vulkan-capable GPU found" << std::endl;
        return false;
    }
    
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
    
    // Find a discrete GPU, or fall back to integrated
    for (const auto& device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
        
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                physicalDevice_ = device;
                graphicsQueueFamily_ = i;
                
                if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    return true;  // Prefer discrete GPU
                }
            }
        }
    }
    
    return physicalDevice_ != VK_NULL_HANDLE;
}

bool VulkanRenderer::createLogicalDevice() {
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsQueueFamily_;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.fillModeNonSolid = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;

    if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
        std::cerr << "Failed to create logical device" << std::endl;
        return false;
    }

    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    return true;
}

bool VulkanRenderer::createAllocator() {
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;
    allocatorInfo.instance = instance_;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_4;
    
    if (vmaCreateAllocator(&allocatorInfo, &allocator_) != VK_SUCCESS) {
        std::cerr << "Failed to create VMA allocator" << std::endl;
        return false;
    }
    return true;
}

bool VulkanRenderer::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsQueueFamily_;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        std::cerr << "Failed to create command pool" << std::endl;
        return false;
    }
    return true;
}

bool VulkanRenderer::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 2 * MAX_FRAMES_IN_FLIGHT;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 2 * MAX_FRAMES_IN_FLIGHT;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[2].descriptorCount = 1 * MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;

    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        std::cerr << "Failed to create descriptor pool" << std::endl;
        return false;
    }
    return true;
}

bool VulkanRenderer::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    
    // Uniform buffer (MVP + model matrices)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    
    // Diffuse texture
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Normal texture
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Fragment uniforms (light direction, enable lighting)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        std::cerr << "Failed to create descriptor set layout" << std::endl;
        return false;
    }
    return true;
}

bool VulkanRenderer::createPipelineLayout() {
    // Push constant range for MVP and model matrices
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        std::cerr << "Failed to create pipeline layout" << std::endl;
        return false;
    }
    return true;
}

bool VulkanRenderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_SRGB;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        std::cerr << "Failed to create render pass" << std::endl;
        return false;
    }
    return true;
}

std::vector<char> VulkanRenderer::readShaderFile(const std::string& filename) {
    if (!shaderDirectory_.empty()) {
        std::string path = shaderDirectory_ + filename;
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (file.is_open()) {
            size_t fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> buffer(fileSize);
            file.seekg(0);
            file.read(buffer.data(), fileSize);
            return buffer;
        }
    }
    
    std::vector<std::string> searchPaths;
    
    // Get executable directory
    std::string exePath;
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        exePath = std::string(buf);
        size_t lastSlash = exePath.rfind('/');
        if (lastSlash != std::string::npos) {
            exePath = exePath.substr(0, lastSlash + 1);
        }
    }

    const char* shaderPathEnv = std::getenv("DCAT_SHADER_PATH");
    if (shaderPathEnv) {
        searchPaths.push_back(std::string(shaderPathEnv) + "/");
    }

    if (!exePath.empty()) {
        searchPaths.push_back(exePath + "shaders/");
    }
    searchPaths.push_back("./shaders/");
    
    searchPaths.push_back("/usr/local/share/dcat/shaders/");
    searchPaths.push_back("/usr/share/dcat/shaders/");
    if (!exePath.empty()) {
        searchPaths.push_back(exePath + "../share/dcat/shaders/");
    }

    for (const auto& basePath : searchPaths) {
        std::string path = basePath + filename;
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        
        if (file.is_open()) {
            size_t fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> buffer(fileSize);
            file.seekg(0);
            file.read(buffer.data(), fileSize);
            
            // Cache the directory for future loads
            shaderDirectory_ = basePath;
            
            return buffer;
        }
    }
    
    std::cerr << "Failed to find shader file: " << filename << std::endl;
    std::cerr << "Searched in:" << std::endl;
    for (const auto& path : searchPaths) {
        std::cerr << "  " << path << std::endl;
    }
    return {};
}

VkShaderModule VulkanRenderer::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        std::cerr << "Failed to create shader module" << std::endl;
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

bool VulkanRenderer::createGraphicsPipeline() {
    auto vertShaderCode = readShaderFile("shader.vert.spv");
    auto fragShaderCode = readShaderFile("shader.frag.spv");
    
    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        return false;
    }

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);
    
    if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
        return false;
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
        vertShaderStageInfo, fragShaderStageInfo
    };

    // Vertex input
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 7> attributeDescriptions{};

    // Position
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, position);

    // Texcoord
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, texcoord);

    // Normal
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, normal);

    // Tangent
    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[3].offset = offsetof(Vertex, tangent);

    // Bitangent
    attributeDescriptions[4].binding = 0;
    attributeDescriptions[4].location = 4;
    attributeDescriptions[4].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[4].offset = offsetof(Vertex, bitangent);

    // Bone IDs
    attributeDescriptions[5].binding = 0;
    attributeDescriptions[5].location = 5;
    attributeDescriptions[5].format = VK_FORMAT_R32G32B32A32_SINT;
    attributeDescriptions[5].offset = offsetof(Vertex, boneIDs);

    // Bone Weights
    attributeDescriptions[6].binding = 0;
    attributeDescriptions[6].location = 6;
    attributeDescriptions[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[6].offset = offsetof(Vertex, boneWeights);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width_);
    viewport.height = static_cast<float>(height_);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width_, height_};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;

    // Create Solid Pipeline
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline_) != VK_SUCCESS) {
        std::cerr << "Failed to create graphics pipeline (solid)" << std::endl;
        vkDestroyShaderModule(device_, fragShaderModule, nullptr);
        vkDestroyShaderModule(device_, vertShaderModule, nullptr);
        return false;
    }

    // Create Wireframe Pipeline
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &wireframePipeline_) != VK_SUCCESS) {
        std::cerr << "Failed to create graphics pipeline (wireframe)" << std::endl;
        vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
        vkDestroyShaderModule(device_, fragShaderModule, nullptr);
        vkDestroyShaderModule(device_, vertShaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device_, fragShaderModule, nullptr);
    vkDestroyShaderModule(device_, vertShaderModule, nullptr);
    return true;
}

void VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                   VmaMemoryUsage memoryUsage, VkBuffer& buffer,
                                   VmaAllocation& allocation) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;

    vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
}

void VulkanRenderer::createImage(uint32_t width, uint32_t height, VkFormat format,
                                  VkImageUsageFlags usage, VmaMemoryUsage memoryUsage,
                                  VkImage& image, VmaAllocation& allocation) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;

    vmaCreateImage(allocator_, &imageInfo, &allocInfo, &image, &allocation, nullptr);
}

VkImageView VulkanRenderer::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    vkCreateImageView(device_, &viewInfo, nullptr, &imageView);
    return imageView;
}

bool VulkanRenderer::createRenderTargets() {
    // Color image
    createImage(width_, height_, VK_FORMAT_R8G8B8A8_SRGB,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY, colorImage_, colorImageAllocation_);
    colorImageView_ = createImageView(colorImage_, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

    // Depth image
    createImage(width_, height_, VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY, depthImage_, depthImageAllocation_);
    depthImageView_ = createImageView(depthImage_, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);

    return true;
}

bool VulkanRenderer::createFramebuffer() {
    std::array<VkImageView, 2> attachments = {colorImageView_, depthImageView_};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass_;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = width_;
    framebufferInfo.height = height_;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &framebuffer_) != VK_SUCCESS) {
        std::cerr << "Failed to create framebuffer" << std::endl;
        return false;
    }
    return true;
}

bool VulkanRenderer::createStagingBuffers() {
    stagingBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
    stagingBufferAllocations_.resize(MAX_FRAMES_IN_FLIGHT);
    mappedDatas_.resize(MAX_FRAMES_IN_FLIGHT);
    
    VkDeviceSize bufferSize = width_ * height_ * 4;  // RGBA

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VMA_MEMORY_USAGE_GPU_TO_CPU, stagingBuffers_[i], stagingBufferAllocations_[i]);

        if (vmaMapMemory(allocator_, stagingBufferAllocations_[i], &mappedDatas_[i]) != VK_SUCCESS) {
            std::cerr << "Failed to map staging buffer memory" << std::endl;
            return false;
        }
    }
    return true;
}

bool VulkanRenderer::createSyncObjects() {
    inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create fence" << std::endl;
            return false;
        }
    }
    return true;
}

bool VulkanRenderer::createCommandBuffers() {
    commandBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)commandBuffers_.size();

    if (vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()) != VK_SUCCESS) {
        std::cerr << "Failed to allocate command buffers" << std::endl;
        return false;
    }
    return true;
}

bool VulkanRenderer::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout_);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets_.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device_, &allocInfo, descriptorSets_.data()) != VK_SUCCESS) {
        std::cerr << "Failed to allocate descriptor sets" << std::endl;
        return false;
    }

    descriptorSetsDirty_.resize(MAX_FRAMES_IN_FLIGHT, true);

    return true;
}

void VulkanRenderer::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;
    
    vkDeviceWaitIdle(device_);
    
    if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, sampler_, nullptr);
    
    for (size_t i = 0; i < stagingBuffers_.size(); i++) {
        if (stagingBuffers_[i] != VK_NULL_HANDLE) {
            vmaUnmapMemory(allocator_, stagingBufferAllocations_[i]);
            vmaDestroyBuffer(allocator_, stagingBuffers_[i], stagingBufferAllocations_[i]);
        }
    }
    
    for (size_t i = 0; i < uniformBuffers_.size(); i++) {
        if (uniformBuffers_[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, uniformBuffers_[i], uniformBufferAllocations_[i]);
        }
    }
    
    for (size_t i = 0; i < fragmentUniformBuffers_.size(); i++) {
        if (fragmentUniformBuffers_[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, fragmentUniformBuffers_[i], fragmentUniformBufferAllocations_[i]);
        }
    }

    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, vertexBuffer_, vertexBufferAllocation_);
    }
    if (indexBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, indexBuffer_, indexBufferAllocation_);
    }

    if (diffuseImage_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, diffuseImageView_, nullptr);
        vmaDestroyImage(allocator_, diffuseImage_, diffuseImageAllocation_);
    }
    if (normalImage_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, normalImageView_, nullptr);
        vmaDestroyImage(allocator_, normalImage_, normalImageAllocation_);
    }

    cleanupRenderTargets();

    for (size_t i = 0; i < inFlightFences_.size(); i++) {
        if (inFlightFences_[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device_, inFlightFences_[i], nullptr);
        }
    }

    vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
    vkDestroyPipeline(device_, wireframePipeline_, nullptr);
    vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    vkDestroyRenderPass(device_, renderPass_, nullptr);

    vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);

    vkDestroyCommandPool(device_, commandPool_, nullptr);
    
    if (allocator_ != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator_);
    }

    vkDestroyDevice(device_, nullptr);
    vkDestroyInstance(instance_, nullptr);
}

bool VulkanRenderer::createUniformBuffers() {
    uniformBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBufferAllocations_.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped_.resize(MAX_FRAMES_IN_FLIGHT);

    fragmentUniformBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
    fragmentUniformBufferAllocations_.resize(MAX_FRAMES_IN_FLIGHT);
    fragmentUniformBuffersMapped_.resize(MAX_FRAMES_IN_FLIGHT);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Vertex uniform buffer
        bufferInfo.size = sizeof(Uniforms);
        VmaAllocationInfo outAllocInfo;
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                           &uniformBuffers_[i], &uniformBufferAllocations_[i],
                           &outAllocInfo) != VK_SUCCESS) {
            std::cerr << "Failed to create uniform buffer" << std::endl;
            return false;
        }
        uniformBuffersMapped_[i] = outAllocInfo.pMappedData;

        // Fragment uniform buffer
        bufferInfo.size = sizeof(FragmentUniforms);
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                           &fragmentUniformBuffers_[i], &fragmentUniformBufferAllocations_[i],
                           &outAllocInfo) != VK_SUCCESS) {
            std::cerr << "Failed to create fragment uniform buffer" << std::endl;
            return false;
        }
        fragmentUniformBuffersMapped_[i] = outAllocInfo.pMappedData;
    }
    return true;
}

bool VulkanRenderer::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
        std::cerr << "Failed to create sampler" << std::endl;
        return false;
    }
    return true;
}

VkCommandBuffer VulkanRenderer::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanRenderer::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit command buffer");
    }
    vkQueueWaitIdle(graphicsQueue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

void VulkanRenderer::transitionImageLayout(VkImage image,
                                            VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    endSingleTimeCommands(commandBuffer);
}

void VulkanRenderer::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    endSingleTimeCommands(commandBuffer);
}

void VulkanRenderer::updateDiffuseTexture(const Texture& texture) {
    if (cachedDiffuseWidth_ == texture.width && 
        cachedDiffuseHeight_ == texture.height && 
        cachedDiffuseDataPtr_ == texture.data.data() && 
        diffuseImage_ != VK_NULL_HANDLE) {
        return;
    }

    if (cachedDiffuseWidth_ != texture.width || cachedDiffuseHeight_ != texture.height || diffuseImage_ == VK_NULL_HANDLE) {
        // Recreate texture
        if (diffuseImageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, diffuseImageView_, nullptr);
        }
        if (diffuseImage_ != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, diffuseImage_, diffuseImageAllocation_);
        }

        createImage(texture.width, texture.height, VK_FORMAT_R8G8B8A8_SRGB,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VMA_MEMORY_USAGE_GPU_ONLY, diffuseImage_, diffuseImageAllocation_);
        diffuseImageView_ = createImageView(diffuseImage_, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
        
        cachedDiffuseWidth_ = texture.width;
        cachedDiffuseHeight_ = texture.height;
        
        std::fill(descriptorSetsDirty_.begin(), descriptorSetsDirty_.end(), true);
    }

    // Texture is already RGBA
    VkDeviceSize imageSize = texture.data.size();
    VkBuffer stagingBuf;
    VmaAllocation stagingBufAlloc;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VMA_MEMORY_USAGE_CPU_TO_GPU,
                 stagingBuf, stagingBufAlloc);

    void* data;
    vmaMapMemory(allocator_, stagingBufAlloc, &data);
    memcpy(data, texture.data.data(), imageSize);
    vmaUnmapMemory(allocator_, stagingBufAlloc);

    transitionImageLayout(diffuseImage_,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(stagingBuf, diffuseImage_, texture.width, texture.height);
    transitionImageLayout(diffuseImage_,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vmaDestroyBuffer(allocator_, stagingBuf, stagingBufAlloc);
    
    cachedDiffuseDataPtr_ = texture.data.data();
}

void VulkanRenderer::updateNormalTexture(const Texture& texture) {
    if (cachedNormalWidth_ == texture.width && 
        cachedNormalHeight_ == texture.height && 
        cachedNormalDataPtr_ == texture.data.data() &&
        normalImage_ != VK_NULL_HANDLE) {
        return;
    }

    if (cachedNormalWidth_ != texture.width || cachedNormalHeight_ != texture.height || normalImage_ == VK_NULL_HANDLE) {
        // Recreate texture
        if (normalImageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, normalImageView_, nullptr);
        }
        if (normalImage_ != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, normalImage_, normalImageAllocation_);
        }

        createImage(texture.width, texture.height, VK_FORMAT_R8G8B8A8_SRGB,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VMA_MEMORY_USAGE_GPU_ONLY, normalImage_, normalImageAllocation_);
        normalImageView_ = createImageView(normalImage_, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
        
        cachedNormalWidth_ = texture.width;
        cachedNormalHeight_ = texture.height;

        std::fill(descriptorSetsDirty_.begin(), descriptorSetsDirty_.end(), true);
    }

    // Texture is already RGBA
    VkDeviceSize imageSize = texture.data.size();
    VkBuffer stagingBuf;
    VmaAllocation stagingBufAlloc;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VMA_MEMORY_USAGE_CPU_TO_GPU,
                 stagingBuf, stagingBufAlloc);

    void* data;
    vmaMapMemory(allocator_, stagingBufAlloc, &data);
    memcpy(data, texture.data.data(), imageSize);
    vmaUnmapMemory(allocator_, stagingBufAlloc);

    transitionImageLayout(normalImage_,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(stagingBuf, normalImage_, texture.width, texture.height);
    transitionImageLayout(normalImage_,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vmaDestroyBuffer(allocator_, stagingBuf, stagingBufAlloc);
    
    cachedNormalDataPtr_ = texture.data.data();
}

void VulkanRenderer::updateVertexBuffer(const std::vector<Vertex>& vertices) {
    if (vertices.empty()) return;
    VkDeviceSize bufferSize = sizeof(Vertex) * vertices.size();
    
    if (cachedVertexCount_ != vertices.size() || vertexBuffer_ == VK_NULL_HANDLE) {
        if (vertexBuffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, vertexBuffer_, vertexBufferAllocation_);
        }
        
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo outAllocInfo;
        vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                       &vertexBuffer_, &vertexBufferAllocation_, &outAllocInfo);
        
        memcpy(outAllocInfo.pMappedData, vertices.data(), bufferSize);
        cachedVertexCount_ = vertices.size();
    } else {
        // Just update the data using persistent mapping
        VmaAllocationInfo allocInfo;
        vmaGetAllocationInfo(allocator_, vertexBufferAllocation_, &allocInfo);
        memcpy(allocInfo.pMappedData, vertices.data(), bufferSize);
    }
}

void VulkanRenderer::updateIndexBuffer(const std::vector<uint32_t>& indices) {
    if (indices.empty()) return;
    VkDeviceSize bufferSize = sizeof(uint32_t) * indices.size();
    
    if (cachedIndexCount_ != indices.size() || indexBuffer_ == VK_NULL_HANDLE) {
        if (indexBuffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, indexBuffer_, indexBufferAllocation_);
        }
        
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo outAllocInfo;
        vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                       &indexBuffer_, &indexBufferAllocation_, &outAllocInfo);
        
        memcpy(outAllocInfo.pMappedData, indices.data(), bufferSize);
        cachedIndexCount_ = indices.size();
    } else {
        // Just update the data using persistent mapping
        VmaAllocationInfo allocInfo;
        vmaGetAllocationInfo(allocator_, indexBufferAllocation_, &allocInfo);
        memcpy(allocInfo.pMappedData, indices.data(), bufferSize);
    }
}

void VulkanRenderer::setLightDirection(const glm::vec3& direction) {
    normalizedLightDir_ = glm::normalize(direction);
}

void VulkanRenderer::setWireframeMode(bool enabled) {
    wireframeMode_ = enabled;
}

void VulkanRenderer::resize(uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(device_);
    
    width_ = width;
    height_ = height;
    
    cleanupRenderTargets();
    createRenderTargets();
    createFramebuffer();
    
    // Recreate staging buffers
    for (size_t i = 0; i < stagingBuffers_.size(); i++) {
        if (stagingBuffers_[i] != VK_NULL_HANDLE) {
            if (mappedDatas_[i]) {
                vmaUnmapMemory(allocator_, stagingBufferAllocations_[i]);
                mappedDatas_[i] = nullptr;
            }
            vmaDestroyBuffer(allocator_, stagingBuffers_[i], stagingBufferAllocations_[i]);
        }
    }
    createStagingBuffers();
    
    // Resize readback buffers
    size_t frameSize = getFrameSize();
    for (auto& buffer : readbackBuffers_) {
        buffer.resize(frameSize);
    }
    
    // Mark all frames as not ready
    std::fill(frameReady_.begin(), frameReady_.end(), false);
}

void VulkanRenderer::cleanupRenderTargets() {
    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }
    if (colorImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, colorImageView_, nullptr);
        colorImageView_ = VK_NULL_HANDLE;
    }
    if (colorImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, colorImage_, colorImageAllocation_);
        colorImage_ = VK_NULL_HANDLE;
    }
    if (depthImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, depthImage_, depthImageAllocation_);
        depthImage_ = VK_NULL_HANDLE;
    }
}

const uint8_t* VulkanRenderer::render(
    const Mesh& mesh,
    const glm::mat4& mvp,
    const glm::mat4& model,
    const Texture& diffuseTexture,
    const Texture& normalTexture,
    bool enableLighting,
    const glm::vec3& cameraPos,
    bool useTriplanarMapping,
    AlphaMode alphaMode,
    const glm::mat4* boneMatrices,
    uint32_t boneCount
) {
    vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

    int prevFrame = (currentFrame_ - 1 + MAX_FRAMES_IN_FLIGHT) % MAX_FRAMES_IN_FLIGHT;
    const uint8_t* result = nullptr;

    if (frameReady_[prevFrame]) {
        vmaInvalidateAllocation(allocator_, stagingBufferAllocations_[currentFrame_], 0, VK_WHOLE_SIZE);
        memcpy(readbackBuffers_[currentFrame_].data(), mappedDatas_[currentFrame_], getFrameSize());
        result = readbackBuffers_[prevFrame].data();
    }

    vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);

    updateDiffuseTexture(diffuseTexture);
    updateNormalTexture(normalTexture);
    
    if (cachedMeshGeneration_ != mesh.generation || vertexBuffer_ == VK_NULL_HANDLE) {
        updateVertexBuffer(mesh.vertices);
        updateIndexBuffer(mesh.indices);
        cachedMeshGeneration_ = mesh.generation;
    }

    if (descriptorSetsDirty_[currentFrame_]) {
        VkDescriptorBufferInfo uniformBufferInfo{};
        uniformBufferInfo.buffer = uniformBuffers_[currentFrame_];
        uniformBufferInfo.offset = 0;
        uniformBufferInfo.range = sizeof(Uniforms);

        VkDescriptorImageInfo diffuseImageInfo{};
        diffuseImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        diffuseImageInfo.imageView = diffuseImageView_;
        diffuseImageInfo.sampler = sampler_;

        VkDescriptorImageInfo normalImageInfo{};
        normalImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normalImageInfo.imageView = normalImageView_;
        normalImageInfo.sampler = sampler_;

        VkDescriptorBufferInfo fragUniformBufferInfo{};
        fragUniformBufferInfo.buffer = fragmentUniformBuffers_[currentFrame_];
        fragUniformBufferInfo.offset = 0;
        fragUniformBufferInfo.range = sizeof(FragmentUniforms);

        std::array<VkWriteDescriptorSet, 4> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets_[currentFrame_];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &uniformBufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets_[currentFrame_];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &diffuseImageInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = descriptorSets_[currentFrame_];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &normalImageInfo;

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = descriptorSets_[currentFrame_];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pBufferInfo = &fragUniformBufferInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
        
        descriptorSetsDirty_[currentFrame_] = false;
    }

    // Prepare push constants for MVP and model matrices
    PushConstants pushConstants{};
    pushConstants.mvp = mvp;
    pushConstants.model = model;

    // Prepare uniform buffer for bone matrices
    Uniforms uniforms{};
    uniforms.hasAnimation = (boneMatrices != nullptr) ? 1 : 0;

    // Copy bone matrices if available
    if (boneMatrices && boneCount > 0) {
        uint32_t numBonesToCopy = std::min(boneCount, static_cast<uint32_t>(MAX_BONES));
        for (uint32_t i = 0; i < numBonesToCopy; i++) {
            uniforms.boneMatrices[i] = boneMatrices[i];
        }
        // Initialize remaining bones to identity
        for (uint32_t i = numBonesToCopy; i < MAX_BONES; i++) {
            uniforms.boneMatrices[i] = glm::mat4(1.0f);
        }
    } else {
        // Initialize all bones to identity for static models
        for (uint32_t i = 0; i < MAX_BONES; i++) {
            uniforms.boneMatrices[i] = glm::mat4(1.0f);
        }
    }

    memcpy(uniformBuffersMapped_[currentFrame_], &uniforms, sizeof(Uniforms));

    FragmentUniforms fragUniforms{};
    fragUniforms.lightDir = normalizedLightDir_;
    fragUniforms.enableLighting = enableLighting ? 1 : 0;
    fragUniforms.cameraPos = cameraPos;
    fragUniforms.fogStart = 5.0f;
    fragUniforms.fogColor = glm::vec3(0.0f, 0.0f, 0.0f);
    fragUniforms.fogEnd = 10.0f;
    fragUniforms.useTriplanarMapping = useTriplanarMapping ? 1 : 0;

    // Handle Alpha Mode
    switch (alphaMode) {
        case AlphaMode::Mask:
            fragUniforms.alphaMode = 1;
            break;
        case AlphaMode::Blend:
            fragUniforms.alphaMode = 2;
            break;
        case AlphaMode::Opaque:
        default:
            fragUniforms.alphaMode = 0;
            break;
    }

    memcpy(fragmentUniformBuffersMapped_[currentFrame_], &fragUniforms, sizeof(FragmentUniforms));
    VkCommandBuffer commandBuffer = commandBuffers_[currentFrame_];
    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = framebuffer_;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {width_, height_};

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                      wireframeMode_ ? wireframePipeline_ : graphicsPipeline_);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width_);
    viewport.height = static_cast<float>(height_);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width_, height_};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);

    // Push constants for MVP and model matrices (updated every frame)
    vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(PushConstants), &pushConstants);

    VkBuffer vertexBuffers[] = {vertexBuffer_};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(mesh.indices.size()), 1, 0, 0, 0);

    vkCmdEndRenderPass(commandBuffer);
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width_, height_, 1};

    vkCmdCopyImageToBuffer(commandBuffer, colorImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           stagingBuffers_[currentFrame_], 1, &region);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }
    frameReady_[currentFrame_] = true;
    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
    return result;
}

void VulkanRenderer::waitIdle() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
}
