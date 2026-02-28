#ifndef DCAT_VK_UPLOAD_H
#define DCAT_VK_UPLOAD_H

#include "vulkan_renderer.h"

void update_diffuse_texture(VulkanRenderer* r, const Texture* texture);
void update_normal_texture(VulkanRenderer* r, const Texture* texture);
void update_skydome_texture(VulkanRenderer* r, const Texture* texture);
void update_vertex_buffer(VulkanRenderer* r, const VertexArray* vertices);
void update_index_buffer(VulkanRenderer* r, const Uint32Array* indices);

#endif // DCAT_VK_UPLOAD_H
