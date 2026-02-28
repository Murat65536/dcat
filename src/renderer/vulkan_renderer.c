#include "vulkan_renderer.h"
#include "vk_device.h"
#include "vk_memory.h"
#include "vk_pipeline.h"
#include "vk_resources.h"
#include "vk_upload.h"
#include <stdlib.h>
#include <string.h>

VulkanRenderer* vulkan_renderer_create(uint32_t width, uint32_t height) {
    VulkanRenderer* r = calloc(1, sizeof(VulkanRenderer));
    if (!r) return NULL;
    
    r->width = width;
    r->height = height;
    glm_vec3_normalize_to((vec3){0.0f, -1.0f, -0.5f}, r->normalized_light_dir);
    
    return r;
}

void vulkan_renderer_destroy(VulkanRenderer* r) {
    if (!r) return;
    cleanup(r);
    free(r);
}

bool vulkan_renderer_initialize(VulkanRenderer* r) {
    if (!create_instance(r)) return false;
    if (!select_physical_device(r)) return false;
    if (!create_logical_device(r)) return false;
    if (!create_command_pool(r)) return false;
    if (!create_descriptor_pool(r)) return false;
    if (!create_descriptor_set_layout(r)) return false;
    if (!create_pipeline_layout(r)) return false;
    if (!create_render_pass(r)) return false;
    if (!create_graphics_pipeline(r)) return false;
    if (!create_render_targets(r)) return false;
    if (!create_framebuffer(r)) return false;
    if (!create_staging_buffers(r)) return false;
    if (!create_uniform_buffers(r)) return false;
    if (!create_sampler(r)) return false;
    if (!create_command_buffers(r)) return false;
    if (!create_sync_objects(r)) return false;
    if (!create_descriptor_sets(r)) return false;

    create_skydome_pipeline(r);
    
    // Initialize frame tracking
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        r->frame_ready[i] = false;
    }
    
    return true;
}

void vulkan_renderer_set_light_direction(VulkanRenderer* r, const float* direction) {
    glm_vec3_normalize_to((float*)direction, r->normalized_light_dir);
}

void vulkan_renderer_set_wireframe_mode(VulkanRenderer* r, bool enabled) {
    atomic_store(&r->wireframe_mode, enabled);
}

bool vulkan_renderer_get_wireframe_mode(const VulkanRenderer* r) {
    return atomic_load(&r->wireframe_mode);
}

void vulkan_renderer_resize(VulkanRenderer* r, uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(r->device);
    
    r->width = width;
    r->height = height;
    
    cleanup_render_targets(r);
    create_render_targets(r);
    create_framebuffer(r);
    
    // Recreate staging and terminal output buffers
    for (int i = 0; i < NUM_STAGING_BUFFERS; i++) {
        if (r->staging_buffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->staging_buffers[i], NULL);
            vkFreeMemory(r->device, r->staging_buffer_allocs[i].memory, NULL);
        }
    }
    create_staging_buffers(r);
    
    // Reset frame tracking
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        r->frame_ready[i] = false;
    }
}

void vulkan_renderer_wait_idle(VulkanRenderer* r) {
    if (r && r->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(r->device);
    }
}

void vulkan_renderer_set_skydome(VulkanRenderer* r, const Mesh* mesh, const Texture* texture) {
    r->skydome_mesh = mesh;
    r->skydome_texture = texture;
    
    if (mesh && mesh->vertices.count > 0 && mesh->indices.count > 0) {
        // Clean up old buffers
        if (r->skydome_vertex_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->skydome_vertex_buffer, NULL);
            vkFreeMemory(r->device, r->skydome_vertex_buffer_alloc.memory, NULL);
        }
        if (r->skydome_index_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->skydome_index_buffer, NULL);
            vkFreeMemory(r->device, r->skydome_index_buffer_alloc.memory, NULL);
        }
        
        VkDeviceSize vertex_size = sizeof(Vertex) * mesh->vertices.count;
        VkDeviceSize index_size = sizeof(uint32_t) * mesh->indices.count;
        
        upload_buffer_via_staging(r, mesh->vertices.data, vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  &r->skydome_vertex_buffer, &r->skydome_vertex_buffer_alloc);
        
        upload_buffer_via_staging(r, mesh->indices.data, index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  &r->skydome_index_buffer, &r->skydome_index_buffer_alloc);
    }
    
    if (texture && texture->data_size > 0) {
        update_skydome_texture(r, texture);
    }
}

const uint8_t* vulkan_renderer_render(
    VulkanRenderer* r,
    const Mesh* mesh,
    const mat4 mvp,
    const mat4 model,
    const Texture* diffuse_texture,
    const Texture* normal_texture,
    bool enable_lighting,
    const vec3 camera_pos,
    bool use_triplanar_mapping,
    AlphaMode alpha_mode,
    const mat4* bone_matrices,
    uint32_t bone_count,
    const mat4* view,
    const mat4* projection
) {
    vkWaitForFences(r->device, 1, &r->in_flight_fences[r->current_frame], VK_TRUE, UINT64_MAX);
    
    // Read framebuffer from the frame that just completed
    const uint8_t* result = NULL;
    uint32_t ready_staging_idx = r->frame_staging_buffers[r->current_frame];
    
    if (r->frame_ready[r->current_frame]) {
        // Invalidate CPU cache before reading from GPU-written staging buffer
        VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = r->staging_buffer_allocs[ready_staging_idx].memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        vkInvalidateMappedMemoryRanges(r->device, 1, &range);
        result = (const uint8_t*)r->staging_buffer_allocs[ready_staging_idx].mapped;
    }
    
    vkResetFences(r->device, 1, &r->in_flight_fences[r->current_frame]);
    
    r->current_staging_buffer = (r->current_staging_buffer + 1) % NUM_STAGING_BUFFERS;
    uint32_t write_staging_idx = r->current_staging_buffer;
    r->frame_staging_buffers[r->current_frame] = write_staging_idx;
    
    update_diffuse_texture(r, diffuse_texture);
    update_normal_texture(r, normal_texture);
    
    if (r->cached_mesh_generation != mesh->generation || r->vertex_buffer == VK_NULL_HANDLE) {
        update_vertex_buffer(r, &mesh->vertices);
        update_index_buffer(r, &mesh->indices);
        r->cached_mesh_generation = mesh->generation;
    }
    
    if (r->descriptor_sets_dirty[r->current_frame]) {
        VkDescriptorBufferInfo uniform_info = {r->uniform_buffers[r->current_frame], 0, sizeof(Uniforms)};
        VkDescriptorImageInfo diffuse_info = {r->sampler, r->diffuse_image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo normal_info = {r->sampler, r->normal_image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo frag_uniform_info = {r->fragment_uniform_buffers[r->current_frame], 0, sizeof(FragmentUniforms)};
        
        VkWriteDescriptorSet writes[4] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptor_sets[r->current_frame], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &uniform_info, NULL},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptor_sets[r->current_frame], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &diffuse_info, NULL, NULL},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptor_sets[r->current_frame], 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &normal_info, NULL, NULL},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptor_sets[r->current_frame], 3, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &frag_uniform_info, NULL}
        };
        
        vkUpdateDescriptorSets(r->device, 4, writes, 0, NULL);
        r->descriptor_sets_dirty[r->current_frame] = false;
    }
    
    // Prepare push constants
    PushConstants push_constants;
    glm_mat4_copy((float(*)[4])mvp, push_constants.mvp);
    glm_mat4_copy((float(*)[4])model, push_constants.model);
    
    // Prepare uniform buffer
    Uniforms uniforms = {0};
    uniforms.has_animation = (bone_matrices != NULL) ? 1 : 0;
    
    if (bone_matrices && bone_count > 0) {
        uint32_t num_bones = bone_count < MAX_BONES ? bone_count : MAX_BONES;
        memcpy(uniforms.bone_matrices, bone_matrices, num_bones * sizeof(mat4));
        for (uint32_t i = num_bones; i < MAX_BONES; i++) {
            glm_mat4_identity(uniforms.bone_matrices[i]);
        }
    } else {
        for (uint32_t i = 0; i < MAX_BONES; i++) {
            glm_mat4_identity(uniforms.bone_matrices[i]);
        }
    }
    
    memcpy(r->uniform_buffer_allocs[r->current_frame].mapped, &uniforms, sizeof(Uniforms));
    
    // Prepare fragment uniforms
    FragmentUniforms frag_uniforms = {0};
    glm_vec3_copy(r->normalized_light_dir, frag_uniforms.light_dir);
    frag_uniforms.enable_lighting = enable_lighting ? 1 : 0;
    glm_vec3_copy((float*)camera_pos, frag_uniforms.camera_pos);
    frag_uniforms.fog_start = 5.0f;
    glm_vec3_zero(frag_uniforms.fog_color);
    frag_uniforms.fog_end = 10.0f;
    frag_uniforms.use_triplanar_mapping = use_triplanar_mapping ? 1 : 0;
    frag_uniforms.alpha_cutoff = 0.5f;
    
    switch (alpha_mode) {
        case ALPHA_MODE_MASK: frag_uniforms.alpha_mode = 1; break;
        case ALPHA_MODE_BLEND: frag_uniforms.alpha_mode = 2; break;
        default: frag_uniforms.alpha_mode = 0; break;
    }
    
    memcpy(r->fragment_uniform_buffer_allocs[r->current_frame].mapped, &frag_uniforms, sizeof(FragmentUniforms));
    
    VkCommandBuffer cmd = r->command_buffers[r->current_frame];
    vkResetCommandBuffer(cmd, 0);
    
    VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &begin_info);
    
    VkClearValue clear_values[2] = {{{{0, 0, 0, 1}}}, {{{0.0f, 0}}}};
    
    VkRenderPassBeginInfo rp_info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp_info.renderPass = r->render_pass;
    rp_info.framebuffer = r->framebuffer;
    rp_info.renderArea.extent.width = r->width;
    rp_info.renderArea.extent.height = r->height;
    rp_info.clearValueCount = 2;
    rp_info.pClearValues = clear_values;
    
    vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);
    
    VkViewport viewport = {0, 0, (float)r->width, (float)r->height, 1, 0};
    VkRect2D scissor = {{0, 0}, {r->width, r->height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    
    // Render skydome first
    if (r->skydome_mesh && r->skydome_texture && r->skydome_pipeline != VK_NULL_HANDLE &&
        r->skydome_vertex_buffer != VK_NULL_HANDLE && r->skydome_index_buffer != VK_NULL_HANDLE &&
        view != NULL && projection != NULL) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->skydome_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->skydome_pipeline_layout, 0, 1, &r->skydome_descriptor_sets[r->current_frame], 0, NULL);
        
        // Remove translation from view matrix
        mat4 sky_view;
        glm_mat4_copy((float(*)[4])*view, sky_view);
        sky_view[3][0] = sky_view[3][1] = sky_view[3][2] = 0.0f;
        
        mat4 sky_mvp;
        glm_mat4_mul((float(*)[4])*projection, sky_view, sky_mvp);
        vkCmdPushConstants(cmd, r->skydome_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), sky_mvp);
        
        VkBuffer vb[] = {r->skydome_vertex_buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, r->skydome_index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, (uint32_t)r->skydome_mesh->indices.count, 1, 0, 0, 0);
    }
    
    // Render main model
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, atomic_load(&r->wireframe_mode) ? r->wireframe_pipeline : r->graphics_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1, &r->descriptor_sets[r->current_frame], 0, NULL);
    vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push_constants);
    
    VkBuffer vbs[] = {r->vertex_buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbs, offsets);
    vkCmdBindIndexBuffer(cmd, r->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, (uint32_t)mesh->indices.count, 1, 0, 0, 0);
    
    vkCmdEndRenderPass(cmd);
    
    // Copy color image to staging buffer for CPU readback
    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = r->width;
    region.imageExtent.height = r->height;
    region.imageExtent.depth = 1;
    
    vkCmdCopyImageToBuffer(cmd, r->color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, r->staging_buffers[write_staging_idx], 1, &region);
    
    VkBufferMemoryBarrier buffer_barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.buffer = r->staging_buffers[write_staging_idx];
    buffer_barrier.size = VK_WHOLE_SIZE;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &buffer_barrier, 0, NULL);
    
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    
    vkQueueSubmit(r->graphics_queue, 1, &submit_info, r->in_flight_fences[r->current_frame]);
    
    r->frame_ready[r->current_frame] = true;
    r->current_frame = (r->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
    
    return result;
}
