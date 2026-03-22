#ifndef DCAT_VK_UPLOAD_H
#define DCAT_VK_UPLOAD_H

#include "vulkan_renderer.h"

bool update_material_texture(VulkanRenderer* r, MaterialGPUData* mat,
                             const Texture* diffuse, const Texture* normal);
bool update_skydome_texture(VulkanRenderer* r, const Texture* texture);
bool update_vertex_buffer(VulkanRenderer* r, const VertexArray* vertices);
bool update_index_buffer(VulkanRenderer* r, const Uint32Array* indices);

#endif // DCAT_VK_UPLOAD_H
