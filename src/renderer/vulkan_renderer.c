#include "vulkan_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Forward declarations of static functions
static bool create_instance(VulkanRenderer* r);
static bool select_physical_device(VulkanRenderer* r);
static bool create_logical_device(VulkanRenderer* r);
static bool create_command_pool(VulkanRenderer* r);
static bool create_descriptor_pool(VulkanRenderer* r);
static bool create_descriptor_set_layout(VulkanRenderer* r);
static bool create_pipeline_layout(VulkanRenderer* r);
static bool create_render_pass(VulkanRenderer* r);
static bool create_graphics_pipeline(VulkanRenderer* r);
static bool create_skydome_pipeline(VulkanRenderer* r);
static bool create_render_targets(VulkanRenderer* r);
static bool create_framebuffer(VulkanRenderer* r);
static bool create_staging_buffers(VulkanRenderer* r);
static bool create_uniform_buffers(VulkanRenderer* r);
static bool create_sampler(VulkanRenderer* r);
static bool create_command_buffers(VulkanRenderer* r);
static bool create_sync_objects(VulkanRenderer* r);
static bool create_descriptor_sets(VulkanRenderer* r);

static void cleanup_render_targets(VulkanRenderer* r);
static void cleanup(VulkanRenderer* r);

// Memory helpers
static uint32_t find_memory_type(VulkanRenderer* r, uint32_t type_filter, VkMemoryPropertyFlags properties);
static bool create_buffer(VulkanRenderer* r, VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties, VkBuffer* buffer, VulkanAllocation* alloc);
static bool create_image(VulkanRenderer* r, uint32_t width, uint32_t height, VkFormat format,
                         VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                         VkImage* image, VulkanAllocation* alloc);
static VkImageView create_image_view(VulkanRenderer* r, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags);
static VkCommandBuffer begin_single_time_commands(VulkanRenderer* r);
static void end_single_time_commands(VulkanRenderer* r, VkCommandBuffer cmd);
static void transition_image_layout(VulkanRenderer* r, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);
static void copy_buffer_to_image(VulkanRenderer* r, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

// Texture update
static void update_diffuse_texture(VulkanRenderer* r, const Texture* texture);
static void update_normal_texture(VulkanRenderer* r, const Texture* texture);
static void update_skydome_texture(VulkanRenderer* r, const Texture* texture);
static void update_vertex_buffer(VulkanRenderer* r, const VertexArray* vertices);
static void update_index_buffer(VulkanRenderer* r, const Uint32Array* indices);

// Shader loading
static char* read_shader_file(VulkanRenderer* r, const char* filename, size_t* out_size);
static VkShaderModule create_shader_module(VulkanRenderer* r, const char* code, size_t size);

VulkanRenderer* vulkan_renderer_create(uint32_t width, uint32_t height) {
    VulkanRenderer* r = calloc(1, sizeof(VulkanRenderer));
    if (!r) return NULL;
    
    r->width = width;
    r->height = height;
    glm_vec3_normalize_to((vec3){0.0f, -1.0f, -0.5f}, r->normalized_light_dir);
    
    return r;
}

void vulkan_renderer_destroy(VulkanRenderer* r) {
    if (!r) return;
    cleanup(r);
    free(r);
}

bool vulkan_renderer_initialize(VulkanRenderer* r) {
    if (!create_instance(r)) return false;
    if (!select_physical_device(r)) return false;
    if (!create_logical_device(r)) return false;
    if (!create_command_pool(r)) return false;
    if (!create_descriptor_pool(r)) return false;
    if (!create_descriptor_set_layout(r)) return false;
    if (!create_pipeline_layout(r)) return false;
    if (!create_render_pass(r)) return false;
    if (!create_graphics_pipeline(r)) return false;
    if (!create_render_targets(r)) return false;
    if (!create_framebuffer(r)) return false;
    if (!create_staging_buffers(r)) return false;
    if (!create_uniform_buffers(r)) return false;
    if (!create_sampler(r)) return false;
    if (!create_command_buffers(r)) return false;
    if (!create_sync_objects(r)) return false;
    if (!create_descriptor_sets(r)) return false;

    create_skydome_pipeline(r);
    
    // Initialize frame tracking
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        r->frame_ready[i] = false;
    }
    
    return true;
}

static uint32_t find_memory_type(VulkanRenderer* r, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    for (uint32_t i = 0; i < r->mem_properties.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && 
            (r->mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    fprintf(stderr, "Failed to find suitable memory type\n");
    return 0;
}

static bool create_buffer(VulkanRenderer* r, VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties, VkBuffer* buffer, VulkanAllocation* alloc) {
    VkBufferCreateInfo buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(r->device, &buffer_info, NULL, buffer) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create buffer\n");
        return false;
    }
    
    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(r->device, *buffer, &mem_req);
    
    VkMemoryAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc_info.allocationSize = mem_req.size;
    alloc_info.memoryTypeIndex = find_memory_type(r, mem_req.memoryTypeBits, properties);
    
    if (vkAllocateMemory(r->device, &alloc_info, NULL, &alloc->memory) != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate buffer memory\n");
        vkDestroyBuffer(r->device, *buffer, NULL);
        return false;
    }
    
    alloc->offset = 0;
    alloc->size = mem_req.size;
    alloc->mapped = NULL;
    
    vkBindBufferMemory(r->device, *buffer, alloc->memory, 0);
    
    // Map if host visible
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        vkMapMemory(r->device, alloc->memory, 0, size, 0, &alloc->mapped);
    }
    
    return true;
}

static bool create_image(VulkanRenderer* r, uint32_t width, uint32_t height, VkFormat format,
                         VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                         VkImage* image, VulkanAllocation* alloc) {
    VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(r->device, &image_info, NULL, image) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create image\n");
        return false;
    }
    
    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(r->device, *image, &mem_req);
    
    VkMemoryAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc_info.allocationSize = mem_req.size;
    alloc_info.memoryTypeIndex = find_memory_type(r, mem_req.memoryTypeBits, properties);
    
    if (vkAllocateMemory(r->device, &alloc_info, NULL, &alloc->memory) != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate image memory\n");
        vkDestroyImage(r->device, *image, NULL);
        return false;
    }
    
    alloc->offset = 0;
    alloc->size = mem_req.size;
    alloc->mapped = NULL;
    
    vkBindImageMemory(r->device, *image, alloc->memory, 0);
    
    return true;
}

static VkImageView create_image_view(VulkanRenderer* r, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags) {
    VkImageViewCreateInfo view_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspect_flags;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    
    VkImageView image_view;
    if (vkCreateImageView(r->device, &view_info, NULL, &image_view) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create image view\n");
        return VK_NULL_HANDLE;
    }
    return image_view;
}

static bool create_instance(VulkanRenderer* r) {
    VkApplicationInfo app_info = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = "dcat";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;
    
    VkInstanceCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create_info.pApplicationInfo = &app_info;
    
#ifndef NDEBUG
    const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation"};
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, NULL);
    VkLayerProperties* available_layers = malloc(layer_count * sizeof(VkLayerProperties));
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers);
    
    bool validation_available = false;
    for (uint32_t i = 0; i < layer_count; i++) {
        if (strcmp("VK_LAYER_KHRONOS_validation", available_layers[i].layerName) == 0) {
            validation_available = true;
            break;
        }
    }
    free(available_layers);
    
    if (validation_available) {
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = validation_layers;
    }
#endif
    
    if (vkCreateInstance(&create_info, NULL, &r->instance) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan instance\n");
        return false;
    }
    return true;
}

static bool select_physical_device(VulkanRenderer* r) {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(r->instance, &device_count, NULL);
    
    if (device_count == 0) {
        fprintf(stderr, "No Vulkan-capable GPU found\n");
        return false;
    }
    
    VkPhysicalDevice* devices = malloc(device_count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(r->instance, &device_count, devices);
    
    for (uint32_t d = 0; d < device_count; d++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[d], &props);
        
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &queue_family_count, NULL);
        VkQueueFamilyProperties* queue_families = malloc(queue_family_count * sizeof(VkQueueFamilyProperties));
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &queue_family_count, queue_families);
        
        for (uint32_t i = 0; i < queue_family_count; i++) {
            if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                // Check for required features
                VkPhysicalDeviceVulkan12Features features12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
                
                VkPhysicalDeviceFeatures2 features2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
                features2.pNext = &features12;
                
                vkGetPhysicalDeviceFeatures2(devices[d], &features2);
                
                if (!features12.shaderInt8 || !features12.storageBuffer8BitAccess || !features12.uniformAndStorageBuffer8BitAccess || !features2.features.fillModeNonSolid) {
                    fprintf(stderr, "Device %d skipped: missing required features (int8: %d, 8bit_storage: %d, 8bit_uniform: %d, wireframe: %d)\n", 
                            d, features12.shaderInt8, features12.storageBuffer8BitAccess, features12.uniformAndStorageBuffer8BitAccess, features2.features.fillModeNonSolid);
                    continue;
                }

                r->physical_device = devices[d];
                r->graphics_queue_family = i;
                if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    free(queue_families);
                    free(devices);
                    vkGetPhysicalDeviceMemoryProperties(r->physical_device, &r->mem_properties);
                    return true;
                }
                break;
            }
        }
        free(queue_families);
    }
    free(devices);
    
    if (r->physical_device != VK_NULL_HANDLE) {
        vkGetPhysicalDeviceMemoryProperties(r->physical_device, &r->mem_properties);
        return true;
    }
    return false;
}

static bool create_logical_device(VulkanRenderer* r) {
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_create_info.queueFamilyIndex = r->graphics_queue_family;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;
    
    // Legacy features
    VkPhysicalDeviceFeatures device_features = {0};
    device_features.fillModeNonSolid = VK_TRUE;

    // Vulkan 1.2 features (replacing the individual structs)
    VkPhysicalDeviceVulkan12Features features12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.shaderInt8 = VK_TRUE;
    features12.storageBuffer8BitAccess = VK_TRUE;
    features12.uniformAndStorageBuffer8BitAccess = VK_TRUE;
    
    VkDeviceCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    create_info.pNext = &features12;
    create_info.pQueueCreateInfos = &queue_create_info;
    create_info.queueCreateInfoCount = 1;
    create_info.pEnabledFeatures = &device_features;
    
    if (vkCreateDevice(r->physical_device, &create_info, NULL, &r->device) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create logical device\n");
        return false;
    }
    
    vkGetDeviceQueue(r->device, r->graphics_queue_family, 0, &r->graphics_queue);
    return true;
}

static bool create_command_pool(VulkanRenderer* r) {
    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = r->graphics_queue_family;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    if (vkCreateCommandPool(r->device, &pool_info, NULL, &r->command_pool) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create command pool\n");
        return false;
    }
    return true;
}

static bool create_descriptor_pool(VulkanRenderer* r) {
    VkDescriptorPoolSize pool_sizes[5] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 * MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 * MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 * MAX_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 * MAX_FRAMES_IN_FLIGHT}
    };
    
    VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.poolSizeCount = 5;
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = 8 * MAX_FRAMES_IN_FLIGHT;
    
    if (vkCreateDescriptorPool(r->device, &pool_info, NULL, &r->descriptor_pool) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create descriptor pool. This might be due to requesting too many descriptors or memory limits.\n");
        return false;
    }
    return true;
}

static bool create_descriptor_set_layout(VulkanRenderer* r) {
    VkDescriptorSetLayoutBinding bindings[4] = {0};
    
    // Uniform buffer
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
    
    // Fragment uniforms
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 4;
    layout_info.pBindings = bindings;
    
    if (vkCreateDescriptorSetLayout(r->device, &layout_info, NULL, &r->descriptor_set_layout) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create descriptor set layout\n");
        return false;
    }
    return true;
}

static bool create_pipeline_layout(VulkanRenderer* r) {
    VkPushConstantRange push_constant_range = {0};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstants);
    
    VkPipelineLayoutCreateInfo pipeline_layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &r->descriptor_set_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_constant_range;
    
    if (vkCreatePipelineLayout(r->device, &pipeline_layout_info, NULL, &r->pipeline_layout) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create pipeline layout\n");
        return false;
    }
    return true;
}

static bool create_render_pass(VulkanRenderer* r) {
    VkAttachmentDescription attachments[2] = {0};
    
    // Color attachment
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    
    // Depth attachment
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    
    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;
    
    VkRenderPassCreateInfo render_pass_info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    render_pass_info.attachmentCount = 2;
    render_pass_info.pAttachments = attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    
    if (vkCreateRenderPass(r->device, &render_pass_info, NULL, &r->render_pass) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create render pass\n");
        return false;
    }
    return true;
}

static char* read_shader_file(VulkanRenderer* r, const char* filename, size_t* out_size) {
    enum { MAX_SHADER_FILENAME_LEN = 255 };
    enum { MAX_SHADER_DIR_LEN = 240 };
    const char* search_paths[] = {
        "",  // Will be replaced with shader_directory if set
        "./shaders/",
        "/usr/local/share/dcat/shaders/",
        "/usr/share/dcat/shaders/"
    };
    
    // Get executable directory
    char exe_path[4096] = {0};
    char exe_dir[4096] = {0};
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        char* last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            size_t dir_len = last_slash - exe_path + 1;
            memcpy(exe_dir, exe_path, dir_len);
            exe_dir[dir_len] = '\0';
        }
    }
    
    // Try shader directory first if set
    if (r->shader_directory[0]) {
        char path[512];
        snprintf(path, sizeof(path), "%s%.*s", r->shader_directory, MAX_SHADER_FILENAME_LEN, filename);
        FILE* f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            *out_size = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* buffer = malloc(*out_size);
            fread(buffer, 1, *out_size, f);
            fclose(f);
            return buffer;
        }
    }
    
    // Try various paths
    for (size_t i = 0; i < (sizeof(search_paths) / sizeof(search_paths[0])); i++) {
        char path[512];
        if (i == 0 && exe_dir[0]) {
            snprintf(path, sizeof(path), "%.*sshaders/%.*s", MAX_SHADER_DIR_LEN, exe_dir, MAX_SHADER_FILENAME_LEN, filename);
        } else {
            snprintf(path, sizeof(path), "%s%.*s", search_paths[i], MAX_SHADER_FILENAME_LEN, filename);
        }
        
        FILE* f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            *out_size = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* buffer = malloc(*out_size);
            fread(buffer, 1, *out_size, f);
            fclose(f);
            
            // Cache shader directory
            char* last_slash = strrchr(path, '/');
            if (last_slash) {
                size_t dir_len = last_slash - path + 1;
                if (dir_len < sizeof(r->shader_directory)) {
                    memcpy(r->shader_directory, path, dir_len);
                    r->shader_directory[dir_len] = '\0';
                }
            }

            return buffer;
        }
    }
    
    fprintf(stderr, "Failed to find shader file: %s\n", filename);
    return NULL;
}

static VkShaderModule create_shader_module(VulkanRenderer* r, const char* code, size_t size) {
    VkShaderModuleCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    create_info.codeSize = size;
    create_info.pCode = (const uint32_t*)code;
    
    VkShaderModule shader_module;
    if (vkCreateShaderModule(r->device, &create_info, NULL, &shader_module) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create shader module\n");
        return VK_NULL_HANDLE;
    }
    return shader_module;
}

static bool create_graphics_pipeline(VulkanRenderer* r) {
    size_t vert_size, frag_size;
    char* vert_code = read_shader_file(r, "shader.vert.spv", &vert_size);
    char* frag_code = read_shader_file(r, "shader.frag.spv", &frag_size);
    
    if (!vert_code || !frag_code) {
        free(vert_code);
        free(frag_code);
        return false;
    }
    
    VkShaderModule vert_module = create_shader_module(r, vert_code, vert_size);
    VkShaderModule frag_module = create_shader_module(r, frag_code, frag_size);
    free(vert_code);
    free(frag_code);
    
    if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
        return false;
    }
    
    VkPipelineShaderStageCreateInfo shader_stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}
    };
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vert_module;
    shader_stages[0].pName = "main";
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = frag_module;
    shader_stages[1].pName = "main";
    
    // Vertex input
    VkVertexInputBindingDescription binding_desc = {0};
    binding_desc.binding = 0;
    binding_desc.stride = sizeof(Vertex);
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription attr_descs[7] = {0};
    // Position
    attr_descs[0].location = 0;
    attr_descs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attr_descs[0].offset = offsetof(Vertex, position);
    // Texcoord
    attr_descs[1].location = 1;
    attr_descs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attr_descs[1].offset = offsetof(Vertex, texcoord);
    // Normal
    attr_descs[2].location = 2;
    attr_descs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attr_descs[2].offset = offsetof(Vertex, normal);
    // Tangent
    attr_descs[3].location = 3;
    attr_descs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    attr_descs[3].offset = offsetof(Vertex, tangent);
    // Bitangent
    attr_descs[4].location = 4;
    attr_descs[4].format = VK_FORMAT_R32G32B32_SFLOAT;
    attr_descs[4].offset = offsetof(Vertex, bitangent);
    // Bone IDs
    attr_descs[5].location = 5;
    attr_descs[5].format = VK_FORMAT_R32G32B32A32_SINT;
    attr_descs[5].offset = offsetof(Vertex, bone_ids);
    // Bone Weights
    attr_descs[6].location = 6;
    attr_descs[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attr_descs[6].offset = offsetof(Vertex, bone_weights);
    
    VkPipelineVertexInputStateCreateInfo vertex_input_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_desc;
    vertex_input_info.vertexAttributeDescriptionCount = 7;
    vertex_input_info.pVertexAttributeDescriptions = attr_descs;
    
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    VkViewport viewport = {0, 0, (float)r->width, (float)r->height, 1, 0};
    VkRect2D scissor = {{0, 0}, {r->width, r->height}};
    
    VkPipelineViewportStateCreateInfo viewport_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;
    
    VkPipelineRasterizationStateCreateInfo rasterizer = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    
    VkPipelineMultisampleStateCreateInfo multisampling = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_GREATER;
    
    VkPipelineColorBlendAttachmentState color_blend_attachment = {0};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo color_blending = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;
    
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;
    
    VkGraphicsPipelineCreateInfo pipeline_info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = r->pipeline_layout;
    pipeline_info.renderPass = r->render_pass;
    pipeline_info.subpass = 0;
    
    // Create solid pipeline
    if (vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &r->graphics_pipeline) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create graphics pipeline\n");
        vkDestroyShaderModule(r->device, vert_module, NULL);
        vkDestroyShaderModule(r->device, frag_module, NULL);
        return false;
    }
    
    // Create wireframe pipeline
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    if (vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &r->wireframe_pipeline) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create wireframe pipeline\n");
        vkDestroyPipeline(r->device, r->graphics_pipeline, NULL);
        vkDestroyShaderModule(r->device, vert_module, NULL);
        vkDestroyShaderModule(r->device, frag_module, NULL);
        return false;
    }
    
    vkDestroyShaderModule(r->device, vert_module, NULL);
    vkDestroyShaderModule(r->device, frag_module, NULL);
    return true;
}

static bool create_skydome_pipeline(VulkanRenderer* r) {
    size_t vert_size, frag_size;
    char* vert_code = read_shader_file(r, "skydome.vert.spv", &vert_size);
    char* frag_code = read_shader_file(r, "skydome.frag.spv", &frag_size);
    
    if (!vert_code || !frag_code) {
        fprintf(stderr, "Warning: Skydome shaders not found\n");
        free(vert_code);
        free(frag_code);
        return false;
    }
    
    VkShaderModule vert_module = create_shader_module(r, vert_code, vert_size);
    VkShaderModule frag_module = create_shader_module(r, frag_code, frag_size);
    free(vert_code);
    free(frag_code);
    
    if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
        return false;
    }
    
    // Create skydome descriptor set layout
    VkDescriptorSetLayoutBinding sampler_binding = {0};
    sampler_binding.binding = 0;
    sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_binding.descriptorCount = 1;
    sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 1;
    layout_info.pBindings = &sampler_binding;
    
    if (vkCreateDescriptorSetLayout(r->device, &layout_info, NULL, &r->skydome_descriptor_set_layout) != VK_SUCCESS) {
        vkDestroyShaderModule(r->device, vert_module, NULL);
        vkDestroyShaderModule(r->device, frag_module, NULL);
        return false;
    }
    
    // Create skydome pipeline layout
    VkPushConstantRange push_constant_range = {0};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(mat4);
    
    VkPipelineLayoutCreateInfo pipeline_layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &r->skydome_descriptor_set_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_constant_range;
    
    if (vkCreatePipelineLayout(r->device, &pipeline_layout_info, NULL, &r->skydome_pipeline_layout) != VK_SUCCESS) {
        vkDestroyShaderModule(r->device, vert_module, NULL);
        vkDestroyShaderModule(r->device, frag_module, NULL);
        return false;
    }
    
    // Setup shader stages
    VkPipelineShaderStageCreateInfo shader_stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}
    };
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vert_module;
    shader_stages[0].pName = "main";
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = frag_module;
    shader_stages[1].pName = "main";
    
    // Vertex input - only position and texcoord
    VkVertexInputBindingDescription binding_desc = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attr_descs[2] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texcoord)}
    };
    
    VkPipelineVertexInputStateCreateInfo vertex_input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding_desc;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions = attr_descs;
    
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    VkViewport viewport = {0, 0, (float)r->width, (float)r->height, 1, 0};
    VkRect2D scissor = {{0, 0}, {r->width, r->height}};
    VkPipelineViewportStateCreateInfo viewport_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;
    
    VkPipelineRasterizationStateCreateInfo rasterizer = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    
    VkPipelineMultisampleStateCreateInfo multisampling = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_FALSE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    
    VkPipelineColorBlendAttachmentState color_blend = {0};
    color_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    VkPipelineColorBlendStateCreateInfo color_blending = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend;
    
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;
    
    VkGraphicsPipelineCreateInfo pipeline_info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = r->skydome_pipeline_layout;
    pipeline_info.renderPass = r->render_pass;
    pipeline_info.subpass = 0;
    
    if (vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &r->skydome_pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(r->device, vert_module, NULL);
        vkDestroyShaderModule(r->device, frag_module, NULL);
        return false;
    }
    
    vkDestroyShaderModule(r->device, vert_module, NULL);
    vkDestroyShaderModule(r->device, frag_module, NULL);
    
    // Allocate skydome descriptor sets
    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        layouts[i] = r->skydome_descriptor_set_layout;
    }
    
    VkDescriptorSetAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = r->descriptor_pool;
    alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    alloc_info.pSetLayouts = layouts;
    
    if (vkAllocateDescriptorSets(r->device, &alloc_info, r->skydome_descriptor_sets) != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate skydome descriptor sets\n");
        return false;
    }
    
    return true;
}

static bool create_render_targets(VulkanRenderer* r) {
    // Color image
    if (!create_image(r, r->width, r->height, VK_FORMAT_R8G8B8A8_UNORM,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->color_image, &r->color_image_alloc)) {
        return false;
    }
    r->color_image_view = create_image_view(r, r->color_image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    
    // Depth image
    if (!create_image(r, r->width, r->height, VK_FORMAT_D32_SFLOAT,
                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->depth_image, &r->depth_image_alloc)) {
        return false;
    }
    r->depth_image_view = create_image_view(r, r->depth_image, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
    
    return true;
}

static bool create_framebuffer(VulkanRenderer* r) {
    VkImageView attachments[2] = {r->color_image_view, r->depth_image_view};
    
    VkFramebufferCreateInfo fb_info = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb_info.renderPass = r->render_pass;
    fb_info.attachmentCount = 2;
    fb_info.pAttachments = attachments;
    fb_info.width = r->width;
    fb_info.height = r->height;
    fb_info.layers = 1;
    
    if (vkCreateFramebuffer(r->device, &fb_info, NULL, &r->framebuffer) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create framebuffer\n");
        return false;
    }
    return true;
}

static bool create_staging_buffers(VulkanRenderer* r) {
    VkDeviceSize buffer_size = r->width * r->height * 4;
    
    for (int i = 0; i < NUM_STAGING_BUFFERS; i++) {
        // Prefer HOST_CACHED for fast CPU reads; fall back to HOST_COHERENT
        if (!create_buffer(r, buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                           &r->staging_buffers[i], &r->staging_buffer_allocs[i])) {
            if (!create_buffer(r, buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               &r->staging_buffers[i], &r->staging_buffer_allocs[i])) {
                return false;
            }
        }
    }
    return true;
}

static bool create_uniform_buffers(VulkanRenderer* r) {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (!create_buffer(r, sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           &r->uniform_buffers[i], &r->uniform_buffer_allocs[i])) {
            return false;
        }
        
        if (!create_buffer(r, sizeof(FragmentUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           &r->fragment_uniform_buffers[i], &r->fragment_uniform_buffer_allocs[i])) {
            return false;
        }
    }
    return true;
}

static bool create_sampler(VulkanRenderer* r) {
    VkSamplerCreateInfo sampler_info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    
    if (vkCreateSampler(r->device, &sampler_info, NULL, &r->sampler) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create sampler\n");
        return false;
    }
    return true;
}

static bool create_command_buffers(VulkanRenderer* r) {
    VkCommandBufferAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.commandPool = r->command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    
    if (vkAllocateCommandBuffers(r->device, &alloc_info, r->command_buffers) != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate command buffers\n");
        return false;
    }
    return true;
}

static bool create_sync_objects(VulkanRenderer* r) {
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateFence(r->device, &fence_info, NULL, &r->in_flight_fences[i]) != VK_SUCCESS) {
            fprintf(stderr, "Failed to create fence\n");
            return false;
        }
    }
    return true;
}

static bool create_descriptor_sets(VulkanRenderer* r) {
    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        layouts[i] = r->descriptor_set_layout;
        r->descriptor_sets_dirty[i] = true;
    }
    
    VkDescriptorSetAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = r->descriptor_pool;
    alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    alloc_info.pSetLayouts = layouts;
    
    if (vkAllocateDescriptorSets(r->device, &alloc_info, r->descriptor_sets) != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate descriptor sets\n");
        return false;
    }
    return true;
}

static VkCommandBuffer begin_single_time_commands(VulkanRenderer* r) {
    VkCommandBufferAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = r->command_pool;
    alloc_info.commandBufferCount = 1;
    
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(r->device, &alloc_info, &cmd);
    
    VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);
    
    return cmd;
}

static void end_single_time_commands(VulkanRenderer* r, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    
    vkQueueSubmit(r->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(r->graphics_queue);
    
    vkFreeCommandBuffers(r->device, r->command_pool, 1, &cmd);
}

static void transition_image_layout(VulkanRenderer* r, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout) {
    VkCommandBuffer cmd = begin_single_time_commands(r);
    
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    
    VkPipelineStageFlags src_stage, dst_stage;
    
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
    
    end_single_time_commands(r, cmd);
}

static void copy_buffer_to_image(VulkanRenderer* r, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer cmd = begin_single_time_commands(r);
    
    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = width;
    region.imageExtent.height = height;
    region.imageExtent.depth = 1;
    
    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    end_single_time_commands(r, cmd);
}

static void update_diffuse_texture(VulkanRenderer* r, const Texture* texture) {
    if (r->cached_diffuse_width == texture->width &&
        r->cached_diffuse_height == texture->height &&
        r->cached_diffuse_data_ptr == texture->data &&
        r->diffuse_image != VK_NULL_HANDLE) {
        return;
    }
    
    if (r->cached_diffuse_width != texture->width || r->cached_diffuse_height != texture->height || r->diffuse_image == VK_NULL_HANDLE) {
        if (r->diffuse_image_view != VK_NULL_HANDLE) {
            vkDestroyImageView(r->device, r->diffuse_image_view, NULL);
        }
        if (r->diffuse_image != VK_NULL_HANDLE) {
            vkDestroyImage(r->device, r->diffuse_image, NULL);
            vkFreeMemory(r->device, r->diffuse_image_alloc.memory, NULL);
        }
        
        create_image(r, texture->width, texture->height, VK_FORMAT_R8G8B8A8_SRGB,
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->diffuse_image, &r->diffuse_image_alloc);
        r->diffuse_image_view = create_image_view(r, r->diffuse_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
        
        r->cached_diffuse_width = texture->width;
        r->cached_diffuse_height = texture->height;
        
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            r->descriptor_sets_dirty[i] = true;
        }
    }
    
    VkDeviceSize image_size = texture->data_size;
    VkBuffer staging_buf;
    VulkanAllocation staging_alloc;
    create_buffer(r, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &staging_buf, &staging_alloc);
    
    memcpy(staging_alloc.mapped, texture->data, image_size);
    
    transition_image_layout(r, r->diffuse_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(r, staging_buf, r->diffuse_image, texture->width, texture->height);
    transition_image_layout(r, r->diffuse_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    vkDestroyBuffer(r->device, staging_buf, NULL);
    vkFreeMemory(r->device, staging_alloc.memory, NULL);
    
    r->cached_diffuse_data_ptr = texture->data;
}

static void update_normal_texture(VulkanRenderer* r, const Texture* texture) {
    if (r->cached_normal_width == texture->width &&
        r->cached_normal_height == texture->height &&
        r->cached_normal_data_ptr == texture->data &&
        r->normal_image != VK_NULL_HANDLE) {
        return;
    }
    
    if (r->cached_normal_width != texture->width || r->cached_normal_height != texture->height || r->normal_image == VK_NULL_HANDLE) {
        if (r->normal_image_view != VK_NULL_HANDLE) {
            vkDestroyImageView(r->device, r->normal_image_view, NULL);
        }
        if (r->normal_image != VK_NULL_HANDLE) {
            vkDestroyImage(r->device, r->normal_image, NULL);
            vkFreeMemory(r->device, r->normal_image_alloc.memory, NULL);
        }
        
        create_image(r, texture->width, texture->height, VK_FORMAT_R8G8B8A8_SRGB,
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->normal_image, &r->normal_image_alloc);
        r->normal_image_view = create_image_view(r, r->normal_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
        
        r->cached_normal_width = texture->width;
        r->cached_normal_height = texture->height;
        
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            r->descriptor_sets_dirty[i] = true;
        }
    }
    
    VkDeviceSize image_size = texture->data_size;
    VkBuffer staging_buf;
    VulkanAllocation staging_alloc;
    create_buffer(r, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &staging_buf, &staging_alloc);
    
    memcpy(staging_alloc.mapped, texture->data, image_size);
    
    transition_image_layout(r, r->normal_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(r, staging_buf, r->normal_image, texture->width, texture->height);
    transition_image_layout(r, r->normal_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    vkDestroyBuffer(r->device, staging_buf, NULL);
    vkFreeMemory(r->device, staging_alloc.memory, NULL);
    
    r->cached_normal_data_ptr = texture->data;
}

static void upload_buffer_via_staging(VulkanRenderer* r, const void* data, VkDeviceSize size,
                                       VkBufferUsageFlagBits usage_bit, VkBuffer* buffer, VulkanAllocation* alloc) {
    VkBuffer staging_buffer;
    VulkanAllocation staging_alloc;
    create_buffer(r, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &staging_buffer, &staging_alloc);
    
    create_buffer(r, size, usage_bit | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, alloc);
    
    memcpy(staging_alloc.mapped, data, size);
    
    VkCommandBuffer cmd = begin_single_time_commands(r);
    VkBufferCopy copy_region = {0, 0, size};
    vkCmdCopyBuffer(cmd, staging_buffer, *buffer, 1, &copy_region);
    end_single_time_commands(r, cmd);
    
    vkDestroyBuffer(r->device, staging_buffer, NULL);
    vkFreeMemory(r->device, staging_alloc.memory, NULL);
}

static void update_vertex_buffer(VulkanRenderer* r, const VertexArray* vertices) {
    if (vertices->count == 0) return;
    VkDeviceSize buffer_size = sizeof(Vertex) * vertices->count;
    
    if (r->cached_vertex_count != vertices->count || r->vertex_buffer == VK_NULL_HANDLE) {
        if (r->vertex_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->vertex_buffer, NULL);
            vkFreeMemory(r->device, r->vertex_buffer_alloc.memory, NULL);
        }
        
        upload_buffer_via_staging(r, vertices->data, buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  &r->vertex_buffer, &r->vertex_buffer_alloc);
        r->cached_vertex_count = vertices->count;
    }
}

static void update_index_buffer(VulkanRenderer* r, const Uint32Array* indices) {
    if (indices->count == 0) return;
    VkDeviceSize buffer_size = sizeof(uint32_t) * indices->count;
    
    if (r->cached_index_count != indices->count || r->index_buffer == VK_NULL_HANDLE) {
        if (r->index_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->index_buffer, NULL);
            vkFreeMemory(r->device, r->index_buffer_alloc.memory, NULL);
        }
        
        upload_buffer_via_staging(r, indices->data, buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  &r->index_buffer, &r->index_buffer_alloc);
        r->cached_index_count = indices->count;
    }
}







static void cleanup_render_targets(VulkanRenderer* r) {
    if (r->framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(r->device, r->framebuffer, NULL);
        r->framebuffer = VK_NULL_HANDLE;
    }
    if (r->color_image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(r->device, r->color_image_view, NULL);
        r->color_image_view = VK_NULL_HANDLE;
    }
    if (r->color_image != VK_NULL_HANDLE) {
        vkDestroyImage(r->device, r->color_image, NULL);
        vkFreeMemory(r->device, r->color_image_alloc.memory, NULL);
        r->color_image = VK_NULL_HANDLE;
    }
    if (r->depth_image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(r->device, r->depth_image_view, NULL);
        r->depth_image_view = VK_NULL_HANDLE;
    }
    if (r->depth_image != VK_NULL_HANDLE) {
        vkDestroyImage(r->device, r->depth_image, NULL);
        vkFreeMemory(r->device, r->depth_image_alloc.memory, NULL);
        r->depth_image = VK_NULL_HANDLE;
    }
}

static void cleanup(VulkanRenderer* r) {
    if (r->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(r->device);
    }
    
    if (r->device != VK_NULL_HANDLE) {
        if (r->sampler != VK_NULL_HANDLE) {
            vkDestroySampler(r->device, r->sampler, NULL);
        }
        
        for (int i = 0; i < NUM_STAGING_BUFFERS; i++) {
            if (r->staging_buffers[i] != VK_NULL_HANDLE) {
                vkDestroyBuffer(r->device, r->staging_buffers[i], NULL);
                if (r->staging_buffer_allocs[i].memory != VK_NULL_HANDLE) {
                    vkFreeMemory(r->device, r->staging_buffer_allocs[i].memory, NULL);
                }
            }
        }
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (r->uniform_buffers[i] != VK_NULL_HANDLE) {
                vkDestroyBuffer(r->device, r->uniform_buffers[i], NULL);
                if (r->uniform_buffer_allocs[i].memory != VK_NULL_HANDLE) {
                    vkFreeMemory(r->device, r->uniform_buffer_allocs[i].memory, NULL);
                }
            }
            if (r->fragment_uniform_buffers[i] != VK_NULL_HANDLE) {
                vkDestroyBuffer(r->device, r->fragment_uniform_buffers[i], NULL);
                if (r->fragment_uniform_buffer_allocs[i].memory != VK_NULL_HANDLE) {
                    vkFreeMemory(r->device, r->fragment_uniform_buffer_allocs[i].memory, NULL);
                }
            }
            if (r->in_flight_fences[i] != VK_NULL_HANDLE) {
                vkDestroyFence(r->device, r->in_flight_fences[i], NULL);
            }
        }

        if (r->vertex_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->vertex_buffer, NULL);
            if (r->vertex_buffer_alloc.memory != VK_NULL_HANDLE) {
                vkFreeMemory(r->device, r->vertex_buffer_alloc.memory, NULL);
            }
        }
        if (r->index_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->index_buffer, NULL);
            if (r->index_buffer_alloc.memory != VK_NULL_HANDLE) {
                vkFreeMemory(r->device, r->index_buffer_alloc.memory, NULL);
            }
        }
        if (r->skydome_vertex_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->skydome_vertex_buffer, NULL);
            if (r->skydome_vertex_buffer_alloc.memory != VK_NULL_HANDLE) {
                vkFreeMemory(r->device, r->skydome_vertex_buffer_alloc.memory, NULL);
            }
        }
        if (r->skydome_index_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->skydome_index_buffer, NULL);
            if (r->skydome_index_buffer_alloc.memory != VK_NULL_HANDLE) {
                vkFreeMemory(r->device, r->skydome_index_buffer_alloc.memory, NULL);
            }
        }
        
        if (r->diffuse_image != VK_NULL_HANDLE) {
            if (r->diffuse_image_view != VK_NULL_HANDLE) {
                vkDestroyImageView(r->device, r->diffuse_image_view, NULL);
            }
            vkDestroyImage(r->device, r->diffuse_image, NULL);
            if (r->diffuse_image_alloc.memory != VK_NULL_HANDLE) {
                vkFreeMemory(r->device, r->diffuse_image_alloc.memory, NULL);
            }
        }
        if (r->normal_image != VK_NULL_HANDLE) {
            if (r->normal_image_view != VK_NULL_HANDLE) {
                vkDestroyImageView(r->device, r->normal_image_view, NULL);
            }
            vkDestroyImage(r->device, r->normal_image, NULL);
            if (r->normal_image_alloc.memory != VK_NULL_HANDLE) {
                vkFreeMemory(r->device, r->normal_image_alloc.memory, NULL);
            }
        }
        if (r->skydome_image != VK_NULL_HANDLE) {
            if (r->skydome_image_view != VK_NULL_HANDLE) {
                vkDestroyImageView(r->device, r->skydome_image_view, NULL);
            }
            vkDestroyImage(r->device, r->skydome_image, NULL);
            if (r->skydome_image_alloc.memory != VK_NULL_HANDLE) {
                vkFreeMemory(r->device, r->skydome_image_alloc.memory, NULL);
            }
        }
    }
    
    cleanup_render_targets(r);
    
    if (r->device != VK_NULL_HANDLE) {
        if (r->graphics_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(r->device, r->graphics_pipeline, NULL);
        }
        if (r->wireframe_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(r->device, r->wireframe_pipeline, NULL);
        }
        if (r->pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(r->device, r->pipeline_layout, NULL);
        }
        
        if (r->skydome_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(r->device, r->skydome_pipeline, NULL);
        }
        if (r->skydome_pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(r->device, r->skydome_pipeline_layout, NULL);
        }
        if (r->skydome_descriptor_set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(r->device, r->skydome_descriptor_set_layout, NULL);
        }
        
        if (r->render_pass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(r->device, r->render_pass, NULL);
        }
        if (r->descriptor_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(r->device, r->descriptor_pool, NULL);
        }
        if (r->descriptor_set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(r->device, r->descriptor_set_layout, NULL);
        }
        if (r->command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(r->device, r->command_pool, NULL);
        }
        
        vkDestroyDevice(r->device, NULL);
    }
    
    if (r->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(r->instance, NULL);
    }
}

void vulkan_renderer_set_light_direction(VulkanRenderer* r, const float* direction) {
    glm_vec3_normalize_to((float*)direction, r->normalized_light_dir);
}

void vulkan_renderer_set_wireframe_mode(VulkanRenderer* r, bool enabled) {
    atomic_store(&r->wireframe_mode, enabled);
}

bool vulkan_renderer_get_wireframe_mode(const VulkanRenderer* r) {
    return atomic_load(&r->wireframe_mode);
}

void vulkan_renderer_resize(VulkanRenderer* r, uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(r->device);
    
    r->width = width;
    r->height = height;
    
    cleanup_render_targets(r);
    create_render_targets(r);
    create_framebuffer(r);
    
    // Recreate staging and terminal output buffers
    for (int i = 0; i < NUM_STAGING_BUFFERS; i++) {
        if (r->staging_buffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->staging_buffers[i], NULL);
            vkFreeMemory(r->device, r->staging_buffer_allocs[i].memory, NULL);
        }
    }
    create_staging_buffers(r);
    
    // Reset frame tracking
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        r->frame_ready[i] = false;
    }
}

void vulkan_renderer_wait_idle(VulkanRenderer* r) {
    if (r && r->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(r->device);
    }
}

static void update_skydome_texture(VulkanRenderer* r, const Texture* texture) {
    if (!texture->data || texture->data_size == 0) return;
    
    if (r->cached_skydome_data_ptr == texture->data && r->skydome_image != VK_NULL_HANDLE) {
        return;
    }
    
    if (r->skydome_image != VK_NULL_HANDLE) {
        vkDestroyImageView(r->device, r->skydome_image_view, NULL);
        vkDestroyImage(r->device, r->skydome_image, NULL);
        vkFreeMemory(r->device, r->skydome_image_alloc.memory, NULL);
    }
    
    create_image(r, texture->width, texture->height, VK_FORMAT_R8G8B8A8_UNORM,
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->skydome_image, &r->skydome_image_alloc);
    r->skydome_image_view = create_image_view(r, r->skydome_image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    
    VkDeviceSize image_size = texture->data_size;
    VkBuffer staging_buf;
    VulkanAllocation staging_alloc;
    create_buffer(r, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &staging_buf, &staging_alloc);
    
    memcpy(staging_alloc.mapped, texture->data, image_size);
    
    transition_image_layout(r, r->skydome_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(r, staging_buf, r->skydome_image, texture->width, texture->height);
    transition_image_layout(r, r->skydome_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    vkDestroyBuffer(r->device, staging_buf, NULL);
    vkFreeMemory(r->device, staging_alloc.memory, NULL);
    
    r->cached_skydome_data_ptr = texture->data;
    
    // Update descriptor sets
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo image_info = {0};
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_info.imageView = r->skydome_image_view;
        image_info.sampler = r->sampler;
        
        VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = r->skydome_descriptor_sets[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &image_info;
        
        vkUpdateDescriptorSets(r->device, 1, &write, 0, NULL);
    }
}

void vulkan_renderer_set_skydome(VulkanRenderer* r, const Mesh* mesh, const Texture* texture) {
    r->skydome_mesh = mesh;
    r->skydome_texture = texture;
    
    if (mesh && mesh->vertices.count > 0 && mesh->indices.count > 0) {
        // Clean up old buffers
        if (r->skydome_vertex_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->skydome_vertex_buffer, NULL);
            vkFreeMemory(r->device, r->skydome_vertex_buffer_alloc.memory, NULL);
        }
        if (r->skydome_index_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->skydome_index_buffer, NULL);
            vkFreeMemory(r->device, r->skydome_index_buffer_alloc.memory, NULL);
        }
        
        VkDeviceSize vertex_size = sizeof(Vertex) * mesh->vertices.count;
        VkDeviceSize index_size = sizeof(uint32_t) * mesh->indices.count;
        
        upload_buffer_via_staging(r, mesh->vertices.data, vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  &r->skydome_vertex_buffer, &r->skydome_vertex_buffer_alloc);
        
        upload_buffer_via_staging(r, mesh->indices.data, index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  &r->skydome_index_buffer, &r->skydome_index_buffer_alloc);
    }
    
    if (texture && texture->data_size > 0) {
        update_skydome_texture(r, texture);
    }
}

const uint8_t* vulkan_renderer_render(
    VulkanRenderer* r,
    const Mesh* mesh,
    const mat4 mvp,
    const mat4 model,
    const Texture* diffuse_texture,
    const Texture* normal_texture,
    bool enable_lighting,
    const vec3 camera_pos,
    bool use_triplanar_mapping,
    AlphaMode alpha_mode,
    const mat4* bone_matrices,
    uint32_t bone_count,
    const mat4* view,
    const mat4* projection
) {
    vkWaitForFences(r->device, 1, &r->in_flight_fences[r->current_frame], VK_TRUE, UINT64_MAX);
    
    // Read framebuffer from the frame that just completed
    const uint8_t* result = NULL;
    uint32_t ready_staging_idx = r->frame_staging_buffers[r->current_frame];
    
    if (r->frame_ready[r->current_frame]) {
        // Invalidate CPU cache before reading from GPU-written staging buffer
        VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = r->staging_buffer_allocs[ready_staging_idx].memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        vkInvalidateMappedMemoryRanges(r->device, 1, &range);
        result = (const uint8_t*)r->staging_buffer_allocs[ready_staging_idx].mapped;
    }
    
    vkResetFences(r->device, 1, &r->in_flight_fences[r->current_frame]);
    
    r->current_staging_buffer = (r->current_staging_buffer + 1) % NUM_STAGING_BUFFERS;
    uint32_t write_staging_idx = r->current_staging_buffer;
    r->frame_staging_buffers[r->current_frame] = write_staging_idx;
    
    update_diffuse_texture(r, diffuse_texture);
    update_normal_texture(r, normal_texture);
    
    if (r->cached_mesh_generation != mesh->generation || r->vertex_buffer == VK_NULL_HANDLE) {
        update_vertex_buffer(r, &mesh->vertices);
        update_index_buffer(r, &mesh->indices);
        r->cached_mesh_generation = mesh->generation;
    }
    
    if (r->descriptor_sets_dirty[r->current_frame]) {
        VkDescriptorBufferInfo uniform_info = {r->uniform_buffers[r->current_frame], 0, sizeof(Uniforms)};
        VkDescriptorImageInfo diffuse_info = {r->sampler, r->diffuse_image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo normal_info = {r->sampler, r->normal_image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo frag_uniform_info = {r->fragment_uniform_buffers[r->current_frame], 0, sizeof(FragmentUniforms)};
        
        VkWriteDescriptorSet writes[4] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptor_sets[r->current_frame], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &uniform_info, NULL},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptor_sets[r->current_frame], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &diffuse_info, NULL, NULL},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptor_sets[r->current_frame], 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &normal_info, NULL, NULL},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptor_sets[r->current_frame], 3, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &frag_uniform_info, NULL}
        };
        
        vkUpdateDescriptorSets(r->device, 4, writes, 0, NULL);
        r->descriptor_sets_dirty[r->current_frame] = false;
    }
    
    // Prepare push constants
    PushConstants push_constants;
    glm_mat4_copy((float(*)[4])mvp, push_constants.mvp);
    glm_mat4_copy((float(*)[4])model, push_constants.model);
    
    // Prepare uniform buffer
    Uniforms uniforms = {0};
    uniforms.has_animation = (bone_matrices != NULL) ? 1 : 0;
    
    if (bone_matrices && bone_count > 0) {
        uint32_t num_bones = bone_count < MAX_BONES ? bone_count : MAX_BONES;
        memcpy(uniforms.bone_matrices, bone_matrices, num_bones * sizeof(mat4));
        for (uint32_t i = num_bones; i < MAX_BONES; i++) {
            glm_mat4_identity(uniforms.bone_matrices[i]);
        }
    } else {
        for (uint32_t i = 0; i < MAX_BONES; i++) {
            glm_mat4_identity(uniforms.bone_matrices[i]);
        }
    }
    
    memcpy(r->uniform_buffer_allocs[r->current_frame].mapped, &uniforms, sizeof(Uniforms));
    
    // Prepare fragment uniforms
    FragmentUniforms frag_uniforms = {0};
    glm_vec3_copy(r->normalized_light_dir, frag_uniforms.light_dir);
    frag_uniforms.enable_lighting = enable_lighting ? 1 : 0;
    glm_vec3_copy((float*)camera_pos, frag_uniforms.camera_pos);
    frag_uniforms.fog_start = 5.0f;
    glm_vec3_zero(frag_uniforms.fog_color);
    frag_uniforms.fog_end = 10.0f;
    frag_uniforms.use_triplanar_mapping = use_triplanar_mapping ? 1 : 0;
    frag_uniforms.alpha_cutoff = 0.5f;
    
    switch (alpha_mode) {
        case ALPHA_MODE_MASK: frag_uniforms.alpha_mode = 1; break;
        case ALPHA_MODE_BLEND: frag_uniforms.alpha_mode = 2; break;
        default: frag_uniforms.alpha_mode = 0; break;
    }
    
    memcpy(r->fragment_uniform_buffer_allocs[r->current_frame].mapped, &frag_uniforms, sizeof(FragmentUniforms));
    
    VkCommandBuffer cmd = r->command_buffers[r->current_frame];
    vkResetCommandBuffer(cmd, 0);
    
    VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &begin_info);
    
    VkClearValue clear_values[2] = {{{{0, 0, 0, 1}}}, {{{0.0f, 0}}}};
    
    VkRenderPassBeginInfo rp_info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp_info.renderPass = r->render_pass;
    rp_info.framebuffer = r->framebuffer;
    rp_info.renderArea.extent.width = r->width;
    rp_info.renderArea.extent.height = r->height;
    rp_info.clearValueCount = 2;
    rp_info.pClearValues = clear_values;
    
    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);
    
    VkViewport viewport = {0, 0, (float)r->width, (float)r->height, 1, 0};
    VkRect2D scissor = {{0, 0}, {r->width, r->height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    
    // Render skydome first
    if (r->skydome_mesh && r->skydome_texture && r->skydome_pipeline != VK_NULL_HANDLE &&
        r->skydome_vertex_buffer != VK_NULL_HANDLE && r->skydome_index_buffer != VK_NULL_HANDLE &&
        view != NULL && projection != NULL) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->skydome_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->skydome_pipeline_layout, 0, 1, &r->skydome_descriptor_sets[r->current_frame], 0, NULL);
        
        // Remove translation from view matrix
        mat4 sky_view;
        glm_mat4_copy((float(*)[4])*view, sky_view);
        sky_view[3][0] = sky_view[3][1] = sky_view[3][2] = 0.0f;
        
        mat4 sky_mvp;
        glm_mat4_mul((float(*)[4])*projection, sky_view, sky_mvp);
        vkCmdPushConstants(cmd, r->skydome_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), sky_mvp);
        
        VkBuffer vb[] = {r->skydome_vertex_buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, r->skydome_index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, (uint32_t)r->skydome_mesh->indices.count, 1, 0, 0, 0);
    }
    
    // Render main model
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, atomic_load(&r->wireframe_mode) ? r->wireframe_pipeline : r->graphics_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1, &r->descriptor_sets[r->current_frame], 0, NULL);
    vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push_constants);
    
    VkBuffer vbs[] = {r->vertex_buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offsets);
    vkCmdBindIndexBuffer(cmd, r->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, (uint32_t)mesh->indices.count, 1, 0, 0, 0);
    
    vkCmdEndRenderPass(cmd);
    
    // Copy color image to staging buffer for CPU readback
    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = r->width;
    region.imageExtent.height = r->height;
    region.imageExtent.depth = 1;
    
    vkCmdCopyImageToBuffer(cmd, r->color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, r->staging_buffers[write_staging_idx], 1, &region);
    
    VkBufferMemoryBarrier buffer_barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.buffer = r->staging_buffers[write_staging_idx];
    buffer_barrier.size = VK_WHOLE_SIZE;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &buffer_barrier, 0, NULL);
    
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    
    vkQueueSubmit(r->graphics_queue, 1, &submit_info, r->in_flight_fences[r->current_frame]);
    
    r->frame_ready[r->current_frame] = true;
    r->current_frame = (r->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
    
    return result;
}
