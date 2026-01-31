#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <optional>
#include <array>
#include "model.hpp"

struct Texture;

struct Uniforms {
    glm::mat4 mvp;
    glm::mat4 model;
};

struct FragmentUniforms {
    glm::vec3 lightDir;
    uint32_t enableLighting;
    glm::vec3 cameraPos;
    float fogStart;
    glm::vec3 fogColor;
    float fogEnd;
    uint32_t useTriplanarMapping;
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
    
    // Renders the scene and returns a pointer to the pixel data (RGBA)
    // The pointer is valid until the next render call or resize
    const uint8_t* render(
        const Mesh& mesh,
        const glm::mat4& mvp,
        const glm::mat4& model,
        const Texture& diffuseTexture,
        const Texture& normalTexture,
        bool enableLighting,
        const glm::vec3& cameraPos,
        bool useTriplanarMapping = false
    );

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
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;
    
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkPipeline wireframePipeline_ = VK_NULL_HANDLE;
    bool wireframeMode_ = false;
    
    // Command Buffers and Sync Objects
    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkFence> inFlightFences_;

    // Render targets
    VkImage colorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory colorImageMemory_ = VK_NULL_HANDLE;
    VkImageView colorImageView_ = VK_NULL_HANDLE;
    
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    
    // Staging buffers for readback (per frame)
    std::vector<VkBuffer> stagingBuffers_;
    std::vector<VkDeviceMemory> stagingBufferMemories_;
    std::vector<void*> mappedDatas_;
    
    // Uniform buffers (per frame)
    std::vector<VkBuffer> uniformBuffers_;
    std::vector<VkDeviceMemory> uniformBufferMemories_;
    std::vector<void*> uniformBuffersMapped_;

    std::vector<VkBuffer> fragmentUniformBuffers_;
    std::vector<VkDeviceMemory> fragmentUniformBufferMemories_;
    std::vector<void*> fragmentUniformBuffersMapped_;

    // Per-frame Descriptor Sets
    std::vector<VkDescriptorSet> descriptorSets_;
    
    // Textures
    VkImage diffuseImage_ = VK_NULL_HANDLE;
    VkDeviceMemory diffuseImageMemory_ = VK_NULL_HANDLE;
    VkImageView diffuseImageView_ = VK_NULL_HANDLE;
    uint32_t cachedDiffuseWidth_ = 0;
    uint32_t cachedDiffuseHeight_ = 0;
    
    VkImage normalImage_ = VK_NULL_HANDLE;
    VkDeviceMemory normalImageMemory_ = VK_NULL_HANDLE;
    VkImageView normalImageView_ = VK_NULL_HANDLE;
    uint32_t cachedNormalWidth_ = 0;
    uint32_t cachedNormalHeight_ = 0;
    
    VkSampler sampler_ = VK_NULL_HANDLE;
    
    // Cached vertex/index buffers
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory_ = VK_NULL_HANDLE;
    size_t cachedVertexCount_ = 0;
    
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory_ = VK_NULL_HANDLE;
    size_t cachedIndexCount_ = 0;

    // Cached generation
    uint64_t cachedMeshGeneration_ = 0;
    const void* cachedDiffuseDataPtr_ = nullptr;
    const void* cachedNormalDataPtr_ = nullptr;
    
    // Helper functions
    bool createInstance();
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    bool createDescriptorPool();
    bool createDescriptorSetLayout();
    bool createPipelineLayout();
    bool createRenderPass();
    bool createGraphicsPipeline();
    bool createRenderTargets();
    bool createFramebuffer();
    bool createStagingBuffers();
    bool createUniformBuffers();
    bool createDescriptorSets();
    bool createSampler();
    
    void cleanupRenderTargets();
    void cleanup();
    
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, 
                      VkMemoryPropertyFlags properties, VkBuffer& buffer, 
                      VkDeviceMemory& bufferMemory);
    void createImage(uint32_t width, uint32_t height, VkFormat format,
                     VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                     VkImage& image, VkDeviceMemory& imageMemory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    
    void transitionImageLayout(VkImage image, VkFormat format,
                               VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    
    void updateDiffuseTexture(const Texture& texture);
    void updateNormalTexture(const Texture& texture);
    void updateVertexBuffer(const std::vector<Vertex>& vertices);
    void updateIndexBuffer(const std::vector<uint32_t>& indices);
    void updateDescriptorSet();
    
    std::vector<char> readShaderFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);
};
