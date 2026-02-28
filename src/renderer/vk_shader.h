#ifndef DCAT_VK_SHADER_H
#define DCAT_VK_SHADER_H

#include "vulkan_renderer.h"
#include <stddef.h>

char* read_shader_file(VulkanRenderer* r, const char* filename, size_t* out_size);
VkShaderModule create_shader_module(VulkanRenderer* r, const char* code, size_t size);

#endif // DCAT_VK_SHADER_H
