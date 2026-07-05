#include "vk_device.h"
#include "vulkan/vulkan_core.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NDEBUG
void vk_set_object_name(VulkanRenderer *r, VkObjectType type, uint64_t handle, const char *fmt,
                        ...) {
    if (r->pfn_set_object_name == NULL || handle == 0) {
        return;
    }

    char name[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(name, sizeof(name), fmt, args);
    va_end(args);

    VkDebugUtilsObjectNameInfoEXT info = {.sType =
                                              VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    r->pfn_set_object_name(r->device, &info);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data) {
    (void)user_data;

    const char *type_text;
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
        type_text = "GENERAL";
    } else if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        type_text = "VALIDATION";
    } else if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        type_text = "PERFORMANCE";
    } else {
        type_text = "UNKNOWN";
    }

    const char *severity_text;
    const char *color;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        color = "\x1b[91m";
        severity_text = "ERROR";
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        color = "\x1b[93m";
        severity_text = "WARNING";
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        color = "\x1b[96m";
        severity_text = "INFO";
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        color = "\x1b[90m";
        severity_text = "VERBOSE";
    } else {
        color = "";
        severity_text = "UNKNOWN";
    }

    fprintf(stderr, "[VULKAN:%s:%s%s\x1b[0m] %s\n", type_text, color, severity_text,
            callback_data->pMessage);
    return VK_FALSE;
}

static void populate_debug_messenger_info(VkDebugUtilsMessengerCreateInfoEXT *info) {
    info->sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info->messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    info->messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info->pfnUserCallback = debug_callback;
}
#endif

bool create_instance(VulkanRenderer *r) {
    VkApplicationInfo app_info = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = "dcat";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = 0;
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create_info.pApplicationInfo = &app_info;

#ifndef NDEBUG
    const char *validation_layers[] = {"VK_LAYER_KHRONOS_validation"};
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, NULL);
    VkLayerProperties *available_layers = malloc(layer_count * sizeof(VkLayerProperties));
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers);

    uint32_t validation_layer_count = sizeof(validation_layers) / sizeof(validation_layers[0]);
    bool validation_available = true;
    for (uint32_t v = 0; v < validation_layer_count; v++) {
        bool found = false;
        for (uint32_t i = 0; i < layer_count; i++) {
            if (strcmp(validation_layers[v], available_layers[i].layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            validation_available = false;
            break;
        }
    }
    free(available_layers);

    if (validation_available) {
        create_info.enabledLayerCount = validation_layer_count;
        create_info.ppEnabledLayerNames = validation_layers;
    }

    uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &ext_count, NULL);
    VkExtensionProperties *available_exts = malloc(ext_count * sizeof(VkExtensionProperties));
    vkEnumerateInstanceExtensionProperties(NULL, &ext_count, available_exts);

    bool debug_utils_available = false;
    for (uint32_t i = 0; i < ext_count; i++) {
        if (strcmp(available_exts[i].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
            debug_utils_available = true;
            break;
        }
    }
    free(available_exts);

    const char *extensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    VkDebugUtilsMessengerCreateInfoEXT debug_info = {0};
    if (debug_utils_available) {
        create_info.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]);
        create_info.ppEnabledExtensionNames = extensions;
        populate_debug_messenger_info(&debug_info);
        create_info.pNext = &debug_info;
    }
#endif

    if (vkCreateInstance(&create_info, NULL, &r->instance) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan instance\n");
        return false;
    }

#ifndef NDEBUG
    if (debug_utils_available) {
        PFN_vkCreateDebugUtilsMessengerEXT create_messenger =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                r->instance, "vkCreateDebugUtilsMessengerEXT");
        if (create_messenger != NULL) {
            create_messenger(r->instance, &debug_info, NULL, &r->debug_messenger);
        }
    }
#endif
    return true;
}

bool select_physical_device(VulkanRenderer *r) {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(r->instance, &device_count, NULL);

    if (device_count == 0) {
        fprintf(stderr, "No Vulkan-capable GPU found\n");
        return false;
    }

    VkPhysicalDevice *devices = (VkPhysicalDevice *)malloc(device_count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(r->instance, &device_count, devices);

    for (uint32_t d = 0; d < device_count; d++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[d], &props);

        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &queue_family_count, NULL);
        VkQueueFamilyProperties *queue_families =
            malloc(queue_family_count * sizeof(VkQueueFamilyProperties));
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &queue_family_count, queue_families);

        for (uint32_t i = 0; i < queue_family_count; i++) {
            if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                VkPhysicalDeviceFeatures features;
                vkGetPhysicalDeviceFeatures(devices[d], &features);

                if (!features.fillModeNonSolid) {
                    fprintf(stderr,
                            "Device %d skipped: missing required features (wireframe: %d)\n", d,
                            features.fillModeNonSolid);
                    continue;
                }

                r->physical_device = devices[d];
                r->graphics_queue_family = i;
                if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    free(queue_families);
                    free(devices);
                    vkGetPhysicalDeviceMemoryProperties(r->physical_device, &r->mem_properties);
                    r->non_coherent_atom_size = props.limits.nonCoherentAtomSize;
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
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(r->physical_device, &props);
        r->non_coherent_atom_size = props.limits.nonCoherentAtomSize;
        return true;
    }
    return false;
}

bool create_logical_device(VulkanRenderer *r) {
    float queue_priority = 1.0F;
    VkDeviceQueueCreateInfo queue_create_info = {.sType =
                                                     VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_create_info.queueFamilyIndex = r->graphics_queue_family;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    // Only Vulkan 1.0 core features are required.
    VkPhysicalDeviceFeatures device_features = {0};
    device_features.fillModeNonSolid = VK_TRUE;
    // Make out-of-range buffer accesses well-defined (return 0) instead of UB.
    device_features.robustBufferAccess = VK_TRUE;

    VkDeviceCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    create_info.pQueueCreateInfos = &queue_create_info;
    create_info.queueCreateInfoCount = 1;
    create_info.pEnabledFeatures = &device_features;

    if (vkCreateDevice(r->physical_device, &create_info, NULL, &r->device) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create logical device\n");
        return false;
    }

#ifndef NDEBUG
    r->pfn_set_object_name = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(
        r->device, "vkSetDebugUtilsObjectNameEXT");
#endif

    vkGetDeviceQueue(r->device, r->graphics_queue_family, 0, &r->graphics_queue);

    VK_NAME(r, VK_OBJECT_TYPE_DEVICE, r->device, "device");
    VK_NAME(r, VK_OBJECT_TYPE_QUEUE, r->graphics_queue, "graphics_queue");
    return true;
}
