#pragma once
#include "renderer/VulkanContext.hpp"
#include "ecs/Registry.hpp"
#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <unordered_map>

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

        void setShadowDistance(float dist) { m_shadowDistance = std::clamp(dist, 20.0f, 200.0f); }
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

        void setExposure(float ev) { m_exposure = std::clamp(ev, 0.1f, 6.0f); }
        float getExposure() const { return m_exposure; }

        void setSkyboxEnabled(bool enabled) { m_skyboxEnabled = enabled; }
        bool isSkyboxEnabled() const { return m_skyboxEnabled; }

        void setFogEnabled(bool enabled) { m_fogEnabled = enabled; }
        bool isFogEnabled() const { return m_fogEnabled; }

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
        void bakeIblMaps();
        void createSkyboxPipeline();
        void cleanupSkyboxPipeline();
        void loadEnvironmentHdri(int index);
        bool loadEnvironmentHdriFile(const std::string& path);
        void createDefaultTextures();
        void cleanupDefaultTextures();
        void uploadTexture2D(VkImage& dstImage, VmaAllocation& dstAlloc, VkImageView& dstView,
                             const void* pixels, uint32_t width, uint32_t height, VkFormat format, bool generateMips);
        void createParticlePipeline();
        void cleanupParticlePipeline();
        void createPostProcessResources();
        void updatePostProcessDescriptorSets();
        void cleanupPostProcessResources();

        VkShaderModule createShaderModule(const std::vector<char>& code) const;
        void createFullscreenPipeline(const std::string& fragShaderSpv, VkPipelineLayout layout,
                                      VkFormat targetFormat, VkPipeline* outPipeline, bool additiveBlend = false);
        static void transitionImage(VkCommandBuffer cmd, VkImage image,
                                    VkImageLayout oldLayout, VkImageLayout newLayout,
                                    VkImageAspectFlags aspectMask,
                                    uint32_t baseMip, uint32_t levelCount,
                                    uint32_t baseLayer, uint32_t layerCount,
                                    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

#ifdef USE_GLFW
        void initImGui();
        void renderImGui(VkCommandBuffer cmd, VkImageView targetView, ecs::Registry& registry, core::AssetManager* assetManager, core::Engine* engine);
#endif

        VulkanContext* m_context{nullptr};
        uint32_t m_width{1920};
        uint32_t m_height{1080};
        uint32_t m_baseWidth{1920};
        uint32_t m_baseHeight{1080};
        float m_resolutionScale{1.0f};
        float m_uiScale{1.0f};
        float m_shadowDistance{140.0f};
        int m_antiAliasingMode{2};
        bool m_standaloneGameMode{false};
        uint32_t m_frameCounter{0};

        // Main PBR & Depth Prepass Pipelines
        VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_graphicsPipeline{VK_NULL_HANDLE};
        VkPipeline m_depthPrepassPipeline{VK_NULL_HANDLE};

        // Final Output & LDR Intermediate (for single-pass ACES -> FXAA 3.11)
        VkImage m_colorImage{VK_NULL_HANDLE};
        VmaAllocation m_colorAllocation{VK_NULL_HANDLE};
        VkImageView m_colorImageView{VK_NULL_HANDLE};

        VkImage m_ldrTempImage{VK_NULL_HANDLE};
        VmaAllocation m_ldrTempAllocation{VK_NULL_HANDLE};
        VkImageView m_ldrTempImageView{VK_NULL_HANDLE};

        VkSampler m_viewportSampler{VK_NULL_HANDLE};
        VkDescriptorSet m_viewportDescriptor{VK_NULL_HANDLE};

        // HDR Scene & 5-Mip Jimenez Bloom Pyramid
        static constexpr uint32_t BLOOM_MIP_COUNT = 5;
        bool m_bloomEnabled{true};
        float m_bloomIntensity{0.42f};
        float m_bloomThreshold{1.20f};
        float m_exposure{1.0f};

        VkImage m_hdrImage{VK_NULL_HANDLE};
        VmaAllocation m_hdrAllocation{VK_NULL_HANDLE};
        VkImageView m_hdrImageView{VK_NULL_HANDLE};

        VkImage m_bloomImage{VK_NULL_HANDLE};
        VmaAllocation m_bloomAllocation{VK_NULL_HANDLE};
        VkImageView m_bloomFullView{VK_NULL_HANDLE};
        std::array<VkImageView, BLOOM_MIP_COUNT> m_bloomMipViews{};
        std::array<VkExtent2D, BLOOM_MIP_COUNT> m_bloomMipExtents{};
        uint32_t m_activeBloomMips{BLOOM_MIP_COUNT};
        VkSampler m_linearSampler{VK_NULL_HANDLE};

        // Exponential Height Fog
        bool m_fogEnabled{false};
        float m_fogDensity{0.012f};
        float m_fogHeightFalloff{0.08f};
        glm::vec3 m_fogColor{0.07f, 0.10f, 0.16f};

        // Global Illumination / Light Bouncing
        bool m_lightBounceEnabled{true};
        float m_lightBounceIntensity{0.80f};
        float m_lightBounceGroundReflectance{0.60f};
        float m_lightBounceMultiBounce{1.0f};
        glm::vec3 m_lightBounceGroundColor{0.85f, 0.80f, 0.72f};

        void setLightBounceEnabled(bool enabled) { m_lightBounceEnabled = enabled; }
        bool isLightBounceEnabled() const { return m_lightBounceEnabled; }
        void setLightBounceIntensity(float intensity) { m_lightBounceIntensity = std::clamp(intensity, 0.0f, 5.0f); }
        float getLightBounceIntensity() const { return m_lightBounceIntensity; }
        void setLightBounceGroundReflectance(float r) { m_lightBounceGroundReflectance = std::clamp(r, 0.0f, 1.0f); }
        float getLightBounceGroundReflectance() const { return m_lightBounceGroundReflectance; }
        void setLightBounceMultiBounce(float mb) { m_lightBounceMultiBounce = std::clamp(mb, 0.0f, 3.0f); }
        float getLightBounceMultiBounce() const { return m_lightBounceMultiBounce; }
        void setLightBounceGroundColor(const glm::vec3& col) { m_lightBounceGroundColor = col; }
        glm::vec3 getLightBounceGroundColor() const { return m_lightBounceGroundColor; }

        // Ground Truth Ambient Occlusion (GTAO)
        bool m_gtaoEnabled{true};
        float m_gtaoRadius{0.75f};
        float m_gtaoIntensity{1.15f};
        VkImage m_aoImage{VK_NULL_HANDLE};
        VmaAllocation m_aoAllocation{VK_NULL_HANDLE};
        VkImageView m_aoImageView{VK_NULL_HANDLE};
        VkImage m_aoBlurImage{VK_NULL_HANDLE};
        VmaAllocation m_aoBlurAllocation{VK_NULL_HANDLE};
        VkImageView m_aoBlurImageView{VK_NULL_HANDLE};
        VkSampler m_depthSampler{VK_NULL_HANDLE};

        VkDescriptorSetLayout m_singleTextureDescLayout{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_dualTextureDescLayout{VK_NULL_HANDLE};
        VkDescriptorSet m_gtaoDescSet{VK_NULL_HANDLE};
        VkDescriptorSet m_gtaoBlurDescSet{VK_NULL_HANDLE};
        VkPipelineLayout m_gtaoPipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_gtaoPipeline{VK_NULL_HANDLE};
        VkPipelineLayout m_gtaoBlurPipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_gtaoBlurPipeline{VK_NULL_HANDLE};

        // Precomputed GPU Cubemap IBL (Sky Cubemap, Irradiance, GGX Specular Prefilter, 2D BRDF LUT)
        static constexpr uint32_t ENV_CUBE_SIZE = 512;
        static constexpr uint32_t ENV_CUBE_MIPS = 10;
        static constexpr uint32_t IRR_CUBE_SIZE = 64;
        static constexpr uint32_t PREFILTER_CUBE_SIZE = 256;
        static constexpr uint32_t PREFILTER_CUBE_MIPS = 5;
        static constexpr uint32_t BRDF_LUT_SIZE = 512;

        int m_selectedHdriIndex{0};
        char m_customHdriPath[512]{};
        bool m_skyboxEnabled{true};
        bool m_brdfLutBaked{false};

        VkImage m_environmentImage{VK_NULL_HANDLE};
        VmaAllocation m_environmentAlloc{VK_NULL_HANDLE};
        VkImageView m_environmentImageView{VK_NULL_HANDLE};
        VkSampler m_environmentSampler{VK_NULL_HANDLE};

        VkImage m_envCubeImage{VK_NULL_HANDLE};
        VmaAllocation m_envCubeAlloc{VK_NULL_HANDLE};
        VkImageView m_envCubeView{VK_NULL_HANDLE};
        std::array<VkImageView, 6> m_envCubeFaceViews{};

        VkImage m_irradianceCubeImage{VK_NULL_HANDLE};
        VmaAllocation m_irradianceCubeAlloc{VK_NULL_HANDLE};
        VkImageView m_irradianceCubeView{VK_NULL_HANDLE};
        std::array<VkImageView, 6> m_irradianceFaceViews{};

        VkImage m_prefilterCubeImage{VK_NULL_HANDLE};
        VmaAllocation m_prefilterCubeAlloc{VK_NULL_HANDLE};
        VkImageView m_prefilterCubeView{VK_NULL_HANDLE};
        std::array<std::array<VkImageView, 6>, PREFILTER_CUBE_MIPS> m_prefilterFaceMipViews{};

        VkImage m_brdfLutImage{VK_NULL_HANDLE};
        VmaAllocation m_brdfLutAlloc{VK_NULL_HANDLE};
        VkImageView m_brdfLutView{VK_NULL_HANDLE};

        VkDescriptorPool m_iblDescPool{VK_NULL_HANDLE};
        VkDescriptorSet m_iblEquirectDescSet{VK_NULL_HANDLE};
        VkDescriptorSet m_iblCubemapDescSet{VK_NULL_HANDLE};
        VkPipelineLayout m_iblCubePipelineLayout{VK_NULL_HANDLE};
        VkPipelineLayout m_brdfLutPipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_equirectToCubePipeline{VK_NULL_HANDLE};
        VkPipeline m_irradiancePipeline{VK_NULL_HANDLE};
        VkPipeline m_prefilterPipeline{VK_NULL_HANDLE};
        VkPipeline m_brdfLutPipeline{VK_NULL_HANDLE};

        VkPipelineLayout m_skyboxPipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_skyboxPipeline{VK_NULL_HANDLE};

        bool m_autoViewportResolution{true};
        int m_resolutionPresetIndex{0};

        // Default PBR Material Textures
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

        // Omnidirectional Point Light Shadow Cubemap
        static constexpr uint32_t POINT_SHADOW_MAP_SIZE = 1024;
        VkImage m_pointShadowImage{VK_NULL_HANDLE};
        VmaAllocation m_pointShadowAlloc{VK_NULL_HANDLE};
        VkImageView m_pointShadowCubeView{VK_NULL_HANDLE};
        std::array<VkImageView, 6> m_pointShadowFaceViews{};
        VkSampler m_pointShadowSampler{VK_NULL_HANDLE};

        // Post-Process Descriptors & Pipelines
        VkDescriptorPool m_postProcessDescPool{VK_NULL_HANDLE};
        VkDescriptorSet m_bloomDownHdrDescSet{VK_NULL_HANDLE};
        std::array<VkDescriptorSet, BLOOM_MIP_COUNT> m_bloomMipDescSets{};
        VkDescriptorSet m_compositeDescSet{VK_NULL_HANDLE};
        VkDescriptorSet m_fxaaDescSet{VK_NULL_HANDLE};

        VkPipelineLayout m_bloomPipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_bloomDownPipeline{VK_NULL_HANDLE};
        VkPipeline m_bloomUpPipeline{VK_NULL_HANDLE};
        VkPipelineLayout m_compositePipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_compositePipeline{VK_NULL_HANDLE};
        VkPipelineLayout m_fxaaPipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_fxaaPipeline{VK_NULL_HANDLE};

        // Semantic Segmentation & Depth Buffer
        VkImage m_objectIdImage{VK_NULL_HANDLE};
        VmaAllocation m_objectIdAllocation{VK_NULL_HANDLE};
        VkImageView m_objectIdImageView{VK_NULL_HANDLE};

        VkImage m_depthImage{VK_NULL_HANDLE};
        VmaAllocation m_depthAllocation{VK_NULL_HANDLE};
        VkImageView m_depthImageView{VK_NULL_HANDLE};

        // Directional Cascaded Shadow Mapping (4 Cascades)
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

        // Aligned std140 Scene UBO
        struct OccluderData {
            glm::vec4 centerRadius;
            glm::vec4 halfExtents;
        };

        struct SceneUboData {
            glm::mat4 viewProj{1.0f};
            glm::mat4 view{1.0f};
            glm::mat4 invViewProj{1.0f};
            glm::mat4 cascadeLightSpace[4]{};
            glm::vec4 cascadeSplits{7.0f, 20.0f, 48.0f, 140.0f};
            glm::vec4 cascadeRadii{8.0f, 22.0f, 52.0f, 145.0f};
            glm::vec4 cameraPos{0.0f, 15.0f, 30.0f, 1.0f};
            glm::vec4 sunDirection{0.55f, 0.85f, 0.40f, 2.8f};
            glm::vec4 sunPosition{0.0f, 18.0f, 0.0f, 1.0f};
            glm::vec4 sunColor{1.0f, 0.97f, 0.92f, 1.0f};
            glm::vec4 ambientColor{0.15f, 0.22f, 0.35f, 0.85f};
            glm::vec4 fogParams{0.0f, 0.012f, 0.08f, 0.0f};
            glm::vec4 fogColor{0.07f, 0.10f, 0.16f, 1.0f};
            glm::vec4 screenParams{1920.0f, 1080.0f, 1.0f / 1920.0f, 1.0f / 1080.0f};
            glm::vec4 lightsPos[8]{};
            glm::vec4 lightsCol[8]{};
            glm::vec4 lightsDir[8]{};
            glm::vec4 lightsParams[8]{};
            glm::vec4 pointShadowParams{-1.0f, 0.1f, 25.0f, 0.005f};
            glm::vec4 bounceParams{1.0f, 0.80f, 0.60f, 1.0f};
            glm::vec4 bounceColor{0.85f, 0.80f, 0.72f, 1.0f};
            int numLights{0};
            int numOccluders{0};
            int frameIndex{0};
            int skyboxMode{1};
            OccluderData occluders[32]{};
        };

        VkBuffer m_sceneUboBuffer{VK_NULL_HANDLE};
        VmaAllocation m_sceneUboAlloc{VK_NULL_HANDLE};
        void* m_sceneUboMappedPtr{nullptr};
        VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
        VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
        VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};
        std::unordered_map<uint32_t, VkDescriptorSet> m_materialDescriptorSets;
        VkDescriptorSet getOrCreateMaterialDescriptorSet(uint32_t materialId, core::AssetManager* assetManager);

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
        VkSemaphore m_imageAvailableSemaphore{VK_NULL_HANDLE};
        VkSemaphore m_renderFinishedSemaphore{VK_NULL_HANDLE};
        VkFence m_inFlightFence{VK_NULL_HANDLE};
        uint32_t m_currentFrameIndex{0};
        float m_viewportAspectRatio{16.0f / 9.0f};
#endif
    };
}
