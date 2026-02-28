#include "vk_resources.h"
#include "vk_memory.h"
#include <stdio.h>

bool create_command_pool(VulkanRenderer* r) {
    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.queueFamilyIndex = r->graphics_queue_family;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    if (vkCreateCommandPool(r->device, &pool_info, NULL, &r->command_pool) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create command pool\n");
        return false;
    }
    return true;
}

bool create_descriptor_pool(VulkanRenderer* r) {
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

bool create_render_targets(VulkanRenderer* r) {
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

bool create_framebuffer(VulkanRenderer* r) {
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

bool create_staging_buffers(VulkanRenderer* r) {
    VkDeviceSize buffer_size = r->width * r->height * 4;
    buffer_size = align_up(buffer_size, r->non_coherent_atom_size);

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

bool create_uniform_buffers(VulkanRenderer* r) {
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

bool create_sampler(VulkanRenderer* r) {
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

bool create_command_buffers(VulkanRenderer* r) {
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

bool create_sync_objects(VulkanRenderer* r) {
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

bool create_descriptor_sets(VulkanRenderer* r) {
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

void cleanup_render_targets(VulkanRenderer* r) {
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

void cleanup(VulkanRenderer* r) {
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
