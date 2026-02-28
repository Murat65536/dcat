#ifndef DCAT_VK_DEVICE_H
#define DCAT_VK_DEVICE_H

#include "vulkan_renderer.h"

bool create_instance(VulkanRenderer* r);
bool select_physical_device(VulkanRenderer* r);
bool create_logical_device(VulkanRenderer* r);

#endif // DCAT_VK_DEVICE_H
