#include "renderer/RtDenoiser.hpp"
#include "core/Logger.hpp"
#include "core/Profiler.hpp"
#include "utils/FileIO.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <vector>

namespace engine::renderer {

    namespace {
        struct PushData {
            glm::mat4 m;   // previous view-projection (temporal pass)
            glm::vec4 f;
            uint32_t  u[4];
        };
        static_assert(sizeof(PushData) == 96, "RT denoiser push constants must match the shaders");

        void memoryBarrier(VkCommandBuffer cmd,
                           VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                           VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
            VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
            mb.srcStageMask = srcStage;
            mb.srcAccessMask = srcAccess;
            mb.dstStageMask = dstStage;
            mb.dstAccessMask = dstAccess;
            VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            dep.memoryBarrierCount = 1;
            dep.pMemoryBarriers = &mb;
            vkCmdPipelineBarrier2(cmd, &dep);
        }

        constexpr VkAccessFlags2 kStorageRW = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    }

    // Set 1 bindings: 0 depth sampler, 1 imgS, 2 imgA, 3 geomCur, 4 geomPrev, 5 histCur, 6 histPrev.
    bool RtDenoiser::init(VulkanContext* context, VkDescriptorSetLayout sceneSetLayout,
                          uint32_t width, uint32_t height,
                          VkImageView depthView, VkSampler depthSampler,
                          VkImageView gNormalRoughnessView,
                          VkImageView gAlbedoMetallicView,
                          VkImageView gMotionView) {
        m_context = context;
        if (!m_context || !m_context->isRayTracingSupported()) return false;
        VkDevice dev = m_context->getDevice();
        m_width = width;
        m_height = height;
        m_giWidth = std::max(1u, (m_width + 1u) / 2u);
        m_giHeight = std::max(1u, (m_height + 1u) / 2u);

        VkSamplerCreateInfo si{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(dev, &si, nullptr, &m_sampler) != VK_SUCCESS)
            throw std::runtime_error("RtDenoiser: failed to create sampler");

        VkDescriptorSetLayoutBinding b[15]{};
        for (uint32_t i = 0; i < 15; ++i) {
            b[i].binding = i;
            b[i].descriptorCount = 1;
            b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            if (i == 0 || i == 7 || i == 8 || i == 12) {
                b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            } else {
                b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            }
        }
        VkDescriptorSetLayoutCreateInfo li{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        li.bindingCount = 15;
        li.pBindings = b;
        if (vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_setLayout) != VK_SUCCESS)
            throw std::runtime_error("RtDenoiser: failed to create set layout");

        const VkDescriptorPoolSize sizes[2] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32 }
        };
        VkDescriptorPoolCreateInfo pi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pi.maxSets = 2;
        pi.poolSizeCount = 2;
        pi.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(dev, &pi, nullptr, &m_pool) != VK_SUCCESS)
            throw std::runtime_error("RtDenoiser: failed to create descriptor pool");

        const VkDescriptorSetLayout layouts[2] = { m_setLayout, m_setLayout };
        VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        ai.descriptorPool = m_pool;
        ai.descriptorSetCount = 2;
        ai.pSetLayouts = layouts;
        if (vkAllocateDescriptorSets(dev, &ai, m_sets.data()) != VK_SUCCESS)
            throw std::runtime_error("RtDenoiser: failed to allocate descriptor sets");

        const VkDescriptorSetLayout setLayouts[2] = { sceneSetLayout, m_setLayout };
        const VkPushConstantRange pcr{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushData) };
        VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pli.setLayoutCount = 2;
        pli.pSetLayouts = setLayouts;
        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges = &pcr;
        if (vkCreatePipelineLayout(dev, &pli, nullptr, &m_pipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("RtDenoiser: failed to create pipeline layout");

        VkShaderModule trace = loadModule("rt_indirect.comp.spv");
        VkShaderModule temporal = loadModule("rt_temporal.comp.spv");
        VkShaderModule atrous = loadModule("rt_atrous.comp.spv");
        VkShaderModule refl = loadModule("rt_reflections.comp.spv");
        VkShaderModule reflTemp = loadModule("rt_refl_temporal.comp.spv");
        m_tracePipelineHandle = createPipeline(trace, -1);
        m_temporalPipeline = createPipeline(temporal, -1);
        m_atrousPipelines[0] = createPipeline(atrous, 0);
        m_atrousPipelines[1] = createPipeline(atrous, 1);
        m_reflPipeline = createPipeline(refl, -1);
        m_reflTemporalPipeline = createPipeline(reflTemp, -1);
        vkDestroyShaderModule(dev, reflTemp, nullptr);
        vkDestroyShaderModule(dev, refl, nullptr);
        vkDestroyShaderModule(dev, trace, nullptr);
        vkDestroyShaderModule(dev, temporal, nullptr);
        vkDestroyShaderModule(dev, atrous, nullptr);

        createImages();
        writeDescriptors(depthView, depthSampler, gNormalRoughnessView, gAlbedoMetallicView, gMotionView);

        const auto& props = m_context->getPhysicalDeviceProperties();
        m_timestampsSupported = (props.limits.timestampPeriod > 0.0f) && (m_context->getTimestampValidBits() > 0);
        if (m_timestampsSupported) {
            VkQueryPoolCreateInfo qpci{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
            qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qpci.queryCount = 13;
            if (vkCreateQueryPool(dev, &qpci, nullptr, &m_queryPool) != VK_SUCCESS) {
                m_queryPool = VK_NULL_HANDLE;
                m_timestampsSupported = false;
            }
        }

        m_ready = true;
        core::Logger::info("RT indirect pipeline (trace + temporal + a-trous) initialized.");
        return true;
    }

    void RtDenoiser::resize(uint32_t width, uint32_t height,
                            VkImageView depthView, VkSampler depthSampler,
                            VkImageView gNormalRoughnessView,
                            VkImageView gAlbedoMetallicView,
                            VkImageView gMotionView) {
        if (!m_context || m_setLayout == VK_NULL_HANDLE) return;
        vkDeviceWaitIdle(m_context->getDevice());
        destroyImages();
        m_width = width;
        m_height = height;
        m_giWidth = std::max(1u, (m_width + 1u) / 2u);
        m_giHeight = std::max(1u, (m_height + 1u) / 2u);
        createImages();
        writeDescriptors(depthView, depthSampler, gNormalRoughnessView, gAlbedoMetallicView, gMotionView);
        m_needsLayoutInit = true;
        m_historyValid = false;
        m_giHistoryValid = false;
    }

    void RtDenoiser::cleanup() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice dev = m_context->getDevice();
        destroyImages();
        for (auto& p : m_atrousPipelines) { if (p) vkDestroyPipeline(dev, p, nullptr); p = VK_NULL_HANDLE; }
        if (m_temporalPipeline) vkDestroyPipeline(dev, m_temporalPipeline, nullptr);
        if (m_tracePipelineHandle) vkDestroyPipeline(dev, m_tracePipelineHandle, nullptr);
        if (m_reflTemporalPipeline) vkDestroyPipeline(dev, m_reflTemporalPipeline, nullptr);
        if (m_reflPipeline) vkDestroyPipeline(dev, m_reflPipeline, nullptr);
        m_reflTemporalPipeline = m_reflPipeline = VK_NULL_HANDLE;
        m_temporalPipeline = m_tracePipelineHandle = VK_NULL_HANDLE;
        if (m_pipelineLayout) vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr);
        if (m_pool) vkDestroyDescriptorPool(dev, m_pool, nullptr);
        if (m_setLayout) vkDestroyDescriptorSetLayout(dev, m_setLayout, nullptr);
        if (m_queryPool) { vkDestroyQueryPool(dev, m_queryPool, nullptr); m_queryPool = VK_NULL_HANDLE; }
        if (m_sampler) vkDestroySampler(dev, m_sampler, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
        m_pool = VK_NULL_HANDLE;
        m_setLayout = VK_NULL_HANDLE;
        m_sampler = VK_NULL_HANDLE;
        m_ready = false;
    }

    VkShaderModule RtDenoiser::loadModule(const char* spvName) const {
        const std::vector<char> code = utils::readFile(spvName);
        VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        ci.codeSize = code.size();
        ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule m = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_context->getDevice(), &ci, nullptr, &m) != VK_SUCCESS)
            throw std::runtime_error(std::string("RtDenoiser: failed to create shader module ") + spvName);
        return m;
    }

    VkPipeline RtDenoiser::createPipeline(VkShaderModule module, int32_t specPass) const {
        VkSpecializationMapEntry entry{ 0, 0, sizeof(int32_t) };
        VkSpecializationInfo spec{};
        spec.mapEntryCount = 1;
        spec.pMapEntries = &entry;
        spec.dataSize = sizeof(int32_t);
        spec.pData = &specPass;

        VkComputePipelineCreateInfo ci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        ci.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_COMPUTE_BIT, module, "main", specPass >= 0 ? &spec : nullptr };
        ci.layout = m_pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateComputePipelines(m_context->getDevice(), VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline) != VK_SUCCESS)
            throw std::runtime_error("RtDenoiser: failed to create compute pipeline");
        return pipeline;
    }

    RtDenoiser::Image RtDenoiser::makeImage(VkFormat format, uint32_t w, uint32_t h) const {
        Image img;
        VkImageCreateInfo ii{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = format;
        ii.extent = { w, h, 1 };
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(m_context->getAllocator(), &ii, &ai, &img.image, &img.alloc, nullptr) != VK_SUCCESS)
            throw std::runtime_error("RtDenoiser: failed to create image");

        VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = img.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(m_context->getDevice(), &vi, nullptr, &img.view) != VK_SUCCESS)
            throw std::runtime_error("RtDenoiser: failed to create image view");
        return img;
    }

    void RtDenoiser::createImages() {
        m_imgS = makeImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_giWidth, m_giHeight);
        m_imgA = makeImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_giWidth, m_giHeight);
        for (auto& g : m_geom) g = makeImage(VK_FORMAT_R32G32B32A32_SFLOAT, m_giWidth, m_giHeight);
        for (auto& h : m_hist) h = makeImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_giWidth, m_giHeight);
        m_imgRefl = makeImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_width, m_height);
        for (auto& rh : m_imgReflHist) rh = makeImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_width, m_height);
        for (auto& m : m_moments) m = makeImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_giWidth, m_giHeight);
    }

    void RtDenoiser::destroyImages() {
        auto destroy = [&](Image& i) {
            if (i.view) vkDestroyImageView(m_context->getDevice(), i.view, nullptr);
            if (i.image) vmaDestroyImage(m_context->getAllocator(), i.image, i.alloc);
            i = {};
        };
        destroy(m_imgS);
        destroy(m_imgA);
        for (auto& g : m_geom) destroy(g);
        for (auto& h : m_hist) destroy(h);
        destroy(m_imgRefl);
        for (auto& rh : m_imgReflHist) destroy(rh);
        for (auto& m : m_moments) destroy(m);
    }

    void RtDenoiser::writeDescriptors(VkImageView depthView, VkSampler depthSampler,
                                      VkImageView gNormalRoughnessView, VkImageView gAlbedoMetallicView,
                                      VkImageView gMotionView) {
        for (uint32_t p = 0; p < 2; ++p) {
            const uint32_t q = 1 - p;
            const VkDescriptorImageInfo depthInfo{ depthSampler, depthView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
            const VkImageView storageViews[6] = { m_imgS.view, m_imgA.view, m_geom[p].view,
                                                  m_geom[q].view, m_hist[p].view, m_hist[q].view };
            VkDescriptorImageInfo storageInfos[6];
            VkWriteDescriptorSet writes[15]{};
            writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[0].dstSet = m_sets[p];
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &depthInfo;
            for (uint32_t i = 0; i < 6; ++i) {
                storageInfos[i] = { VK_NULL_HANDLE, storageViews[i], VK_IMAGE_LAYOUT_GENERAL };
                writes[i + 1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                writes[i + 1].dstSet = m_sets[p];
                writes[i + 1].dstBinding = i + 1;
                writes[i + 1].descriptorCount = 1;
                writes[i + 1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                writes[i + 1].pImageInfo = &storageInfos[i];
            }
            const VkDescriptorImageInfo gNRInfo{ m_sampler, gNormalRoughnessView ? gNormalRoughnessView : depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            writes[7] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[7].dstSet = m_sets[p];
            writes[7].dstBinding = 7;
            writes[7].descriptorCount = 1;
            writes[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[7].pImageInfo = &gNRInfo;

            const VkDescriptorImageInfo gAMInfo{ m_sampler, gAlbedoMetallicView ? gAlbedoMetallicView : depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            writes[8] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[8].dstSet = m_sets[p];
            writes[8].dstBinding = 8;
            writes[8].descriptorCount = 1;
            writes[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[8].pImageInfo = &gAMInfo;

            const VkDescriptorImageInfo reflInfo{ VK_NULL_HANDLE, m_imgRefl.view, VK_IMAGE_LAYOUT_GENERAL };
            writes[9] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[9].dstSet = m_sets[p];
            writes[9].dstBinding = 9;
            writes[9].descriptorCount = 1;
            writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[9].pImageInfo = &reflInfo;

            const VkDescriptorImageInfo reflHistCurInfo{ VK_NULL_HANDLE, m_imgReflHist[p].view, VK_IMAGE_LAYOUT_GENERAL };
            writes[10] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[10].dstSet = m_sets[p];
            writes[10].dstBinding = 10;
            writes[10].descriptorCount = 1;
            writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[10].pImageInfo = &reflHistCurInfo;

            const VkDescriptorImageInfo reflHistPrevInfo{ VK_NULL_HANDLE, m_imgReflHist[q].view, VK_IMAGE_LAYOUT_GENERAL };
            writes[11] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[11].dstSet = m_sets[p];
            writes[11].dstBinding = 11;
            writes[11].descriptorCount = 1;
            writes[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[11].pImageInfo = &reflHistPrevInfo;

            const VkDescriptorImageInfo motionInfo{ m_sampler, gMotionView ? gMotionView : depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            writes[12] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[12].dstSet = m_sets[p];
            writes[12].dstBinding = 12;
            writes[12].descriptorCount = 1;
            writes[12].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[12].pImageInfo = &motionInfo;

            const VkDescriptorImageInfo momentsCurInfo{ VK_NULL_HANDLE, m_moments[p].view, VK_IMAGE_LAYOUT_GENERAL };
            writes[13] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[13].dstSet = m_sets[p];
            writes[13].dstBinding = 13;
            writes[13].descriptorCount = 1;
            writes[13].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[13].pImageInfo = &momentsCurInfo;

            const VkDescriptorImageInfo momentsPrevInfo{ VK_NULL_HANDLE, m_moments[q].view, VK_IMAGE_LAYOUT_GENERAL };
            writes[14] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[14].dstSet = m_sets[p];
            writes[14].dstBinding = 14;
            writes[14].descriptorCount = 1;
            writes[14].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[14].pImageInfo = &momentsPrevInfo;

            vkUpdateDescriptorSets(m_context->getDevice(), 15, writes, 0, nullptr);
        }
    }

    void RtDenoiser::record(VkCommandBuffer cmd, VkDescriptorSet sceneSet, const Params& params) {
        if (!m_ready) return;

        // All images live permanently in GENERAL (storage writes + sampled reads in the PBR pass).
        if (m_needsLayoutInit) {
            const Image* all[11] = {
                &m_imgS, &m_imgA, &m_geom[0], &m_geom[1], &m_hist[0], &m_hist[1],
                &m_imgRefl, &m_imgReflHist[0], &m_imgReflHist[1],
                &m_moments[0], &m_moments[1]
            };
            VkImageMemoryBarrier2 bars[11]{};
            for (int i = 0; i < 11; ++i) {
                bars[i] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                bars[i].srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                bars[i].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                bars[i].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
                bars[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                bars[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
                bars[i].srcQueueFamilyIndex = bars[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bars[i].image = all[i]->image;
                bars[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            }
            VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            dep.imageMemoryBarrierCount = 11;
            dep.pImageMemoryBarriers = bars;
            vkCmdPipelineBarrier2(cmd, &dep);
            m_needsLayoutInit = false;
            m_historyValid = false;
            m_giHistoryValid = false;
        }

        // Retrieve previous frame's GPU timestamps if available
        if (params.lightChanged) {
            m_historyValid = false;
            m_giHistoryValid = false;
        }
        m_gpuTimings = {};
        if (m_timestampsSupported && m_queryPool && m_hasTimestampResults) {
            uint64_t timestamps[13]{};
            VkResult res = vkGetQueryPoolResults(m_context->getDevice(), m_queryPool, 0, 13,
                                                 sizeof(timestamps), timestamps, sizeof(uint64_t),
                                                 VK_QUERY_RESULT_64_BIT);
            if (res == VK_SUCCESS) {
                const uint32_t validBits = m_context->getTimestampValidBits();
                const uint64_t mask = validBits >= 64
                    ? UINT64_MAX
                    : ((uint64_t{1} << validBits) - 1u);
                const double periodNs = static_cast<double>(
                    m_context->getPhysicalDeviceProperties().limits.timestampPeriod);
                auto toMs = [&](uint32_t begin, uint32_t end) -> float {
                    const uint64_t ticks = (timestamps[end] - timestamps[begin]) & mask;
                    return static_cast<float>(static_cast<double>(ticks) * periodNs * 1e-6);
                };

                // Inclusive stage times preserve existing profiler/report semantics.
                m_gpuTimings.traceMs        = toMs(0, 2);
                m_gpuTimings.temporalMs     = toMs(2, 4);
                m_gpuTimings.atrous0Ms      = toMs(4, 6);
                m_gpuTimings.atrous1Ms      = toMs(6, 7);
                m_gpuTimings.reflTraceMs    = toMs(7, 10);
                m_gpuTimings.reflTemporalMs = toMs(10, 12);
                m_gpuTimings.totalMs        = toMs(0, 12);

                // Dispatch execution excludes the following dependency barrier.
                m_gpuTimings.traceExecMs        = toMs(0, 1);
                m_gpuTimings.traceSyncMs        = toMs(1, 2);
                m_gpuTimings.temporalExecMs     = toMs(2, 3);
                m_gpuTimings.temporalSyncMs     = toMs(3, 4);
                m_gpuTimings.atrous0ExecMs      = toMs(4, 5);
                m_gpuTimings.atrous0SyncMs      = toMs(5, 6);
                m_gpuTimings.reflStartSyncMs    = toMs(7, 8);
                m_gpuTimings.reflTraceExecMs    = toMs(8, 9);
                m_gpuTimings.reflTraceSyncMs    = toMs(9, 10);
                m_gpuTimings.reflTemporalExecMs = toMs(10, 11);
                m_gpuTimings.reflTemporalSyncMs = toMs(11, 12);
            }
        }
        auto& profiler = core::Profiler::get();
        profiler.recordMetric("rtGiTraceMs", m_gpuTimings.traceMs);
        profiler.recordMetric("rtGiTemporalMs", m_gpuTimings.temporalMs);
        profiler.recordMetric("rtGiAtrous0Ms", m_gpuTimings.atrous0Ms);
        profiler.recordMetric("rtGiAtrous1Ms", m_gpuTimings.atrous1Ms);
        profiler.recordMetric("rtReflectionTraceMs", m_gpuTimings.reflTraceMs);
        profiler.recordMetric("rtReflectionTemporalMs", m_gpuTimings.reflTemporalMs);
        profiler.recordMetric("rtComputeTotalMs", m_gpuTimings.totalMs);

        profiler.recordMetric("rtGiTraceExecMs", m_gpuTimings.traceExecMs);
        profiler.recordMetric("rtGiTraceSyncMs", m_gpuTimings.traceSyncMs);
        profiler.recordMetric("rtGiTemporalExecMs", m_gpuTimings.temporalExecMs);
        profiler.recordMetric("rtGiTemporalSyncMs", m_gpuTimings.temporalSyncMs);
        profiler.recordMetric("rtGiAtrous0ExecMs", m_gpuTimings.atrous0ExecMs);
        profiler.recordMetric("rtGiAtrous0SyncMs", m_gpuTimings.atrous0SyncMs);
        profiler.recordMetric("rtReflectionStartSyncMs", m_gpuTimings.reflStartSyncMs);
        profiler.recordMetric("rtReflectionTraceExecMs", m_gpuTimings.reflTraceExecMs);
        profiler.recordMetric("rtReflectionTraceSyncMs", m_gpuTimings.reflTraceSyncMs);
        profiler.recordMetric("rtReflectionTemporalExecMs", m_gpuTimings.reflTemporalExecMs);
        profiler.recordMetric("rtReflectionTemporalSyncMs", m_gpuTimings.reflTemporalSyncMs);

        // Quality telemetry is observational only; it never changes RT settings.
        profiler.recordMetric("rtGiSamples", std::clamp(params.samples, 1u, 16u));
        profiler.recordMetric("rtGiBounces", params.bounces);
        profiler.recordMetric("rtGiWidth", m_giWidth);
        profiler.recordMetric("rtGiHeight", m_giHeight);
        profiler.recordMetric("rtMaxHistory", params.maxHistory);
        profiler.recordMetric("rtSpatialStrength", params.spatialStrength);
        profiler.recordMetric("rtSvgfEnabled", params.svgfEnabled ? 1.0 : 0.0);
        profiler.recordMetric("rtGiCheckerboard", params.giCheckerboard ? 1.0 : 0.0);
        profiler.recordMetric("rtGiHistoryValid",
            (params.giEnabled || params.aoEnabled || params.debugMode != RtDebugMode::None) && m_giHistoryValid ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionSamples", params.reflectionSamples);
        profiler.recordMetric("rtReflectionBounces", params.reflectionBounces);
        profiler.recordMetric("rtReflectionInfiniteBounces", params.reflectionInfiniteBounces ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionMaxRoughness", params.reflectionMaxRoughness);
        profiler.recordMetric("rtReflectionCheckerboard", params.reflectionCheckerboard ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionStochasticLights", params.reflectionStochasticLights ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionLodBias", params.reflectionLodBias ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionHalfRes", params.reflectionHalfRes ? 1.0 : 0.0);
        profiler.recordMetric("rtDebugMode", static_cast<uint32_t>(params.debugMode));

        // Reset query pool before recording timestamps
        if (m_timestampsSupported && m_queryPool) {
            vkCmdResetQueryPool(cmd, m_queryPool, 0, 13);
        }

        // Depth (prepass) + everything the previous frame left behind -> this frame's compute.
        memoryBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | kStorageRW);

        const VkDescriptorSet sets[2] = { sceneSet, m_sets[m_parity] };
        const uint32_t gx = (m_width + 7u) / 8u;
        const uint32_t gy = (m_height + 7u) / 8u;
        const uint32_t giGx = (m_giWidth + 7u) / 8u;
        const uint32_t giGy = (m_giHeight + 7u) / 8u;
        const auto sync = [&]() {
            memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, kStorageRW,
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, kStorageRW);
        };
        const auto bindGI = [&](VkPipeline pipeline, const PushData& pd) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 2, sets, 0, nullptr);
            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushData), &pd);
            vkCmdDispatch(cmd, giGx, giGy, 1);
        };
        const auto bindRefl = [&](VkPipeline pipeline, const PushData& pd) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 2, sets, 0, nullptr);
            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushData), &pd);
            vkCmdDispatch(cmd, gx, gy, 1);
        };

        const uint32_t samples = std::clamp(params.samples, 1u, 16u);
        PushData pd{};
        pd.m = m_prevViewProj;
        pd.f = glm::vec4(std::max(params.maxHistory, 1.0f), static_cast<float>(samples),
                         m_giHistoryValid ? 1.0f : 0.0f, params.svgfEnabled ? 1.0f : 0.0f);
        pd.u[0] = samples;
        pd.u[1] = params.frameIndex;
        pd.u[2] = (params.giEnabled ? 1u : 0u) | (params.aoEnabled ? 2u : 0u) | (params.giCheckerboard ? 4u : 0u);
        pd.u[3] = std::clamp(params.bounces, 1u, 15u);

        const bool giOrAoActive = params.giEnabled || params.aoEnabled || params.debugMode != RtDebugMode::None;
        if (giOrAoActive) {
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 0);

            bindGI(m_tracePipelineHandle, pd); // 1. trace
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 1);
            sync();
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 2);

            bindGI(m_temporalPipeline, pd); // 2. temporal accumulation
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 3);
            sync();
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 4);

            // Pass 3: Atrous step 1 (hist -> A).
            pd.f.x = std::clamp(params.spatialStrength, 0.0f, 1.0f);
            pd.f.y = std::max(params.maxHistory, 1.0f);
            pd.u[0] = 0u;
            bindGI(m_atrousPipelines[0], pd);
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 5);
            sync();
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 6);

            // Pass 4: Atrous step 2 (A -> S, final output).
            pd.u[0] = static_cast<uint32_t>(params.debugMode);
            bindGI(m_atrousPipelines[1], pd);
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 7);
            m_giHistoryValid = true;
        } else {
            // Keep the sampled output valid and neutral for the PBR combine (rgb=0, visibility=1).
            VkImageMemoryBarrier2 toClear{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
            toClear.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            toClear.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            toClear.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            toClear.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            toClear.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            toClear.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            toClear.srcQueueFamilyIndex = toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toClear.image = m_imgS.image;
            toClear.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            VkDependencyInfo clearBefore{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            clearBefore.imageMemoryBarrierCount = 1;
            clearBefore.pImageMemoryBarriers = &toClear;
            vkCmdPipelineBarrier2(cmd, &clearBefore);

            VkClearColorValue neutralGi{};
            neutralGi.float32[3] = 1.0f;
            const VkImageSubresourceRange colorRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdClearColorImage(cmd, m_imgS.image, VK_IMAGE_LAYOUT_GENERAL, &neutralGi, 1, &colorRange);

            VkImageMemoryBarrier2 afterClear{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
            afterClear.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            afterClear.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            afterClear.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            afterClear.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            afterClear.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            afterClear.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            afterClear.srcQueueFamilyIndex = afterClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            afterClear.image = m_imgS.image;
            afterClear.subresourceRange = colorRange;
            VkDependencyInfo clearAfter{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            clearAfter.imageMemoryBarrierCount = 1;
            clearAfter.pImageMemoryBarriers = &afterClear;
            vkCmdPipelineBarrier2(cmd, &clearAfter);

            // Fill unused GI boundaries with equal timestamps; the profiler reports zero work.
            if (m_timestampsSupported && m_queryPool) {
                for (uint32_t query = 0; query <= 7; ++query)
                    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, query);
            }
            m_giHistoryValid = false;
        }

        // Reflection work starts after the GI output dependency. Keep this barrier explicit
        // and measure its scheduling gap separately from the reflection dispatch itself.
        if (params.reflectionsEnabled && m_reflPipeline && m_reflTemporalPipeline) {
            sync();
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 8);

            PushData reflPd{};
            reflPd.m = m_prevViewProj;
            reflPd.f = glm::vec4(std::max(params.maxHistory, 1.0f), static_cast<float>(params.reflectionSamples),
                                 m_historyValid ? 1.0f : 0.0f, std::clamp(params.reflectionMaxRoughness, 0.02f, 1.0f));
            reflPd.u[0] = std::clamp(params.reflectionSamples, 1u, 16u);
            reflPd.u[1] = params.frameIndex;
            reflPd.u[2] = std::bit_cast<uint32_t>(std::clamp(params.reflectionMinThroughput, 0.001f, 1.0f));

            uint32_t reflFlags = 0;
            if (params.reflectionCheckerboard)     reflFlags |= (1u << 8);
            if (params.reflectionStochasticLights) reflFlags |= (1u << 9);
            if (params.reflectionLodBias)          reflFlags |= (1u << 10);
            if (params.reflectionHalfRes)          reflFlags |= (1u << 11);
            reflPd.u[3] = (params.reflectionInfiniteBounces
                ? (0x80000000u | 8u)
                : std::clamp(params.reflectionBounces, 2u, 4u)) | reflFlags;

            bindRefl(m_reflPipeline, reflPd);
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 9);
            sync();
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 10);

            bindRefl(m_reflTemporalPipeline, reflPd);
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 11);
            sync();
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 12);
        } else if (m_timestampsSupported && m_queryPool) {
            for (uint32_t query = 8; query <= 12; ++query)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, query);
        }

        memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        m_prevViewProj = params.viewProj;
        m_historyValid = true;
        m_parity ^= 1u;
        m_hasTimestampResults = m_timestampsSupported;
    }
}
