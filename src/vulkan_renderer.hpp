#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <optional>
#include <array>
#include <atomic>
#include "model.hpp"

struct Texture;

struct PushConstants {
    glm::mat4 mvp;
    glm::mat4 model;
};

struct Uniforms {
    glm::mat4 boneMatrices[MAX_BONES];
    uint32_t hasAnimation;
    uint32_t padding[3];
};

struct FragmentUniforms {
    glm::vec3 lightDir;
    uint32_t enableLighting;
    glm::vec3 cameraPos;
    float fogStart;
    glm::vec3 fogColor;
    float fogEnd;
    uint32_t useTriplanarMapping;
    uint32_t alphaMode;
    float alphaCutoff;
    float padding;
};

class VulkanRenderer {
public:
    VulkanRenderer(uint32_t width, uint32_t height);
    ~VulkanRenderer();
    
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    
    bool initialize();
    void resize(uint32_t width, uint32_t height);
    void setLightDirection(const glm::vec3& direction);
    void setWireframeMode(bool enabled);
    bool getWireframeMode() const { return wireframeMode_.load(); }
    
    // Renders the scene asynchronously and returns the most recent completed frame
    // Returns nullptr if no frames have completed yet
    const uint8_t* render(
        const Mesh& mesh,
        const glm::mat4& mvp,
        const glm::mat4& model,
        const Texture& diffuseTexture,
        const Texture& normalTexture,
        bool enableLighting,
        const glm::vec3& cameraPos,
        bool useTriplanarMapping = false,
        AlphaMode alphaMode = AlphaMode::Opaque,
        const glm::mat4* boneMatrices = nullptr,
        uint32_t boneCount = 0,
        const glm::mat4* view = nullptr,
        const glm::mat4* projection = nullptr
    );
    
    // Enable/disable skydome rendering and set its texture
    void setSkydome(const Mesh* skydomeMesh, const Texture* skydomeTexture);
    
    // Wait for all pending frames to complete
    void waitIdle();

    size_t getFrameSize() const { return width_ * height_ * 4; }
    
private:
    static const int MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t currentFrame_ = 0;

    uint32_t width_;
    uint32_t height_;
    glm::vec3 normalizedLightDir_;
    
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;
    
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkPipeline wireframePipeline_ = VK_NULL_HANDLE;
    std::atomic<bool> wireframeMode_{false};
    
    // Skydome pipeline and resources
    VkDescriptorSetLayout skydomeDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout skydomePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline skydomePipeline_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> skydomeDescriptorSets_;
    const Mesh* skydomeMesh_ = nullptr;
    const Texture* skydomeTexture_ = nullptr;
    
    VkImage skydomeImage_ = VK_NULL_HANDLE;
    VmaAllocation skydomeImageAllocation_ = VK_NULL_HANDLE;
    VkImageView skydomeImageView_ = VK_NULL_HANDLE;
    const void* cachedSkydomeDataPtr_ = nullptr;
    
    // Command Buffers and Sync Objects
    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkFence> inFlightFences_;

    // Render targets
    VkImage colorImage_ = VK_NULL_HANDLE;
    VmaAllocation colorImageAllocation_ = VK_NULL_HANDLE;
    VkImageView colorImageView_ = VK_NULL_HANDLE;
    
    VkImage depthImage_ = VK_NULL_HANDLE;
    VmaAllocation depthImageAllocation_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    
    // Staging buffers for readback (per frame)
    std::vector<VkBuffer> stagingBuffers_;
    std::vector<VmaAllocation> stagingBufferAllocations_;
    std::vector<void*> mappedDatas_;
    
    // CPU-side readback buffers for async access
    std::vector<std::vector<uint8_t>> readbackBuffers_;
    std::vector<bool> frameReady_;
    
    // Uniform buffers (per frame)
    std::vector<VkBuffer> uniformBuffers_;
    std::vector<VmaAllocation> uniformBufferAllocations_;
    std::vector<void*> uniformBuffersMapped_;

    std::vector<VkBuffer> fragmentUniformBuffers_;
    std::vector<VmaAllocation> fragmentUniformBufferAllocations_;
    std::vector<void*> fragmentUniformBuffersMapped_;

    // Per-frame Descriptor Sets
    std::vector<VkDescriptorSet> descriptorSets_;
    std::vector<bool> descriptorSetsDirty_;
    
    // Textures
    VkImage diffuseImage_ = VK_NULL_HANDLE;
    VmaAllocation diffuseImageAllocation_ = VK_NULL_HANDLE;
    VkImageView diffuseImageView_ = VK_NULL_HANDLE;
    uint32_t cachedDiffuseWidth_ = 0;
    uint32_t cachedDiffuseHeight_ = 0;
    
    VkImage normalImage_ = VK_NULL_HANDLE;
    VmaAllocation normalImageAllocation_ = VK_NULL_HANDLE;
    VkImageView normalImageView_ = VK_NULL_HANDLE;
    uint32_t cachedNormalWidth_ = 0;
    uint32_t cachedNormalHeight_ = 0;
    
    VkSampler sampler_ = VK_NULL_HANDLE;
    
    // Cached vertex/index buffers
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation vertexBufferAllocation_ = VK_NULL_HANDLE;
    size_t cachedVertexCount_ = 0;
    
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation indexBufferAllocation_ = VK_NULL_HANDLE;
    size_t cachedIndexCount_ = 0;
    
    // Skydome vertex/index buffers
    VkBuffer skydomeVertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation skydomeVertexBufferAllocation_ = VK_NULL_HANDLE;
    VkBuffer skydomeIndexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation skydomeIndexBufferAllocation_ = VK_NULL_HANDLE;

    // Cached generation
    uint64_t cachedMeshGeneration_ = 0;
    const void* cachedDiffuseDataPtr_ = nullptr;
    const void* cachedNormalDataPtr_ = nullptr;

    std::string shaderDirectory_;
    
    // Helper functions
    bool createInstance();
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createAllocator();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    bool createDescriptorPool();
    bool createDescriptorSetLayout();
    bool createPipelineLayout();
    bool createRenderPass();
    bool createGraphicsPipeline();
    bool createSkydomePipeline();
    bool createRenderTargets();
    bool createFramebuffer();
    bool createStagingBuffers();
    bool createUniformBuffers();
    bool createDescriptorSets();
    bool createSampler();
    
    void cleanupRenderTargets();
    void cleanup();
    
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, 
                      VmaMemoryUsage memoryUsage, VkBuffer& buffer, 
                      VmaAllocation& allocation, VmaAllocationInfo* outAllocInfo = nullptr);
    void createImage(uint32_t width, uint32_t height, VkFormat format,
                     VkImageUsageFlags usage, VmaMemoryUsage memoryUsage,
                     VkImage& image, VmaAllocation& allocation);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    
    void transitionImageLayout(VkImage image,
                               VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    
    void updateDiffuseTexture(const Texture& texture);
    void updateNormalTexture(const Texture& texture);
    void updateSkydomeTexture(const Texture& texture);
    void updateVertexBuffer(const std::vector<Vertex>& vertices);
    void updateIndexBuffer(const std::vector<uint32_t>& indices);
    
    std::vector<char> readShaderFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);
};
