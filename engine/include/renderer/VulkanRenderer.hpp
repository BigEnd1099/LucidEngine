#pragma once
#include "renderer/VulkanContext.hpp"
#include "ecs/Registry.hpp"
#include <vector>
#include <string>

namespace engine {
    namespace core {
        class AssetManager;
        class Engine;
    }
}

namespace engine::renderer {
    class VulkanRenderer {
    public:
        VulkanRenderer(VulkanContext* context, uint32_t width, uint32_t height);
        ~VulkanRenderer();

        void render(ecs::Registry& registry, core::AssetManager* assetManager = nullptr, glm::mat4 viewMatrix = glm::mat4(1.0f), core::Engine* engine = nullptr);
        std::string captureFrameBase64();
        void submitBufferBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size);

        void setRenderResolution(uint32_t width, uint32_t height);
        uint32_t getWidth() const { return m_width; }
        uint32_t getHeight() const { return m_height; }

        void setResolutionScale(float scale);
        float getResolutionScale() const { return m_resolutionScale; }

        void setUiScale(float scale) { m_uiScale = std::clamp(scale, 0.5f, 3.0f); }
        float getUiScale() const { return m_uiScale; }

        void createComputePipelines();
        void setStandaloneGameMode(bool enabled) { m_standaloneGameMode = enabled; }
        bool isStandaloneGameMode() const { return m_standaloneGameMode; }

    private:
        void createOffscreenTarget();
        void cleanupOffscreenTarget();
        void createCommandBuffer();
        void createShadowResources();
        void createShadowPipeline();
        void createDescriptorSets();
        void createGraphicsPipeline();
        void initCaptureResources();
        void cleanupCaptureResources();
        void cleanupShadowResources();
        void createParticlePipeline();
        void cleanupParticlePipeline();

#ifdef USE_GLFW
        void initImGui();
        void renderImGui(VkCommandBuffer cmd, VkImageView targetView, ecs::Registry& registry, core::AssetManager* assetManager, core::Engine* engine);
#endif

        VulkanContext* m_context;
        uint32_t m_width;
        uint32_t m_height;
        uint32_t m_baseWidth{1920};
        uint32_t m_baseHeight{1080};
        float m_resolutionScale{1.0f};
        float m_uiScale{1.0f};

        VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_graphicsPipeline{VK_NULL_HANDLE};

        VkImage m_colorImage{VK_NULL_HANDLE};
        VmaAllocation m_colorAllocation{VK_NULL_HANDLE};
        VkImageView m_colorImageView{VK_NULL_HANDLE};
        VkSampler m_viewportSampler{VK_NULL_HANDLE};
        VkDescriptorSet m_viewportDescriptor{VK_NULL_HANDLE};

        // Semantic Segmentation Buffer (Object ID)
        VkImage m_objectIdImage{VK_NULL_HANDLE};
        VmaAllocation m_objectIdAllocation{VK_NULL_HANDLE};
        VkImageView m_objectIdImageView{VK_NULL_HANDLE};

        VkImage m_depthImage{VK_NULL_HANDLE};
        VmaAllocation m_depthAllocation{VK_NULL_HANDLE};
        VkImageView m_depthImageView{VK_NULL_HANDLE};

        // Directional Cascaded Shadow Mapping (UE5 Style)
        uint32_t m_shadowWidth{2048};
        uint32_t m_shadowHeight{2048};
        VkImage m_shadowImage{VK_NULL_HANDLE};
        VmaAllocation m_shadowAllocation{VK_NULL_HANDLE};
        VkImageView m_shadowImageView{VK_NULL_HANDLE};
        VkSampler m_shadowSampler{VK_NULL_HANDLE};
        VkPipelineLayout m_shadowPipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_shadowPipeline{VK_NULL_HANDLE};

        // PBR Descriptor Sets (Shadow Map + Scene UBO)
        struct SceneUboData {
            glm::vec4 sunDirection{0.55f, 0.85f, 0.40f, 3.4f};
            glm::vec4 sunColor{1.0f, 0.97f, 0.92f, 1.0f};
            glm::vec4 ambientColor{0.15f, 0.22f, 0.35f, 1.0f};
            glm::vec4 fogParams{0.0f, 20.0f, 100.0f, 0.0f};
            glm::vec4 fogColor{0.06f, 0.07f, 0.09f, 1.0f};
            glm::vec4 pointLightsPos[8]{};
            glm::vec4 pointLightsCol[8]{};
            int numPointLights{0};
            float padding[3]{0.0f, 0.0f, 0.0f};
        };

        VkBuffer m_sceneUboBuffer{VK_NULL_HANDLE};
        VmaAllocation m_sceneUboAlloc{VK_NULL_HANDLE};
        void* m_sceneUboMappedPtr{nullptr};

        VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
        VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
        VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};

        VkCommandBuffer m_commandBuffer{VK_NULL_HANDLE};

        // Persistent capture staging buffer & command buffer
        VkBuffer m_captureStagingBuffer{VK_NULL_HANDLE};
        VmaAllocation m_captureStagingAlloc{VK_NULL_HANDLE};
        void* m_captureMappedPtr{nullptr};
        VkCommandBuffer m_captureCommandBuffer{VK_NULL_HANDLE};

        // Niagara-Style Particle Pipeline
        struct ParticleVertex {
            glm::vec3 pos;
            glm::vec4 color;
            float size;
        };
        VkPipelineLayout m_particlePipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_particlePipeline{VK_NULL_HANDLE};
        VkBuffer m_particleVertexBuffer{VK_NULL_HANDLE};
        VmaAllocation m_particleVertexAlloc{VK_NULL_HANDLE};
        void* m_particleMappedPtr{nullptr};
        static constexpr size_t MAX_PARTICLES = 16384;

#ifdef USE_GLFW
        // Swapchain sync and state
        VkSemaphore m_imageAvailableSemaphore{VK_NULL_HANDLE};
        VkSemaphore m_renderFinishedSemaphore{VK_NULL_HANDLE};
        VkFence m_inFlightFence{VK_NULL_HANDLE};
        uint32_t m_currentFrameIndex{0};
        float m_viewportAspectRatio{16.0f / 9.0f};
        bool m_standaloneGameMode{false};
#endif
    };
}
