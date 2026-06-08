#pragma once
#include "vulkan_renderer.h"

char *read_shader_file(VulkanRenderer *r, const char *filename, size_t *out_size);
VkShaderModule create_shader_module(VulkanRenderer *r, const char *code, size_t size);
