#include "vk_memory.h"
#include <stdio.h>
#include <string.h>

uint32_t find_memory_type(VulkanRenderer* r, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    for (uint32_t i = 0; i < r->mem_properties.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && 
            (r->mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    fprintf(stderr, "Failed to find suitable memory type\n");
    return 0;
}

VkDeviceSize align_up(VkDeviceSize size, VkDeviceSize alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

bool create_buffer(VulkanRenderer* r, VkDeviceSize size, VkBufferUsageFlags usage,
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

bool create_image(VulkanRenderer* r, uint32_t width, uint32_t height, VkFormat format,
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

VkImageView create_image_view(VulkanRenderer* r, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags) {
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

VkCommandBuffer begin_single_time_commands(VulkanRenderer* r) {
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

void end_single_time_commands(VulkanRenderer* r, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    
    vkQueueSubmit(r->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(r->graphics_queue);
    
    vkFreeCommandBuffers(r->device, r->command_pool, 1, &cmd);
}

void transition_image_layout(VulkanRenderer* r, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout) {
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

void copy_buffer_to_image(VulkanRenderer* r, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
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

void upload_buffer_via_staging(VulkanRenderer* r, const void* data, VkDeviceSize size,
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
