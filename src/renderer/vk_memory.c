#include "vk_memory.h"
#include <string.h>

bool find_memory_type(VulkanRenderer *r, const uint32_t type_filter,
                      const VkMemoryPropertyFlags properties, uint32_t *out_memory_type) {
    for (uint32_t i = 0; i < r->mem_properties.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) &&
            (r->mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            *out_memory_type = i;
            return true;
        }
    }
    vulkan_renderer_set_error(r, VK_ERROR_UNKNOWN, "find_memory_type",
                              "Failed to find suitable memory type");
    return false;
}

bool create_buffer(VulkanRenderer *r, const VkDeviceSize size, const VkBufferUsageFlags usage,
                   const VkMemoryPropertyFlags properties, VkBuffer *buffer,
                   VulkanAllocation *alloc) {
    *buffer = VK_NULL_HANDLE;
    memset(alloc, 0, sizeof(*alloc));

    VkBufferCreateInfo buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(r->device, &buffer_info, NULL, buffer);
    if (result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, result, "vkCreateBuffer", "Failed to create buffer");
        return false;
    }

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(r->device, *buffer, &mem_req);

    VkMemoryAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc_info.allocationSize = mem_req.size;
    if (!find_memory_type(r, mem_req.memoryTypeBits, properties, &alloc_info.memoryTypeIndex)) {
        vkDestroyBuffer(r->device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
        return false;
    }

    result = vkAllocateMemory(r->device, &alloc_info, NULL, &alloc->memory);
    if (result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, result, "vkAllocateMemory",
                                  "Failed to allocate buffer memory");
        vkDestroyBuffer(r->device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
        return false;
    }

    alloc->offset = 0;
    alloc->size = mem_req.size;
    alloc->mapped = NULL;

    result = vkBindBufferMemory(r->device, *buffer, alloc->memory, 0);
    if (result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, result, "vkBindBufferMemory", "Failed to bind buffer memory");
        vkFreeMemory(r->device, alloc->memory, NULL);
        vkDestroyBuffer(r->device, *buffer, NULL);
        memset(alloc, 0, sizeof(*alloc));
        *buffer = VK_NULL_HANDLE;
        return false;
    }

    // Map if host visible
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        result = vkMapMemory(r->device, alloc->memory, 0, size, 0, &alloc->mapped);
        if (result != VK_SUCCESS) {
            vulkan_renderer_set_error(r, result, "vkMapMemory", "Failed to map buffer memory");
            vkFreeMemory(r->device, alloc->memory, NULL);
            vkDestroyBuffer(r->device, *buffer, NULL);
            memset(alloc, 0, sizeof(*alloc));
            *buffer = VK_NULL_HANDLE;
            return false;
        }
    }

    return true;
}

bool create_image(VulkanRenderer *r, const uint32_t width, const uint32_t height,
                  const VkFormat format, const VkImageUsageFlags usage,
                  VkMemoryPropertyFlags properties, VkImage *image, VulkanAllocation *alloc) {
    *image = VK_NULL_HANDLE;
    memset(alloc, 0, sizeof(*alloc));

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

    VkResult result = vkCreateImage(r->device, &image_info, NULL, image);
    if (result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, result, "vkCreateImage", "Failed to create image");
        return false;
    }

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(r->device, *image, &mem_req);

    VkMemoryAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc_info.allocationSize = mem_req.size;
    if (!find_memory_type(r, mem_req.memoryTypeBits, properties, &alloc_info.memoryTypeIndex)) {
        vkDestroyImage(r->device, *image, NULL);
        *image = VK_NULL_HANDLE;
        return false;
    }

    result = vkAllocateMemory(r->device, &alloc_info, NULL, &alloc->memory);
    if (result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, result, "vkAllocateMemory", "Failed to allocate image memory");
        vkDestroyImage(r->device, *image, NULL);
        *image = VK_NULL_HANDLE;
        return false;
    }

    alloc->offset = 0;
    alloc->size = mem_req.size;
    alloc->mapped = NULL;

    result = vkBindImageMemory(r->device, *image, alloc->memory, 0);
    if (result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, result, "vkBindImageMemory", "Failed to bind image memory");
        vkFreeMemory(r->device, alloc->memory, NULL);
        vkDestroyImage(r->device, *image, NULL);
        memset(alloc, 0, sizeof(*alloc));
        *image = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

VkImageView create_image_view(VulkanRenderer *r, VkImage image, VkFormat format,
                              VkImageAspectFlags aspect_flags) {
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
    VkResult result = vkCreateImageView(r->device, &view_info, NULL, &image_view);
    if (result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, result, "vkCreateImageView", "Failed to create image view");
        return VK_NULL_HANDLE;
    }
    return image_view;
}

bool begin_single_time_commands(VulkanRenderer *r, VkCommandBuffer *out_cmd) {
    *out_cmd = VK_NULL_HANDLE;

    VkCommandBufferAllocateInfo alloc_info = {.sType =
                                                  VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = r->command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkResult result = vkAllocateCommandBuffers(r->device, &alloc_info, &cmd);
    if (result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, result, "vkAllocateCommandBuffers",
                                  "Failed to allocate single-use command buffer");
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(cmd, &begin_info);
    if (result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, result, "vkBeginCommandBuffer",
                                  "Failed to begin single-use command buffer");
        vkFreeCommandBuffers(r->device, r->command_pool, 1, &cmd);
        return false;
    }

    *out_cmd = cmd;
    return true;
}

bool end_single_time_commands(VulkanRenderer *r, VkCommandBuffer cmd) {
    VkResult result = vkEndCommandBuffer(cmd);
    if (result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, result, "vkEndCommandBuffer",
                                  "Failed to end single-use command buffer");
        vkFreeCommandBuffers(r->device, r->command_pool, 1, &cmd);
        return false;
    }

    VkFence transfer_fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    result = vkCreateFence(r->device, &fence_info, NULL, &transfer_fence);
    if (result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, result, "vkCreateFence",
                                  "Failed to create fence for single-use command buffer");
        vkFreeCommandBuffers(r->device, r->command_pool, 1, &cmd);
        return false;
    }

    VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    result = vkQueueSubmit(r->graphics_queue, 1, &submit_info, transfer_fence);
    const char *operation = "vkQueueSubmit";
    const char *detail = "Failed to submit single-use command buffer";
    if (result == VK_SUCCESS) {
        result = vkWaitForFences(r->device, 1, &transfer_fence, VK_TRUE, UINT64_MAX);
        operation = "vkWaitForFences";
        detail = "Failed waiting for single-use command buffer fence";
    }

    vkDestroyFence(r->device, transfer_fence, NULL);
    vkFreeCommandBuffers(r->device, r->command_pool, 1, &cmd);
    if (result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, result, operation, detail);
        return false;
    }
    return true;
}

bool transition_image_layout(VulkanRenderer *r, VkImage image, VkImageLayout old_layout,
                             VkImageLayout new_layout) {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (!begin_single_time_commands(r, &cmd)) {
        return false;
    }

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

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
        new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
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

    return end_single_time_commands(r, cmd);
}

bool copy_buffer_to_image(VulkanRenderer *r, VkBuffer buffer, VkImage image, uint32_t width,
                          uint32_t height) {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (!begin_single_time_commands(r, &cmd)) {
        return false;
    }

    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = width;
    region.imageExtent.height = height;
    region.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    return end_single_time_commands(r, cmd);
}

bool upload_buffer_via_staging(VulkanRenderer *r, const void *data, VkDeviceSize size,
                               VkBufferUsageFlagBits usage_bit, VkBuffer *buffer,
                               VulkanAllocation *alloc) {
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VulkanAllocation staging_alloc = {0};
    if (!create_buffer(r, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &staging_buffer, &staging_alloc)) {
        return false;
    }

    if (!create_buffer(r, size, usage_bit | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, alloc)) {
        vkDestroyBuffer(r->device, staging_buffer, NULL);
        vkFreeMemory(r->device, staging_alloc.memory, NULL);
        return false;
    }

    memcpy(staging_alloc.mapped, data, size);

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (!begin_single_time_commands(r, &cmd)) {
        vkDestroyBuffer(r->device, staging_buffer, NULL);
        vkFreeMemory(r->device, staging_alloc.memory, NULL);
        vkDestroyBuffer(r->device, *buffer, NULL);
        vkFreeMemory(r->device, alloc->memory, NULL);
        *buffer = VK_NULL_HANDLE;
        memset(alloc, 0, sizeof(*alloc));
        return false;
    }
    VkBufferCopy copy_region = {0, 0, size};
    vkCmdCopyBuffer(cmd, staging_buffer, *buffer, 1, &copy_region);
    if (!end_single_time_commands(r, cmd)) {
        vkDestroyBuffer(r->device, staging_buffer, NULL);
        vkFreeMemory(r->device, staging_alloc.memory, NULL);
        vkDestroyBuffer(r->device, *buffer, NULL);
        vkFreeMemory(r->device, alloc->memory, NULL);
        *buffer = VK_NULL_HANDLE;
        memset(alloc, 0, sizeof(*alloc));
        return false;
    }

    vkDestroyBuffer(r->device, staging_buffer, NULL);
    vkFreeMemory(r->device, staging_alloc.memory, NULL);
    return true;
}
