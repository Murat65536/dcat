#pragma once
#include "vulkan_renderer.h"

bool create_instance(VulkanRenderer *r);
bool select_physical_device(VulkanRenderer *r);
bool create_logical_device(VulkanRenderer *r);
