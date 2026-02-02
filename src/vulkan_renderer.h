#ifndef DCAT_VULKAN_RENDERER_H
#define DCAT_VULKAN_RENDERER_H

#include <vulkan/vulkan.h>
#include <cglm/cglm.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "types.h"
#include "model.h"
#include "texture.h"

#define MAX_FRAMES_IN_FLIGHT 2

// Push constants for vertex shader
typedef struct PushConstants {
    mat4 mvp;
    mat4 model;
} PushConstants;

// Uniform buffer for vertex shader
typedef struct Uniforms {
    mat4 bone_matrices[MAX_BONES];
    uint32_t has_animation;
    uint32_t padding[3];
} Uniforms;

// Uniform buffer for fragment shader
typedef struct FragmentUniforms {
    vec3 light_dir;
    uint32_t enable_lighting;
    vec3 camera_pos;
    float fog_start;
    vec3 fog_color;
    float fog_end;
    uint32_t use_triplanar_mapping;
    uint32_t alpha_mode;
    float alpha_cutoff;
    float padding;
} FragmentUniforms;

// Memory allocation helper
typedef struct VulkanAllocation {
    VkDeviceMemory memory;
    VkDeviceSize offset;
    VkDeviceSize size;
    void* mapped;
} VulkanAllocation;

// Vulkan Renderer struct
typedef struct VulkanRenderer {
    uint32_t width;
    uint32_t height;
    vec3 normalized_light_dir;
    
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    uint32_t graphics_queue_family;
    
    VkPhysicalDeviceMemoryProperties mem_properties;
    
    VkCommandPool command_pool;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout pipeline_layout;
    VkRenderPass render_pass;
    VkPipeline graphics_pipeline;
    VkPipeline wireframe_pipeline;
    atomic_bool wireframe_mode;
    
    // Skydome
    VkDescriptorSetLayout skydome_descriptor_set_layout;
    VkPipelineLayout skydome_pipeline_layout;
    VkPipeline skydome_pipeline;
    VkDescriptorSet skydome_descriptor_sets[MAX_FRAMES_IN_FLIGHT];
    const Mesh* skydome_mesh;
    const Texture* skydome_texture;
    
    VkImage skydome_image;
    VulkanAllocation skydome_image_alloc;
    VkImageView skydome_image_view;
    const void* cached_skydome_data_ptr;
    
    // Command buffers and sync
    VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];
    VkFence in_flight_fences[MAX_FRAMES_IN_FLIGHT];
    
    // Render targets
    VkImage color_image;
    VulkanAllocation color_image_alloc;
    VkImageView color_image_view;
    
    VkImage depth_image;
    VulkanAllocation depth_image_alloc;
    VkImageView depth_image_view;
    
    VkFramebuffer framebuffer;
    
    // Staging buffers
    VkBuffer staging_buffers[MAX_FRAMES_IN_FLIGHT];
    VulkanAllocation staging_buffer_allocs[MAX_FRAMES_IN_FLIGHT];
    bool frame_ready[MAX_FRAMES_IN_FLIGHT];
    
    // Uniform buffers
    VkBuffer uniform_buffers[MAX_FRAMES_IN_FLIGHT];
    VulkanAllocation uniform_buffer_allocs[MAX_FRAMES_IN_FLIGHT];
    
    VkBuffer fragment_uniform_buffers[MAX_FRAMES_IN_FLIGHT];
    VulkanAllocation fragment_uniform_buffer_allocs[MAX_FRAMES_IN_FLIGHT];
    
    // Descriptor sets
    VkDescriptorSet descriptor_sets[MAX_FRAMES_IN_FLIGHT];
    bool descriptor_sets_dirty[MAX_FRAMES_IN_FLIGHT];
    
    // Textures
    VkImage diffuse_image;
    VulkanAllocation diffuse_image_alloc;
    VkImageView diffuse_image_view;
    uint32_t cached_diffuse_width;
    uint32_t cached_diffuse_height;
    
    VkImage normal_image;
    VulkanAllocation normal_image_alloc;
    VkImageView normal_image_view;
    uint32_t cached_normal_width;
    uint32_t cached_normal_height;
    
    VkSampler sampler;
    
    // Vertex/index buffers
    VkBuffer vertex_buffer;
    VulkanAllocation vertex_buffer_alloc;
    size_t cached_vertex_count;
    
    VkBuffer index_buffer;
    VulkanAllocation index_buffer_alloc;
    size_t cached_index_count;
    
    // Skydome buffers
    VkBuffer skydome_vertex_buffer;
    VulkanAllocation skydome_vertex_buffer_alloc;
    VkBuffer skydome_index_buffer;
    VulkanAllocation skydome_index_buffer_alloc;
    
    // Cache
    uint64_t cached_mesh_generation;
    const void* cached_diffuse_data_ptr;
    const void* cached_normal_data_ptr;
    
    char shader_directory[256];
    uint32_t current_frame;
} VulkanRenderer;

// Create/destroy
VulkanRenderer* vulkan_renderer_create(uint32_t width, uint32_t height);
void vulkan_renderer_destroy(VulkanRenderer* r);

// Initialize
bool vulkan_renderer_initialize(VulkanRenderer* r);

// Resize
void vulkan_renderer_resize(VulkanRenderer* r, uint32_t width, uint32_t height);

// Set light direction
void vulkan_renderer_set_light_direction(VulkanRenderer* r, const float* direction);

// Wireframe mode
void vulkan_renderer_set_wireframe_mode(VulkanRenderer* r, bool enabled);
bool vulkan_renderer_get_wireframe_mode(const VulkanRenderer* r);

// Render and return framebuffer
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
);

// Set skydome
void vulkan_renderer_set_skydome(VulkanRenderer* r, const Mesh* mesh, const Texture* texture);

// Wait for idle
void vulkan_renderer_wait_idle(VulkanRenderer* r);

// Get frame size
static inline size_t vulkan_renderer_get_frame_size(const VulkanRenderer* r) {
    return r->width * r->height * 4;
}

#endif // DCAT_VULKAN_RENDERER_H
