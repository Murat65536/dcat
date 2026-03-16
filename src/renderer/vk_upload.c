#include "vk_upload.h"
#include "vk_memory.h"
#include <string.h>

static void upload_texture_image(VulkanRenderer* r, const Texture* texture, VkFormat format,
                                 VkImage* image, VulkanAllocation* alloc, VkImageView* view,
                                 uint32_t* cached_w, uint32_t* cached_h) {
    if (*cached_w != texture->width || *cached_h != texture->height || *image == VK_NULL_HANDLE) {
        if (*view != VK_NULL_HANDLE) {
            vkDestroyImageView(r->device, *view, NULL);
            *view = VK_NULL_HANDLE;
        }
        if (*image != VK_NULL_HANDLE) {
            vkDestroyImage(r->device, *image, NULL);
            vkFreeMemory(r->device, alloc->memory, NULL);
            *image = VK_NULL_HANDLE;
        }

        create_image(r, texture->width, texture->height, format,
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, alloc);
        *view = create_image_view(r, *image, format, VK_IMAGE_ASPECT_COLOR_BIT);

        *cached_w = texture->width;
        *cached_h = texture->height;
    }

    VkDeviceSize image_size = texture->data_size;
    VkBuffer staging_buf;
    VulkanAllocation staging_alloc;
    create_buffer(r, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &staging_buf, &staging_alloc);

    memcpy(staging_alloc.mapped, texture->data, image_size);

    transition_image_layout(r, *image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(r, staging_buf, *image, texture->width, texture->height);
    transition_image_layout(r, *image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(r->device, staging_buf, NULL);
    vkFreeMemory(r->device, staging_alloc.memory, NULL);
}

void update_material_texture(VulkanRenderer* r, MaterialGPUData* mat,
                             const Texture* diffuse, const Texture* normal) {
    bool diffuse_changed = false;
    bool normal_changed = false;

    // Check if diffuse needs update
    if (mat->cached_diffuse_data_ptr != diffuse->data ||
        mat->cached_diffuse_width != diffuse->width ||
        mat->cached_diffuse_height != diffuse->height ||
        mat->diffuse_image == VK_NULL_HANDLE) {
        bool size_changed = (mat->cached_diffuse_width != diffuse->width ||
                             mat->cached_diffuse_height != diffuse->height ||
                             mat->diffuse_image == VK_NULL_HANDLE);
        upload_texture_image(r, diffuse, VK_FORMAT_R8G8B8A8_SRGB,
                             &mat->diffuse_image, &mat->diffuse_image_alloc,
                             &mat->diffuse_image_view,
                             &mat->cached_diffuse_width, &mat->cached_diffuse_height);
        mat->cached_diffuse_data_ptr = diffuse->data;
        diffuse_changed = true;
        if (size_changed) {
            for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
                mat->descriptor_sets_dirty[i] = true;
        }
    }

    // Check if normal needs update
    if (mat->cached_normal_data_ptr != normal->data ||
        mat->cached_normal_width != normal->width ||
        mat->cached_normal_height != normal->height ||
        mat->normal_image == VK_NULL_HANDLE) {
        bool size_changed = (mat->cached_normal_width != normal->width ||
                             mat->cached_normal_height != normal->height ||
                             mat->normal_image == VK_NULL_HANDLE);
        upload_texture_image(r, normal, VK_FORMAT_R8G8B8A8_SRGB,
                             &mat->normal_image, &mat->normal_image_alloc,
                             &mat->normal_image_view,
                             &mat->cached_normal_width, &mat->cached_normal_height);
        mat->cached_normal_data_ptr = normal->data;
        normal_changed = true;
        if (size_changed) {
            for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
                mat->descriptor_sets_dirty[i] = true;
        }
    }

    (void)diffuse_changed;
    (void)normal_changed;
}

void update_skydome_texture(VulkanRenderer* r, const Texture* texture) {
    if (!texture->data || texture->data_size == 0) return;

    if (r->cached_skydome_data_ptr == texture->data && r->skydome_image != VK_NULL_HANDLE) {
        return;
    }

    if (r->skydome_image != VK_NULL_HANDLE) {
        vkDestroyImageView(r->device, r->skydome_image_view, NULL);
        vkDestroyImage(r->device, r->skydome_image, NULL);
        vkFreeMemory(r->device, r->skydome_image_alloc.memory, NULL);
    }

    create_image(r, texture->width, texture->height, VK_FORMAT_R8G8B8A8_UNORM,
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->skydome_image, &r->skydome_image_alloc);
    r->skydome_image_view = create_image_view(r, r->skydome_image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    VkDeviceSize image_size = texture->data_size;
    VkBuffer staging_buf;
    VulkanAllocation staging_alloc;
    create_buffer(r, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &staging_buf, &staging_alloc);

    memcpy(staging_alloc.mapped, texture->data, image_size);

    transition_image_layout(r, r->skydome_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(r, staging_buf, r->skydome_image, texture->width, texture->height);
    transition_image_layout(r, r->skydome_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(r->device, staging_buf, NULL);
    vkFreeMemory(r->device, staging_alloc.memory, NULL);

    r->cached_skydome_data_ptr = texture->data;

    // Update descriptor sets
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo image_info = {0};
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_info.imageView = r->skydome_image_view;
        image_info.sampler = r->sampler;

        VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = r->skydome_descriptor_sets[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &image_info;

        vkUpdateDescriptorSets(r->device, 1, &write, 0, NULL);
    }
}

void update_vertex_buffer(VulkanRenderer* r, const VertexArray* vertices) {
    if (vertices->count == 0) return;
    VkDeviceSize buffer_size = sizeof(Vertex) * vertices->count;

    if (r->cached_vertex_count != vertices->count || r->vertex_buffer == VK_NULL_HANDLE) {
        if (r->vertex_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->vertex_buffer, NULL);
            vkFreeMemory(r->device, r->vertex_buffer_alloc.memory, NULL);
        }

        upload_buffer_via_staging(r, vertices->data, buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  &r->vertex_buffer, &r->vertex_buffer_alloc);
        r->cached_vertex_count = vertices->count;
    }
}

void update_index_buffer(VulkanRenderer* r, const Uint32Array* indices) {
    if (indices->count == 0) return;
    VkDeviceSize buffer_size = sizeof(uint32_t) * indices->count;

    if (r->cached_index_count != indices->count || r->index_buffer == VK_NULL_HANDLE) {
        if (r->index_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->index_buffer, NULL);
            vkFreeMemory(r->device, r->index_buffer_alloc.memory, NULL);
        }

        upload_buffer_via_staging(r, indices->data, buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  &r->index_buffer, &r->index_buffer_alloc);
        r->cached_index_count = indices->count;
    }
}
