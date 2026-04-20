#include "vulkan_renderer.h"
#include "vk_device.h"
#include "vk_memory.h"
#include "vk_pipeline.h"
#include "vk_resources.h"
#include "vk_upload.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *vk_result_to_string(VkResult result) {
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    default:
        return "VK_ERROR_UNKNOWN";
    }
}

static void set_wireframe_mode(atomic_bool *wireframe_mode, bool enabled) {
    *wireframe_mode = enabled;
}

static bool get_wireframe_mode(const atomic_bool *wireframe_mode) {
    return *wireframe_mode;
}

static bool wait_for_in_flight_frames(VulkanRenderer *r, const char *detail) {
    VkFence pending_fences[MAX_FRAMES_IN_FLIGHT] = {VK_NULL_HANDLE};
    uint32_t pending_count = 0;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (r->frame_ready[i] && r->in_flight_fences[i] != VK_NULL_HANDLE) {
            pending_fences[pending_count++] = r->in_flight_fences[i];
        }
    }

    if (pending_count == 0) {
        return true;
    }

    VkResult result = vkWaitForFences(r->device, pending_count, pending_fences, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, result, "vkWaitForFences", "%s", detail);
        return false;
    }

    return true;
}

void vulkan_renderer_clear_error(VulkanRenderer *r) {
    if (!r)
        return;

    r->last_error_code = VK_SUCCESS;
    r->last_error_message[0] = '\0';
}

void vulkan_renderer_set_error(VulkanRenderer *r, VkResult result, const char *operation,
                               const char *format, ...) {
    if (!r || r->last_error_message[0] != '\0') {
        return;
    }

    r->last_error_code = result;

    char detail[160] = "";
    va_list args;
    va_start(args, format);
    vsnprintf(detail, sizeof(detail), format, args);
    va_end(args);

    snprintf(r->last_error_message, sizeof(r->last_error_message), "%s: %s (%s, %d)", operation,
             detail, vk_result_to_string(result), result);
}

const char *vulkan_renderer_get_last_error(const VulkanRenderer *r) {
    return (r && r->last_error_message[0] != '\0') ? r->last_error_message : NULL;
}

VkResult vulkan_renderer_get_last_error_code(const VulkanRenderer *r) {
    return r ? r->last_error_code : VK_SUCCESS;
}

VulkanRenderer *vulkan_renderer_create(uint32_t width, uint32_t height) {
    VulkanRenderer *r = calloc(1, sizeof(VulkanRenderer));
    if (!r)
        return NULL;

    r->width = width;
    r->height = height;
    r->descriptor_pool_material_capacity = INITIAL_MATERIAL_DESCRIPTOR_CAPACITY;
    glm_vec3_normalize_to((vec3){0.0f, -1.0f, -0.5f}, r->normalized_light_dir);

    return r;
}

void vulkan_renderer_destroy(VulkanRenderer *r) {
    if (!r)
        return;
    cleanup(r);
    free(r);
}

bool vulkan_renderer_initialize(VulkanRenderer *r) {
    vulkan_renderer_clear_error(r);
    if (!create_instance(r))
        return false;
    if (!select_physical_device(r))
        return false;
    if (!create_logical_device(r))
        return false;
    if (!create_command_pool(r))
        return false;
    if (!create_descriptor_pool(r))
        return false;
    if (!create_descriptor_set_layout(r))
        return false;
    if (!create_pipeline_layout(r))
        return false;
    if (!create_render_pass(r))
        return false;
    if (!create_graphics_pipeline(r))
        return false;
    if (!create_render_targets(r))
        return false;
    if (!create_framebuffer(r))
        return false;
    if (!create_staging_buffers(r))
        return false;
    if (!create_uniform_buffers(r))
        return false;
    if (!create_sampler(r))
        return false;
    if (!create_command_buffers(r))
        return false;
    if (!create_sync_objects(r))
        return false;

    create_skydome_pipeline(r);

    // Initialize frame tracking
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        r->frame_ready[i] = false;
    }

    return true;
}

void vulkan_renderer_set_light_direction(VulkanRenderer *r, const float *direction) {
    glm_vec3_normalize_to((float *)direction, r->normalized_light_dir);
}

void vulkan_renderer_set_wireframe_mode(VulkanRenderer *r, bool enabled) {
    set_wireframe_mode(&r->wireframe_mode, enabled);
}

bool vulkan_renderer_get_wireframe_mode(const VulkanRenderer *r) {
    return get_wireframe_mode(&r->wireframe_mode);
}

bool vulkan_renderer_resize(VulkanRenderer *r, uint32_t width, uint32_t height) {
    vulkan_renderer_clear_error(r);

    if (!wait_for_in_flight_frames(r, "Failed to wait for in-flight frames during resize")) {
        return false;
    }

    r->width = width;
    r->height = height;

    cleanup_render_targets(r);
    if (!create_render_targets(r) || !create_framebuffer(r)) {
        return false;
    }

    // Recreate staging buffers
    for (int i = 0; i < NUM_STAGING_BUFFERS; i++) {
        if (r->staging_buffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, r->staging_buffers[i], NULL);
            vkFreeMemory(r->device, r->staging_buffer_allocs[i].memory, NULL);
        }
    }
    if (!create_staging_buffers(r)) {
        return false;
    }

    // Reset frame tracking
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        r->frame_ready[i] = false;
    }

    return true;
}

void vulkan_renderer_wait_idle(const VulkanRenderer *r) {
    if (r && r->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(r->device);
    }
}

bool vulkan_renderer_set_skydome(VulkanRenderer *r, const Mesh *mesh, const Texture *texture) {
    vulkan_renderer_clear_error(r);
    r->skydome_mesh = mesh;
    r->skydome_texture = texture;

    if (mesh && mesh->vertices.count > 0 && mesh->indices.count > 0) {
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

        if (!upload_buffer_via_staging(r, mesh->vertices.data, vertex_size,
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->skydome_vertex_buffer,
                                       &r->skydome_vertex_buffer_alloc) ||
            !upload_buffer_via_staging(r, mesh->indices.data, index_size,
                                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &r->skydome_index_buffer,
                                       &r->skydome_index_buffer_alloc)) {
            return false;
        }
    }

    if (texture && texture->data_size > 0) {
        if (!update_skydome_texture(r, texture)) {
            return false;
        }
    }

    return true;
}

static void cleanup_material_fragment_uniform_buffers(const VulkanRenderer *r,
                                                      MaterialGPUData *mat) {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (mat->fragment_uniform_buffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(r->device, mat->fragment_uniform_buffers[i], NULL);
            mat->fragment_uniform_buffers[i] = VK_NULL_HANDLE;
        }
        if (mat->fragment_uniform_buffer_allocs[i].memory != VK_NULL_HANDLE) {
            vkFreeMemory(r->device, mat->fragment_uniform_buffer_allocs[i].memory, NULL);
            memset(&mat->fragment_uniform_buffer_allocs[i], 0,
                   sizeof(mat->fragment_uniform_buffer_allocs[i]));
        }
    }
}

static bool allocate_descriptor_sets_from_pool(VulkanRenderer *r, VkDescriptorPool descriptor_pool,
                                               VkDescriptorSetLayout layout,
                                               VkDescriptorSet *descriptor_sets,
                                               const char *detail) {
    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        layouts[i] = layout;
    }

    VkDescriptorSetAllocateInfo alloc_info = {.sType =
                                                  VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = descriptor_pool;
    alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    alloc_info.pSetLayouts = layouts;

    const VkResult result = vkAllocateDescriptorSets(r->device, &alloc_info, descriptor_sets);
    if (result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, result, "vkAllocateDescriptorSets", "%s", detail);
        return false;
    }

    return true;
}

static uint32_t next_material_descriptor_capacity(uint32_t current_capacity,
                                                  uint32_t required_materials) {
    uint32_t capacity = current_capacity;
    if (capacity < INITIAL_MATERIAL_DESCRIPTOR_CAPACITY) {
        capacity = INITIAL_MATERIAL_DESCRIPTOR_CAPACITY;
    }

    while (capacity < required_materials) {
        capacity *= 2;
    }

    return capacity;
}

static bool rebuild_material_descriptor_pool(VulkanRenderer *r, uint32_t material_count,
                                             uint32_t material_capacity) {
    if (!wait_for_in_flight_frames(r,
                                   "Failed to wait for in-flight frames before growing descriptors")) {
        return false;
    }

    VkDescriptorPool new_pool = VK_NULL_HANDLE;
    if (!create_descriptor_pool_with_capacity(r, material_capacity, &new_pool)) {
        return false;
    }

    VkDescriptorSet new_skydome_sets[MAX_FRAMES_IN_FLIGHT] = {VK_NULL_HANDLE};
    if (r->skydome_descriptor_set_layout != VK_NULL_HANDLE &&
        !allocate_descriptor_sets_from_pool(r, new_pool, r->skydome_descriptor_set_layout,
                                            new_skydome_sets,
                                            "Failed to allocate skydome descriptor sets")) {
        vkDestroyDescriptorPool(r->device, new_pool, NULL);
        return false;
    }

    VkDescriptorSet *new_material_sets = NULL;
    if (material_count > 0) {
        size_t total_sets = (size_t)material_count * MAX_FRAMES_IN_FLIGHT;
        new_material_sets = calloc(total_sets, sizeof(*new_material_sets));
        if (!new_material_sets) {
            vkDestroyDescriptorPool(r->device, new_pool, NULL);
            vulkan_renderer_set_error(r, VK_ERROR_OUT_OF_HOST_MEMORY, "calloc",
                                      "Failed to allocate descriptor set handles");
            return false;
        }

        for (uint32_t m = 0; m < material_count; m++) {
            if (!allocate_descriptor_sets_from_pool(
                    r, new_pool, r->descriptor_set_layout,
                    &new_material_sets[m * MAX_FRAMES_IN_FLIGHT],
                    "Failed to allocate material descriptor sets")) {
                free(new_material_sets);
                vkDestroyDescriptorPool(r->device, new_pool, NULL);
                return false;
            }
        }
    }

    VkDescriptorPool old_pool = r->descriptor_pool;
    r->descriptor_pool = new_pool;
    r->descriptor_pool_material_capacity = material_capacity;

    if (r->skydome_descriptor_set_layout != VK_NULL_HANDLE) {
        memcpy(r->skydome_descriptor_sets, new_skydome_sets, sizeof(new_skydome_sets));
        update_skydome_descriptor_sets(r, r->skydome_descriptor_sets);
    } else {
        memset(r->skydome_descriptor_sets, 0, sizeof(r->skydome_descriptor_sets));
    }

    for (uint32_t m = 0; m < material_count; m++) {
        memcpy(r->material_gpu[m].descriptor_sets, &new_material_sets[m * MAX_FRAMES_IN_FLIGHT],
               sizeof(r->material_gpu[m].descriptor_sets));
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            r->material_gpu[m].descriptor_sets_dirty[i] = true;
        }
    }

    free(new_material_sets);
    if (old_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(r->device, old_pool, NULL);
    }

    return true;
}

// Ensure per-material GPU resources are allocated for the given material count
static bool ensure_material_gpu(VulkanRenderer *r, const uint32_t material_count) {
    if (r->material_gpu_count >= material_count)
        return true;

    const uint32_t old_material_count = r->material_gpu_count;

    // Grow the array
    MaterialGPUData *new_mats = calloc(material_count, sizeof(MaterialGPUData));
    if (!new_mats) {
        vulkan_renderer_set_error(r, VK_ERROR_OUT_OF_HOST_MEMORY, "calloc",
                                  "Failed to allocate material GPU data");
        return false;
    }
    if (r->material_gpu) {
        memcpy(new_mats, r->material_gpu, old_material_count * sizeof(MaterialGPUData));
        free(r->material_gpu);
    }
    r->material_gpu = new_mats;

    // Allocate fragment UBOs for new materials before rebuilding descriptor sets.
    for (uint32_t m = old_material_count; m < material_count; m++) {
        MaterialGPUData *mat = &r->material_gpu[m];

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            mat->descriptor_sets_dirty[i] = true;
            if (!create_buffer(
                    r, sizeof(FragmentUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    &mat->fragment_uniform_buffers[i], &mat->fragment_uniform_buffer_allocs[i])) {
                for (uint32_t cleanup_idx = old_material_count; cleanup_idx <= m; cleanup_idx++) {
                    cleanup_material_fragment_uniform_buffers(r, &r->material_gpu[cleanup_idx]);
                }
                return false;
            }
        }
    }

    const uint32_t new_capacity =
        next_material_descriptor_capacity(r->descriptor_pool_material_capacity, material_count);
    if (!rebuild_material_descriptor_pool(r, material_count, new_capacity)) {
        for (uint32_t m = old_material_count; m < material_count; m++) {
            cleanup_material_fragment_uniform_buffers(r, &r->material_gpu[m]);
        }
        return false;
    }

    r->material_gpu_count = material_count;
    return true;
}

bool vulkan_renderer_render(VulkanRenderer *r, const Mesh *mesh, mat4 *mvp, mat4 *model,
                            const RenderMaterial *materials, uint32_t material_count,
                            bool enable_lighting, const vec3 camera_pos, bool use_triplanar_mapping,
                            const mat4 *bone_matrices, uint32_t bone_count, mat4 *view,
                            mat4 *projection, const uint8_t **out_framebuffer) {
    vulkan_renderer_clear_error(r);
    *out_framebuffer = NULL;

    VkResult vk_result =
        vkWaitForFences(r->device, 1, &r->in_flight_fences[r->current_frame], VK_TRUE, UINT64_MAX);
    if (vk_result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, vk_result, "vkWaitForFences",
                                  "Failed to wait for in-flight fence");
        return false;
    }

    // Read framebuffer from the frame that just completed
    const uint8_t *result = NULL;
    uint32_t ready_staging_idx = r->frame_staging_buffers[r->current_frame];

    if (r->frame_ready[r->current_frame]) {
        VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = r->staging_buffer_allocs[ready_staging_idx].memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        vk_result = vkInvalidateMappedMemoryRanges(r->device, 1, &range);
        if (vk_result != VK_SUCCESS) {
            vulkan_renderer_set_error(r, vk_result, "vkInvalidateMappedMemoryRanges",
                                      "Failed to invalidate staging buffer memory");
            return false;
        }
        result = (const uint8_t *)r->staging_buffer_allocs[ready_staging_idx].mapped;
    }

    vk_result = vkResetFences(r->device, 1, &r->in_flight_fences[r->current_frame]);
    if (vk_result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, vk_result, "vkResetFences", "Failed to reset in-flight fence");
        return false;
    }
    r->frame_ready[r->current_frame] = false;

    r->current_staging_buffer = (r->current_staging_buffer + 1) % NUM_STAGING_BUFFERS;
    uint32_t write_staging_idx = r->current_staging_buffer;
    r->frame_staging_buffers[r->current_frame] = write_staging_idx;

    // Ensure material GPU resources
    if (material_count == 0)
        material_count = 1;
    if (!ensure_material_gpu(r, material_count)) {
        return false;
    }

    // Upload textures for each material
    for (uint32_t m = 0; m < material_count; m++) {
        if (!update_material_texture(r, &r->material_gpu[m], materials[m].diffuse,
                                     materials[m].normal)) {
            return false;
        }
    }

    // Update vertex/index buffers
    if (r->cached_mesh_generation != mesh->generation || r->vertex_buffer == VK_NULL_HANDLE) {
        if (!update_vertex_buffer(r, &mesh->vertices) || !update_index_buffer(r, &mesh->indices)) {
            return false;
        }
        r->cached_mesh_generation = mesh->generation;
    }

    // Update descriptor sets for each material
    for (uint32_t m = 0; m < material_count; m++) {
        MaterialGPUData *mat = &r->material_gpu[m];
        if (mat->descriptor_sets_dirty[r->current_frame]) {
            VkDescriptorBufferInfo uniform_info = {r->uniform_buffers[r->current_frame], 0,
                                                   sizeof(Uniforms)};
            VkDescriptorImageInfo diffuse_info = {r->sampler, mat->diffuse_image_view,
                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkDescriptorImageInfo normal_info = {r->sampler, mat->normal_image_view,
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkDescriptorBufferInfo frag_uniform_info = {
                mat->fragment_uniform_buffers[r->current_frame], 0, sizeof(FragmentUniforms)};

            VkWriteDescriptorSet writes[4] = {
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL,
                 mat->descriptor_sets[r->current_frame], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 NULL, &uniform_info, NULL},
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL,
                 mat->descriptor_sets[r->current_frame], 1, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &diffuse_info, NULL, NULL},
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL,
                 mat->descriptor_sets[r->current_frame], 2, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &normal_info, NULL, NULL},
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL,
                 mat->descriptor_sets[r->current_frame], 3, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 NULL, &frag_uniform_info, NULL}};

            vkUpdateDescriptorSets(r->device, 4, writes, 0, NULL);
            mat->descriptor_sets_dirty[r->current_frame] = false;
        }
    }

    // Prepare push constants
    PushConstants push_constants;
    glm_mat4_copy(*mvp, push_constants.mvp);
    glm_mat4_copy(*model, push_constants.model);

    // Prepare vertex uniform buffer (bone matrices — shared across all materials)
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

    // Prepare per-material fragment uniforms
    for (uint32_t m = 0; m < material_count; m++) {
        FragmentUniforms frag_uniforms = {0};
        glm_vec3_copy(r->normalized_light_dir, frag_uniforms.light_dir);
        frag_uniforms.enable_lighting = enable_lighting ? 1 : 0;
        glm_vec3_copy((float *)camera_pos, frag_uniforms.camera_pos);
        frag_uniforms.use_triplanar_mapping = use_triplanar_mapping ? 1 : 0;
        frag_uniforms.alpha_cutoff = 0.5f;
        frag_uniforms.specular_strength = materials[m].specular_strength;
        frag_uniforms.shininess = materials[m].shininess;
        frag_uniforms.use_diffuse_alpha_as_luster =
            materials[m].use_diffuse_alpha_as_luster ? 1 : 0;

        // Base/diffuse color factor
        memcpy(frag_uniforms.base_color, materials[m].base_color, sizeof(float) * 4);

        // Hemisphere ambient lighting
        glm_vec4_copy((vec4){0.50f, 0.50f, 0.52f, 0.0f}, frag_uniforms.hemisphere_sky_color);
        glm_vec4_copy((vec4){0.18f, 0.16f, 0.14f, 0.0f}, frag_uniforms.hemisphere_ground_color);

        // Fill/rim are derived from key light direction so camera-linked key
        // lighting remains visually obvious while orbiting.
        vec3 fill_dir = {-frag_uniforms.light_dir[0], -frag_uniforms.light_dir[1] - 0.2f,
                         -frag_uniforms.light_dir[2]};
        glm_vec3_normalize(fill_dir);
        glm_vec4_copy((vec4){fill_dir[0], fill_dir[1], fill_dir[2], 0.18f},
                      frag_uniforms.fill_light_dir);

        vec3 rim_dir = {-frag_uniforms.light_dir[0], -frag_uniforms.light_dir[1],
                        -frag_uniforms.light_dir[2]};
        glm_vec3_normalize(rim_dir);
        glm_vec4_copy((vec4){rim_dir[0], rim_dir[1], rim_dir[2], 0.22f},
                      frag_uniforms.rim_light_dir);

        switch (materials[m].alpha_mode) {
        case ALPHA_MODE_MASK:
            frag_uniforms.alpha_mode = 1;
            break;
        case ALPHA_MODE_BLEND:
            frag_uniforms.alpha_mode = 2;
            break;
        default:
            frag_uniforms.alpha_mode = 0;
            break;
        }

        memcpy(r->material_gpu[m].fragment_uniform_buffer_allocs[r->current_frame].mapped,
               &frag_uniforms, sizeof(FragmentUniforms));
    }

    VkCommandBuffer cmd = r->command_buffers[r->current_frame];
    vk_result = vkResetCommandBuffer(cmd, 0);
    if (vk_result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, vk_result, "vkResetCommandBuffer",
                                  "Failed to reset render command buffer");
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vk_result = vkBeginCommandBuffer(cmd, &begin_info);
    if (vk_result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, vk_result, "vkBeginCommandBuffer",
                                  "Failed to begin render command buffer");
        return false;
    }

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
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->skydome_pipeline_layout, 0,
                                1, &r->skydome_descriptor_sets[r->current_frame], 0, NULL);

        mat4 sky_view;
        glm_mat4_copy(*view, sky_view);
        sky_view[3][0] = sky_view[3][1] = sky_view[3][2] = 0.0f;

        mat4 sky_mvp;
        glm_mat4_mul(*projection, sky_view, sky_mvp);
        vkCmdPushConstants(cmd, r->skydome_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(mat4), sky_mvp);

        VkBuffer vb[] = {r->skydome_vertex_buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, r->skydome_index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, (uint32_t)r->skydome_mesh->indices.count, 1, 0, 0, 0);
    }

    // Render main model
    VkPipeline active_pipeline =
        get_wireframe_mode(&r->wireframe_mode) ? r->wireframe_pipeline : r->graphics_pipeline;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, active_pipeline);
    vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(PushConstants), &push_constants);

    VkBuffer vbs[] = {r->vertex_buffer};
    VkDeviceSize vb_offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbs, vb_offsets);
    vkCmdBindIndexBuffer(cmd, r->index_buffer, 0, VK_INDEX_TYPE_UINT32);

    if (mesh->submeshes.count > 0) {
        for (size_t i = 0; i < mesh->submeshes.count; i++) {
            const SubMesh *sm = &mesh->submeshes.data[i];
            uint32_t mat_idx = sm->material_index < material_count ? sm->material_index : 0;
            if (materials[mat_idx].alpha_mode == ALPHA_MODE_BLEND)
                continue;

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                                    &r->material_gpu[mat_idx].descriptor_sets[r->current_frame], 0,
                                    NULL);
            vkCmdDrawIndexed(cmd, sm->index_count, 1, sm->index_offset, 0, 0);
        }

        bool has_blend = false;
        for (size_t i = 0; i < mesh->submeshes.count; i++) {
            const SubMesh *sm = &mesh->submeshes.data[i];
            uint32_t mat_idx = sm->material_index < material_count ? sm->material_index : 0;
            if (materials[mat_idx].alpha_mode != ALPHA_MODE_BLEND)
                continue;

            if (!has_blend) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->blend_pipeline);
                vkCmdPushConstants(cmd, r->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                   sizeof(PushConstants), &push_constants);
                vkCmdBindVertexBuffers(cmd, 0, 1, vbs, vb_offsets);
                vkCmdBindIndexBuffer(cmd, r->index_buffer, 0, VK_INDEX_TYPE_UINT32);
                has_blend = true;
            }

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                                    &r->material_gpu[mat_idx].descriptor_sets[r->current_frame], 0,
                                    NULL);
            vkCmdDrawIndexed(cmd, sm->index_count, 1, sm->index_offset, 0, 0);
        }
    } else {
        // Fallback: single draw for the whole mesh
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_layout, 0, 1,
                                &r->material_gpu[0].descriptor_sets[r->current_frame], 0, NULL);
        vkCmdDrawIndexed(cmd, (uint32_t)mesh->indices.count, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);

    // Copy color image to staging buffer for CPU readback
    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = r->width;
    region.imageExtent.height = r->height;
    region.imageExtent.depth = 1;

    vkCmdCopyImageToBuffer(cmd, r->color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           r->staging_buffers[write_staging_idx], 1, &region);

    VkBufferMemoryBarrier buffer_barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.buffer = r->staging_buffers[write_staging_idx];
    buffer_barrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                         NULL, 1, &buffer_barrier, 0, NULL);

    vk_result = vkEndCommandBuffer(cmd);
    if (vk_result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, vk_result, "vkEndCommandBuffer",
                                  "Failed to end render command buffer");
        return false;
    }

    VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    vk_result =
        vkQueueSubmit(r->graphics_queue, 1, &submit_info, r->in_flight_fences[r->current_frame]);
    if (vk_result != VK_SUCCESS) {
        vulkan_renderer_set_error(r, vk_result, "vkQueueSubmit",
                                  "Failed to submit render command buffer");
        return false;
    }

    r->frame_ready[r->current_frame] = true;
    r->current_frame = (r->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;

    *out_framebuffer = result;
    return true;
}
