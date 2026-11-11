#pragma once
#include "renderer/VulkanContext.hpp"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace engine::renderer {

    struct alignas(16) InstanceCullData {
        glm::mat4 modelMatrix{1.0f};
        glm::vec4 boundingSphere{0.0f, 0.0f, 0.0f, 1.0f}; // xyz = center, w = radius
        uint32_t indexCount{0};
        uint32_t instanceCount{1};
        uint32_t firstIndex{0};
        int32_t  vertexOffset{0};
        uint32_t firstInstance{0};
        uint32_t padding[3]{0, 0, 0};
    };
    static_assert(sizeof(InstanceCullData) == 112, "InstanceCullData size mismatch");

    struct DrawIndexedIndirectCommand {
        uint32_t indexCount{0};
        uint32_t instanceCount{0};
        uint32_t firstIndex{0};
        int32_t  vertexOffset{0};
        uint32_t firstInstance{0};
    };

    class GpuCulling {
    public:
        static constexpr uint32_t MAX_INDIRECT_INSTANCES = 65536;

        GpuCulling() = default;
        ~GpuCulling();

        void init(VulkanContext* context, VkDescriptorPool descriptorPool, uint32_t width, uint32_t height);
        void resize(uint32_t width, uint32_t height);
        void cleanup();

        // Downsample depth buffer into Hi-Z pyramid
        void buildHiZPyramid(VkCommandBuffer cmd, VkImage depthImage, VkImageView depthImageView, uint32_t width, uint32_t height);

        // Run GPU Frustum & Hi-Z Occlusion Culling
        void dispatchCull(VkCommandBuffer cmd,
                          const glm::mat4& viewProj,
                          const glm::vec3& cameraPos,
                          uint32_t totalInstances,
                          bool enableHiZ);

        VkBuffer getIndirectCommandBuffer() const { return m_indirectCommandBuffer; }
        VkBuffer getCountBuffer() const { return m_countBuffer; }
        VkBuffer getInstanceBuffer() const { return m_instanceBuffer; }
        void* getMappedInstanceData() { return m_instanceMappedPtr; }

        VkImageView getHiZImageView() const { return m_hiZImageView; }
        VkSampler getHiZSampler() const { return m_hiZSampler; }
        bool isInitialized() const { return m_initialized; }

        // CPU algorithm evaluation for unit testing
        static bool testFrustumSphere(const glm::mat4& viewProj, const glm::vec3& center, float radius);

    private:
        void createHiZResources(uint32_t width, uint32_t height);
        void createBuffers();
        void createPipelines();

        VulkanContext* m_context{nullptr};
        VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
        uint32_t m_width{1920};
        uint32_t m_height{1080};
        uint32_t m_hiZMipLevels{1};

        // Hi-Z Image and Mip Views
        VkImage m_hiZImage{VK_NULL_HANDLE};
        VmaAllocation m_hiZAlloc{VK_NULL_HANDLE};
        VkImageView m_hiZImageView{VK_NULL_HANDLE};
        std::vector<VkImageView> m_hiZMipViews;
        VkSampler m_hiZSampler{VK_NULL_HANDLE};

        // Buffers
        VkBuffer m_instanceBuffer{VK_NULL_HANDLE};
        VmaAllocation m_instanceAlloc{VK_NULL_HANDLE};
        void* m_instanceMappedPtr{nullptr};

        VkBuffer m_indirectCommandBuffer{VK_NULL_HANDLE};
        VmaAllocation m_indirectCommandAlloc{VK_NULL_HANDLE};

        VkBuffer m_countBuffer{VK_NULL_HANDLE};
        VmaAllocation m_countAlloc{VK_NULL_HANDLE};

        // Pipelines
        VkDescriptorSetLayout m_cullSetLayout{VK_NULL_HANDLE};
        VkDescriptorSet m_cullDescriptorSet{VK_NULL_HANDLE};
        VkPipelineLayout m_cullPipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_cullPipeline{VK_NULL_HANDLE};

        VkDescriptorSetLayout m_hizReduceSetLayout{VK_NULL_HANDLE};
        VkPipelineLayout m_hizReducePipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_hizReducePipeline{VK_NULL_HANDLE};

        bool m_initialized{false};
    };

} // namespace engine::renderer
