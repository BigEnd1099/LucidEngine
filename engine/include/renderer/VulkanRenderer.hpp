#pragma once
#include "renderer/VulkanContext.hpp"
#include "renderer/AccelerationStructure.hpp"
#include "renderer/RtDenoiser.hpp"
#include "renderer/RtSettings.hpp"
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

    struct EditorPreferencesUiState;
    struct EditorTopBarUiState;
    struct EditorViewportOverlayState;
    struct EditorCameraNavigationState;
    enum class EditorGizmoMode;
    class VulkanRenderer {
    public:
        VulkanRenderer(VulkanContext* context, uint32_t width, uint32_t height);
        ~VulkanRenderer();

        void render(ecs::Registry& registry, core::AssetManager* assetManager = nullptr, glm::mat4 viewMatrix = glm::mat4(1.0f), core::Engine* engine = nullptr);
        std::string captureFrameBase64();
        void submitBufferBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size);

        void setRenderResolution(uint32_t width, uint32_t height);
        void requestResolution(uint32_t width, uint32_t height);
        void invalidateRtHistory();
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

        void setIblEnabled(bool enabled) { m_iblEnabled = enabled; }
        bool isIblEnabled() const { return m_iblEnabled; } bool loadEnvironmentHdriFile(const std::string& path);


        void setRayTracingEnabled(bool enabled) { m_rtSettings.enabled = enabled; }
        bool isRayTracingEnabled() const { return m_rtSettings.enabled && m_context && m_context->isRayTracingSupported(); }
        VkAccelerationStructureKHR getRayTracingTLAS() const { return m_accelerationStructure.getTLAS(); }

        RtSettings& getRtSettings() { return m_rtSettings; }
        const RtSettings& getRtSettings() const { return m_rtSettings; }
        const RtDenoiser::GpuTimings& getRtGpuTimings() const { return m_rtDenoiser.getGpuTimings(); }

        // ---- Ray-traced indirect lighting quality getters/setters forwarding to m_rtSettings
        void setRtGiSamples(int n) { m_rtSettings.giSamples = std::clamp(n, 1, 16); }
        int  getRtGiSamples() const { return m_rtSettings.giSamples; }
        void setRtReflectionSamples(int n) { m_rtSettings.reflectionSamples = std::clamp(n, 1, 16); }
        int  getRtReflectionSamples() const { return m_rtSettings.reflectionSamples; }
        void setRtGiBounces(int n) { m_rtSettings.giBounces = std::clamp(n, 1, 15); }
        int  getRtGiBounces() const { return m_rtSettings.giBounces; }
        void setRtMaxHistory(int n) { m_rtSettings.maxHistory = std::clamp(n, 4, 256); }
        int  getRtMaxHistory() const { return m_rtSettings.maxHistory; }
        void setRtSpatialStrength(float s) { m_rtSettings.spatialStrength = std::clamp(s, 0.0f, 1.0f); }
        float getRtSpatialStrength() const { return m_rtSettings.spatialStrength; }
        void setRtDenoiseEnabled(bool e) { m_rtSettings.denoiseEnabled = e; }
        bool isRtDenoiseEnabled() const { return m_rtSettings.denoiseEnabled; }
        void setRtDebugMode(RtDebugMode mode) { m_rtSettings.debugMode = mode; }
        RtDebugMode getRtDebugMode() const { return m_rtSettings.debugMode; }

        void setFogEnabled(bool enabled) { m_fogEnabled = enabled; }
        bool isFogEnabled() const { return m_fogEnabled; }
        void invalidateMaterialDescriptorSet(uint32_t materialId = 0);

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

        void createDefaultTextures();
        void cleanupDefaultTextures();
        void uploadTexture2D(VkImage& dstImage, VmaAllocation& dstAlloc, VkImageView& dstView,
                             const void* pixels, uint32_t width, uint32_t height, VkFormat format, bool generateMips);
        void createParticlePipeline();
        void cleanupParticlePipeline();
        void initRtDenoiser();
        void updateRtIndirectDescriptor(VkDescriptorSet descriptorSet);
        void refreshRtIndirectDescriptors();
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
        void drawPerformanceProfilerPanel();
        void renderImGui(VkCommandBuffer cmd, VkImageView targetView, ecs::Registry& registry, core::AssetManager* assetManager, core::Engine* engine);
        void drawEditorCameraNavigation(core::Engine* engine, ecs::Registry& registry,
                                       EditorCameraNavigationState& state);
        void drawEditorViewportOverlay(EditorViewportOverlayState& state, const glm::vec2& viewportPanelSize);
        void drawEditorTopBarPanel(core::Engine* engine, EditorTopBarUiState& state,
                                   float screenW, float topTitleBarH, float topLevelTabsH,
                                   float topToolbarH, entt::entity& selectedEntity);
        void drawEditorPreferencesPanel(core::Engine* engine, EditorPreferencesUiState& state,
                                         float screenW, float screenH);
        void drawEditorPlaceActorsPanel(ecs::Registry& registry, core::Engine* engine,
                                        bool immersivePlay, bool& showPlaceActors, bool& showDetails,
                                        float uiScale, float topTotalH, float contentH,
                                        float leftPanelW, const glm::vec3& cameraPosition,
                                        const glm::vec3& cameraForward,
                                        entt::entity& selectedEntity);

        void drawEditorDetailsPanel(ecs::Registry& registry, core::Engine* engine,
                                    entt::entity& selectedEntity, bool immersivePlay,
                                    bool showDetails, bool showOutliner, float uiScale,
                                    float leftPanelW, float centerViewportW, float topTotalH,
                                    float outlinerH, float rightPanelW, float detailsH,
                                    const glm::vec3& cameraPosition, const glm::vec3& cameraForward,
                                    float cameraYaw, float cameraPitch, float cameraFov,
                                    const glm::vec2& viewportPanelSize);

#endif

        VulkanContext* m_context{nullptr};
        AccelerationStructure m_accelerationStructure;
        RtSettings m_rtSettings{};
        RtDenoiser m_rtDenoiser;
        VkPipeline m_rtGraphicsPipeline{VK_NULL_HANDLE};

        static constexpr uint32_t RT_MAX_INSTANCE_COUNT = 4096;
        static constexpr uint32_t RT_MATERIAL_TEXTURE_COUNT = 256;
        struct alignas(16) RtInstanceGpuData {
            glm::mat4 model{1.0f};
            glm::mat4 normalMatrix{1.0f};
            glm::vec4 baseColor{1.0f};
            glm::vec4 material{0.0f}; // metallic, roughness, emissive, AO
            VkDeviceAddress vertexAddress{0};
            VkDeviceAddress indexAddress{0};
            uint32_t vertexCount{0};
            uint32_t indexCount{0};
            uint32_t padding[2]{0, 0};
            glm::uvec4 textureIndices{0}; // albedo, normal, ORM, reserved; 0 = fallback texture
        };
        static_assert(sizeof(RtInstanceGpuData) == 208, "RT instance data must match std430 shader layout");
        VkBuffer m_rtInstanceDataBuffer{VK_NULL_HANDLE};
        VmaAllocation m_rtInstanceDataAllocation{VK_NULL_HANDLE};
        void* m_rtInstanceDataMapped{nullptr};
        bool m_iblEnabled{true};
        int m_detailsTab{0};
        uint32_t m_width{1920};
        uint32_t m_height{1080};
        uint32_t m_baseWidth{1920};
        uint32_t m_baseHeight{1080};
        float m_resolutionScale{1.0f};
        float m_uiScale{1.0f};
        float m_shadowDistance{140.0f};
        bool m_softShadows{true};

        void setSoftShadows(bool enabled) { m_softShadows = enabled; }
        bool areSoftShadowsEnabled() const { return m_softShadows; }
        int m_antiAliasingMode{2};
        bool m_standaloneGameMode{false};
        bool m_showPerformanceProfiler{false};
        uint32_t m_frameCounter{0};

        struct DeformedVertexCache {
            VkBuffer buffer{VK_NULL_HANDLE};
            VmaAllocation allocation{VK_NULL_HANDLE};
            VkDeviceSize size{0};
            uint64_t contentHash{0};
            uint64_t lastSeenFrame{0};
            bool rayTracingCapable{false};
        };
        std::unordered_map<entt::entity, DeformedVertexCache> m_deformedVertexCache;
        uint64_t m_deformationFrameCounter{0};

        // Main PBR, Depth Prepass & G-Buffer Pipelines
        VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_graphicsPipeline{VK_NULL_HANDLE};
        VkPipeline m_depthPrepassPipeline{VK_NULL_HANDLE};
        VkPipeline m_gBufferPipeline{VK_NULL_HANDLE};

        // Thin G-Buffer targets (shading normal + roughness, linear albedo + metallic)
        VkImage m_gBufferNormalRoughnessImage{VK_NULL_HANDLE};
        VmaAllocation m_gBufferNormalRoughnessAlloc{VK_NULL_HANDLE};
        VkImageView m_gBufferNormalRoughnessImageView{VK_NULL_HANDLE};

        VkImage m_gBufferAlbedoMetallicImage{VK_NULL_HANDLE};
        VmaAllocation m_gBufferAlbedoMetallicAlloc{VK_NULL_HANDLE};
        VkImageView m_gBufferAlbedoMetallicImageView{VK_NULL_HANDLE};

        VkImage m_gBufferMotionImage{VK_NULL_HANDLE};
        VmaAllocation m_gBufferMotionAlloc{VK_NULL_HANDLE};
        VkImageView m_gBufferMotionImageView{VK_NULL_HANDLE};

        // Per-instance motion vectors buffer (prevViewProj + prevModels)
        VkBuffer m_motionBuffer{VK_NULL_HANDLE};
        VmaAllocation m_motionBufferAlloc{VK_NULL_HANDLE};
        void* m_motionBufferMapped{nullptr};
        std::unordered_map<uint32_t, glm::mat4> m_prevModelMatrices;

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

        // Indirect Diffuse GI: Color Bleed od kazdego elementu (SSGI) + analityczny Ground Bounce
        bool m_lightBounceEnabled{true};
        float m_lightBounceIntensity{0.85f};
        float m_lightBounceGroundReflectance{1.0f};
        float m_lightBounceMultiBounce{1.0f};
        glm::vec3 m_lightBounceGroundColor{0.16f, 0.19f, 0.25f};
        glm::mat4 m_prevViewProj{1.0f};
        bool m_hasPreviousViewProj{false}; // Prevents sampling undefined history on the first frame or after resize.

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
        uint64_t m_shadowCacheHash{0};
        uint64_t m_pointShadowCacheHash{0};
        uint32_t m_shadowWidth{2048};
        uint32_t m_shadowHeight{2048};
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
            glm::vec4 rtParams{0.0f}; // x = RT active, y = RT shadows, z = RT AO, w = RT reflections
            int numLights{0};
            int numOccluders{0};
            int frameIndex{0};
            int skyboxMode{1};
            OccluderData occluders[32]{};
            glm::vec4 rtFeatureParams{0.0f}; // x = diffuse GI, y = RT instance count, z = GI intensity, w = reserved
        };

        VkBuffer m_sceneUboBuffer{VK_NULL_HANDLE};
        VmaAllocation m_sceneUboAlloc{VK_NULL_HANDLE};
        void* m_sceneUboMappedPtr{nullptr};
        VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
        VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
        VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};
        std::unordered_map<uint32_t, VkDescriptorSet> m_materialDescriptorSets;
        VkDescriptorSet getOrCreateMaterialDescriptorSet(uint32_t materialId, core::AssetManager* assetManager);
        void updateRayTracingDescriptorSet(VkDescriptorSet descriptorSet);
        void updateRayTracingTextureDescriptors(core::AssetManager* assetManager);
        uint32_t m_rtTextureDescriptorCount{UINT32_MAX};

        VkCommandBuffer m_commandBuffer{VK_NULL_HANDLE};

        // Two timestamp queries measure actual GPU execution time for the render command buffer.
        VkQueryPool m_timestampQueryPool{VK_NULL_HANDLE};
        bool m_timestampQueryPending{false};

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
