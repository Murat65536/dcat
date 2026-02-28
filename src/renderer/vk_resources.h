#ifndef DCAT_VK_RESOURCES_H
#define DCAT_VK_RESOURCES_H

#include "vulkan_renderer.h"

bool create_command_pool(VulkanRenderer* r);
bool create_descriptor_pool(VulkanRenderer* r);
bool create_render_targets(VulkanRenderer* r);
bool create_framebuffer(VulkanRenderer* r);
bool create_staging_buffers(VulkanRenderer* r);
bool create_uniform_buffers(VulkanRenderer* r);
bool create_sampler(VulkanRenderer* r);
bool create_command_buffers(VulkanRenderer* r);
bool create_sync_objects(VulkanRenderer* r);
bool create_descriptor_sets(VulkanRenderer* r);

void cleanup_render_targets(VulkanRenderer* r);
void cleanup(VulkanRenderer* r);

#endif // DCAT_VK_RESOURCES_H
