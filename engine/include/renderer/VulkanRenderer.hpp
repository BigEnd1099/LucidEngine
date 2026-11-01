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

        void createComputePipelines();
        void setStandaloneGameMode(bool enabled) { m_standaloneGameMode = enabled; }
        bool isStandaloneGameMode() const { return m_standaloneGameMode; }

    private:
        void createOffscreenTarget();
        void createCommandBuffer();
        void createGraphicsPipeline();
        void initCaptureResources();
        void cleanupCaptureResources();

#ifdef USE_GLFW
        void initImGui();
        void renderImGui(VkCommandBuffer cmd, VkImageView targetView, ecs::Registry& registry, core::AssetManager* assetManager, core::Engine* engine);
#endif

        VulkanContext* m_context;
        uint32_t m_width;
        uint32_t m_height;

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

        VkCommandBuffer m_commandBuffer{VK_NULL_HANDLE};

        // Persistent capture staging buffer & command buffer
        VkBuffer m_captureStagingBuffer{VK_NULL_HANDLE};
        VmaAllocation m_captureStagingAlloc{VK_NULL_HANDLE};
        void* m_captureMappedPtr{nullptr};
        VkCommandBuffer m_captureCommandBuffer{VK_NULL_HANDLE};

#ifdef USE_GLFW
        // Swapchain sync and state
        VkSemaphore m_imageAvailableSemaphore{VK_NULL_HANDLE};
        VkSemaphore m_renderFinishedSemaphore{VK_NULL_HANDLE};
        VkFence m_inFlightFence{VK_NULL_HANDLE};
        uint32_t m_currentFrameIndex{0};
        float m_viewportAspectRatio{16.0f / 9.0f};
        float m_uiScale{1.0f};
        bool m_standaloneGameMode{false};
#endif
    };
}
