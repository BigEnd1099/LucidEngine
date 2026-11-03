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
        void setAntiAliasingMode(int mode) { m_antiAliasingMode = std::clamp(mode, 0, 3); }
        int getAntiAliasingMode() const { return m_antiAliasingMode; }
        void setShadowDistance(float dist) { m_shadowDistance = std::clamp(dist, 20.0f, 150.0f); }
        float getShadowDistance() const { return m_shadowDistance; }
        void setBloomEnabled(bool enabled) { m_bloomEnabled = enabled; }
        bool isBloomEnabled() const { return m_bloomEnabled; }
        void setBloomIntensity(float intensity) { m_bloomIntensity = std::clamp(intensity, 0.0f, 5.0f); }
        float getBloomIntensity() const { return m_bloomIntensity; }
        void setBloomThreshold(float threshold) { m_bloomThreshold = std::max(0.0f, threshold); }
        float getBloomThreshold() const { return m_bloomThreshold; }
        void setGtaoEnabled(bool enabled) { m_gtaoEnabled = enabled; }
        bool isGtaoEnabled() const { return m_gtaoEnabled; }
        void setGtaoRadius(float radius) { m_gtaoRadius = std::clamp(radius, 0.1f, 5.0f); }
        float getGtaoRadius() const { return m_gtaoRadius; }
        void setGtaoIntensity(float intensity) { m_gtaoIntensity = std::clamp(intensity, 0.0f, 5.0f); }
        float getGtaoIntensity() const { return m_gtaoIntensity; }

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
        void createPointShadowResources();
        void cleanupPointShadowResources();
        void createEnvironmentResources();
        void cleanupEnvironmentResources();
        void loadEnvironmentHdri(int index);
        void createDefaultTextures();
        void cleanupDefaultTextures();
        void uploadTexture2D(VkImage& dstImage, VmaAllocation& dstAlloc, VkImageView& dstView,
                             const void* pixels, uint32_t width, uint32_t height, VkFormat format, bool generateMips);
        void createParticlePipeline();
        void cleanupParticlePipeline();
        void createPostProcessResources();
        void updatePostProcessDescriptorSets();
        void cleanupPostProcessResources();

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
        float m_shadowDistance{48.0f};
        int m_antiAliasingMode{2};

        VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_graphicsPipeline{VK_NULL_HANDLE};

        VkImage m_colorImage{VK_NULL_HANDLE};
        VmaAllocation m_colorAllocation{VK_NULL_HANDLE};
        VkImageView m_colorImageView{VK_NULL_HANDLE};
        VkSampler m_viewportSampler{VK_NULL_HANDLE};
        VkDescriptorSet m_viewportDescriptor{VK_NULL_HANDLE};

        // HDR & Post-Processing (Bloom Dual-Kawase & Tone Mapping)
        bool m_bloomEnabled{true};
        float m_bloomIntensity{0.80f};
        float m_bloomThreshold{1.0f};

        VkImage m_hdrImage{VK_NULL_HANDLE};
        VmaAllocation m_hdrAllocation{VK_NULL_HANDLE};
        VkImageView m_hdrImageView{VK_NULL_HANDLE};

        VkImage m_bloomImageA{VK_NULL_HANDLE};
        VmaAllocation m_bloomAllocationA{VK_NULL_HANDLE};
        VkImageView m_bloomImageViewA{VK_NULL_HANDLE};

        VkImage m_bloomImageB{VK_NULL_HANDLE};
        VmaAllocation m_bloomAllocationB{VK_NULL_HANDLE};
        VkImageView m_bloomImageViewB{VK_NULL_HANDLE};

        VkSampler m_linearSampler{VK_NULL_HANDLE};

        // Ground Truth Ambient Occlusion (GTAO)
        bool m_gtaoEnabled{true};
        float m_gtaoRadius{0.80f};
        float m_gtaoIntensity{1.50f};

        VkImage m_aoImage{VK_NULL_HANDLE};
        VmaAllocation m_aoAllocation{VK_NULL_HANDLE};
        VkImageView m_aoImageView{VK_NULL_HANDLE};

        VkImage m_aoBlurImage{VK_NULL_HANDLE};
        VmaAllocation m_aoBlurAllocation{VK_NULL_HANDLE};
        VkImageView m_aoBlurImageView{VK_NULL_HANDLE};

        VkSampler m_depthSampler{VK_NULL_HANDLE};

        VkDescriptorSetLayout m_singleTextureDescLayout{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_dualTextureDescLayout{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_tripleTextureDescLayout{VK_NULL_HANDLE};

        VkDescriptorSet m_gtaoDescSet{VK_NULL_HANDLE};
        VkDescriptorSet m_gtaoBlurDescSet{VK_NULL_HANDLE};

        VkPipelineLayout m_gtaoPipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_gtaoPipeline{VK_NULL_HANDLE};

        VkPipelineLayout m_gtaoBlurPipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_gtaoBlurPipeline{VK_NULL_HANDLE};

        // Pre-defined HDRI Environment Maps (Image-Based Lighting)
        int m_selectedHdriIndex{0}; // 0: Sunset, 1: Studio, 2: City, 3: Courtyard, 4: Forest, 5: Interior, 6: Night, 7: Sunrise
        VkImage m_environmentImage{VK_NULL_HANDLE};
        VmaAllocation m_environmentAlloc{VK_NULL_HANDLE};
        VkImageView m_environmentImageView{VK_NULL_HANDLE};
        VkSampler m_environmentSampler{VK_NULL_HANDLE};

        // Default PBR Material Textures (Albedo, Tangent Normal, ORM)
        VkImage m_defaultAlbedoImage{VK_NULL_HANDLE};
        VmaAllocation m_defaultAlbedoAlloc{VK_NULL_HANDLE};
        VkImageView m_defaultAlbedoView{VK_NULL_HANDLE};

        VkImage m_defaultNormalImage{VK_NULL_HANDLE};
        VmaAllocation m_defaultNormalAlloc{VK_NULL_HANDLE};
        VkImageView m_defaultNormalView{VK_NULL_HANDLE};

        VkImage m_defaultOrmImage{VK_NULL_HANDLE};
        VmaAllocation m_defaultOrmAlloc{VK_NULL_HANDLE};
        VkImageView m_defaultOrmView{VK_NULL_HANDLE};

        VkSampler m_defaultTextureSampler{VK_NULL_HANDLE};

        // Omnidirectional Point Light Shadow Cubemap (UE5 / Blender style)
        static constexpr uint32_t POINT_SHADOW_MAP_SIZE = 1024;
        VkImage m_pointShadowImage{VK_NULL_HANDLE};
        VmaAllocation m_pointShadowAlloc{VK_NULL_HANDLE};
        VkImageView m_pointShadowCubeView{VK_NULL_HANDLE};
        std::array<VkImageView, 6> m_pointShadowFaceViews{};
        VkSampler m_pointShadowSampler{VK_NULL_HANDLE};
        VkDescriptorPool m_postProcessDescPool{VK_NULL_HANDLE};

        VkDescriptorSet m_bloomDownDescSet{VK_NULL_HANDLE};
        VkDescriptorSet m_kawaseDescSetA{VK_NULL_HANDLE};
        VkDescriptorSet m_kawaseDescSetB{VK_NULL_HANDLE};
        VkDescriptorSet m_compositeDescSet{VK_NULL_HANDLE};

        VkPipelineLayout m_bloomDownPipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_bloomDownPipeline{VK_NULL_HANDLE};

        VkPipelineLayout m_kawasePipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_kawasePipeline{VK_NULL_HANDLE};

        VkPipelineLayout m_compositePipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_compositePipeline{VK_NULL_HANDLE};

        // Semantic Segmentation Buffer (Object ID)
        VkImage m_objectIdImage{VK_NULL_HANDLE};
        VmaAllocation m_objectIdAllocation{VK_NULL_HANDLE};
        VkImageView m_objectIdImageView{VK_NULL_HANDLE};

        VkImage m_depthImage{VK_NULL_HANDLE};
        VmaAllocation m_depthAllocation{VK_NULL_HANDLE};
        VkImageView m_depthImageView{VK_NULL_HANDLE};

        // Directional Cascaded Shadow Mapping (UE5 Style - 4 Cascades)
        static constexpr uint32_t SHADOW_CASCADE_COUNT = 4;
        uint32_t m_shadowWidth{4096};
        uint32_t m_shadowHeight{4096};
        VkImage m_shadowImage{VK_NULL_HANDLE};
        VmaAllocation m_shadowAllocation{VK_NULL_HANDLE};
        VkImageView m_shadowImageView{VK_NULL_HANDLE};
        VkImageView m_shadowCascadeImageViews[SHADOW_CASCADE_COUNT]{VK_NULL_HANDLE};
        VkSampler m_shadowSampler{VK_NULL_HANDLE};
        VkPipelineLayout m_shadowPipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_shadowPipeline{VK_NULL_HANDLE};

        // PBR Descriptor Sets (Shadow Map + Scene UBO)
        struct OccluderData {
            glm::vec4 centerRadius; // xyz = center, w = radius
            glm::vec4 halfExtents;  // xyz = half-extents, w = shapeType (0: Box, 1: Sphere, 2: Cylinder)
        };

        struct SceneUboData {
            glm::mat4 cascadeLightSpace[4]{};
            glm::vec4 cascadeSplits{7.0f, 18.0f, 45.0f, 150.0f};
            glm::vec4 sunDirection{0.55f, 0.85f, 0.40f, 3.4f};
            glm::vec4 sunPosition{0.0f, 18.0f, 0.0f, 1.0f};
            glm::vec4 sunColor{1.0f, 0.97f, 0.92f, 1.0f};
            glm::vec4 ambientColor{0.15f, 0.22f, 0.35f, 1.0f};
            glm::vec4 fogParams{0.0f, 20.0f, 100.0f, 0.0f};
            glm::vec4 fogColor{0.06f, 0.07f, 0.09f, 1.0f};
            glm::vec4 lightsPos[8]{};
            glm::vec4 lightsCol[8]{};
            glm::vec4 lightsDir[8]{};
            glm::vec4 lightsParams[8]{};
            glm::vec4 pointShadowParams{-1.0f, 0.1f, 25.0f, 0.005f};
            int numLights{0};
            int numOccluders{0};
            float padding[2]{0.0f, 0.0f};
            OccluderData occluders[32]{};
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
