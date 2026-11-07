#pragma once
#include "renderer/VulkanContext.hpp"
#include "renderer/RtSettings.hpp"
#include <glm/glm.hpp>
#include <array>
#include <cstdint>

namespace engine::renderer {

    // Ray-traced indirect lighting (diffuse GI + ambient visibility) as a self-contained
    // compute pipeline, run between GTAO and the main PBR pass:
    //   1. rt_indirect  - traces N paths/pixel from the depth buffer (noisy, demodulated)
    //   2. rt_temporal  - reprojected temporal accumulation (history counted in samples)
    //   3. rt_atrous x2 - edge-avoiding spatial filter, strength fades with history length
    // The result (rgb = GI radiance, a = AO visibility) is sampled by pbr_rt.frag.
    class RtDenoiser {
    public:
        struct Params {
            glm::mat4 viewProj{1.0f};      // current frame (used as next frame's history camera)
            uint32_t  samples{1};          // paths per pixel per frame, 1..16
            uint32_t  frameIndex{0};
            bool      giEnabled{true};
            bool      aoEnabled{true};
            bool      reflectionsEnabled{true};
            uint32_t  reflectionSamples{4};
            uint32_t  bounces{2};          // 1..15
            float     maxHistory{32.0f};   // effective samples kept in the temporal history
            float     spatialStrength{1.0f}; // 0 = no spatial filter, 1 = full
            bool      svgfEnabled{true};   // Spatiotemporal Variance-Guided Filtering
            RtDebugMode debugMode{RtDebugMode::None};
        };

        struct GpuTimings {
            float traceMs{0.0f};
            float reflTraceMs{0.0f};
            float temporalMs{0.0f};
            float reflTemporalMs{0.0f};
            float atrous0Ms{0.0f};
            float atrous1Ms{0.0f};
            float totalMs{0.0f};
        };

        bool init(VulkanContext* context, VkDescriptorSetLayout sceneSetLayout,
                  uint32_t width, uint32_t height,
                  VkImageView depthView, VkSampler depthSampler,
                  VkImageView gNormalRoughnessView = VK_NULL_HANDLE,
                  VkImageView gAlbedoMetallicView = VK_NULL_HANDLE,
                  VkImageView gMotionView = VK_NULL_HANDLE);
        void resize(uint32_t width, uint32_t height,
                    VkImageView depthView, VkSampler depthSampler,
                    VkImageView gNormalRoughnessView = VK_NULL_HANDLE,
                    VkImageView gAlbedoMetallicView = VK_NULL_HANDLE,
                  VkImageView gMotionView = VK_NULL_HANDLE);
        void cleanup();

        bool isReady() const { return m_ready; }
        // Call when a frame was rendered without RT indirect (history would be stale).
        void invalidateHistory() { m_historyValid = false; }

        // Records all passes. Depth must be in DEPTH_STENCIL_READ_ONLY_OPTIMAL.
        void record(VkCommandBuffer cmd, VkDescriptorSet sceneSet, const Params& params);

        VkImageView outputView() const { return m_imgS.view; }
        VkImageView reflectionOutputView() const { return m_imgRefl.view; }
        VkSampler   outputSampler() const { return m_sampler; }
        const GpuTimings& getGpuTimings() const { return m_gpuTimings; }

    private:
        struct Image {
            VkImage image{VK_NULL_HANDLE};
            VmaAllocation alloc{VK_NULL_HANDLE};
            VkImageView view{VK_NULL_HANDLE};
        };

        void createImages();
        void destroyImages();
        void writeDescriptors(VkImageView depthView, VkSampler depthSampler,
                              VkImageView gNormalRoughnessView, VkImageView gAlbedoMetallicView,
                              VkImageView gMotionView);
        VkShaderModule loadModule(const char* spvName) const;
        VkPipeline createPipeline(VkShaderModule module, int32_t specPass) const;
        Image makeImage(VkFormat format) const;

        VulkanContext* m_context{nullptr};
        uint32_t m_width{0}, m_height{0};
        bool m_ready{false};
        bool m_needsLayoutInit{true};
        bool m_historyValid{false};
        uint32_t m_parity{0};
        glm::mat4 m_prevViewProj{1.0f};

        Image m_imgS{};                    // raw trace output, then final filtered output
        Image m_imgA{};                    // a-trous ping buffer
        std::array<Image, 2> m_geom{};     // oct normal, linear depth, history length (ping-pong)
        std::array<Image, 2> m_hist{};     // temporal accumulation (ping-pong)
        Image m_imgRefl{};                 // raw reflection trace
        std::array<Image, 2> m_imgReflHist{}; // reflection temporal ping-pong
        std::array<Image, 2> m_moments{};     // SVGF: moments (mu1, mu2, variance, filtered_var)

        VkSampler m_sampler{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_setLayout{VK_NULL_HANDLE};
        VkDescriptorPool m_pool{VK_NULL_HANDLE};
        std::array<VkDescriptorSet, 2> m_sets{};
        VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_tracePipelineHandle{VK_NULL_HANDLE};
        VkPipeline m_temporalPipeline{VK_NULL_HANDLE};
        std::array<VkPipeline, 2> m_atrousPipelines{};
        VkPipeline m_reflPipeline{VK_NULL_HANDLE};
        VkPipeline m_reflTemporalPipeline{VK_NULL_HANDLE};

        VkQueryPool m_queryPool{VK_NULL_HANDLE};
        bool m_timestampsSupported{false};
        bool m_hasTimestampResults{false};
        GpuTimings m_gpuTimings{};
    };
}
