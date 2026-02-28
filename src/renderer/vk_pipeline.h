#ifndef DCAT_VK_PIPELINE_H
#define DCAT_VK_PIPELINE_H

#include "vulkan_renderer.h"

bool create_descriptor_set_layout(VulkanRenderer* r);
bool create_pipeline_layout(VulkanRenderer* r);
bool create_render_pass(VulkanRenderer* r);
bool create_graphics_pipeline(VulkanRenderer* r);
bool create_skydome_pipeline(VulkanRenderer* r);

#endif // DCAT_VK_PIPELINE_H
