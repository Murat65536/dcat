#ifndef DCAT_VK_MEMORY_H
#define DCAT_VK_MEMORY_H

#include "vulkan_renderer.h"

uint32_t find_memory_type(VulkanRenderer* r, uint32_t type_filter, VkMemoryPropertyFlags properties);
VkDeviceSize align_up(VkDeviceSize size, VkDeviceSize alignment);

bool create_buffer(VulkanRenderer* r, VkDeviceSize size, VkBufferUsageFlags usage,
                   VkMemoryPropertyFlags properties, VkBuffer* buffer, VulkanAllocation* alloc);

bool create_image(VulkanRenderer* r, uint32_t width, uint32_t height, VkFormat format,
                  VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                  VkImage* image, VulkanAllocation* alloc);

VkImageView create_image_view(VulkanRenderer* r, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags);

VkCommandBuffer begin_single_time_commands(VulkanRenderer* r);
void end_single_time_commands(VulkanRenderer* r, VkCommandBuffer cmd);

void transition_image_layout(VulkanRenderer* r, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);
void copy_buffer_to_image(VulkanRenderer* r, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

void upload_buffer_via_staging(VulkanRenderer* r, const void* data, VkDeviceSize size,
                                VkBufferUsageFlagBits usage_bit, VkBuffer* buffer, VulkanAllocation* alloc);

#endif // DCAT_VK_MEMORY_H
