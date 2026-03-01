#include "vk_device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool create_instance(VulkanRenderer* r) {
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

bool select_physical_device(VulkanRenderer* r) {
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

bool create_logical_device(VulkanRenderer* r) {
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
