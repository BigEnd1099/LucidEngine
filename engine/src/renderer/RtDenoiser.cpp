#include "renderer/RtDenoiser.hpp"
#include "core/Logger.hpp"
#include "core/Profiler.hpp"
#include "utils/FileIO.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
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

        constexpr uint32_t kRtDiagnosticCounterCount = 56u;
        constexpr VkAccessFlags2 kStorageRW = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    }

    // Set 1 bindings: image resources 0-14, binding 15 = RT diagnostic counters (SSBO).
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

        VkDescriptorSetLayoutBinding b[22]{};
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
        b[15].binding = 15;
        b[15].descriptorCount = 1;
        b[15].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[15].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        for (uint32_t i = 16; i < 20; ++i) {
            b[i].binding = i;
            b[i].descriptorCount = 1;
            b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        b[20].binding = 20;
        b[20].descriptorCount = 1;
        b[20].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[20].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        b[21].binding = 21;
        b[21].descriptorCount = 1;
        b[21].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[21].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutCreateInfo li{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        li.bindingCount = 22;
        li.pBindings = b;
        if (vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_setLayout) != VK_SUCCESS)
            throw std::runtime_error("RtDenoiser: failed to create set layout");

        const VkDescriptorPoolSize sizes[3] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 40 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8 }
        };
        VkDescriptorPoolCreateInfo pi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pi.maxSets = 2;
        pi.poolSizeCount = 3;
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
        VkShaderModule reflSpatial = loadModule("rt_refl_spatial.comp.spv");
        m_tracePipelineHandle = createPipeline(trace, -1);
        m_temporalPipeline = createPipeline(temporal, -1);
        m_atrousPipelines[0] = createPipeline(atrous, 0);
        m_atrousPipelines[1] = createPipeline(atrous, 1);
        m_atrousPipelines[2] = createPipeline(atrous, 2);
        m_atrousPipelines[3] = createPipeline(atrous, 3);
        m_reflPipeline = createPipeline(refl, -1);
        m_reflTemporalPipeline = createPipeline(reflTemp, -1);
        m_reflSpatialPipeline = createPipeline(reflSpatial, -1);
        vkDestroyShaderModule(dev, reflSpatial, nullptr);
        vkDestroyShaderModule(dev, reflTemp, nullptr);
        vkDestroyShaderModule(dev, refl, nullptr);
        vkDestroyShaderModule(dev, trace, nullptr);
        vkDestroyShaderModule(dev, temporal, nullptr);
        vkDestroyShaderModule(dev, atrous, nullptr);

        createImages();
        VkBufferCreateInfo diagnosticsInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        diagnosticsInfo.size = sizeof(uint32_t) * kRtDiagnosticCounterCount;
        diagnosticsInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        diagnosticsInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo diagnosticsAlloc{};
        diagnosticsAlloc.usage = VMA_MEMORY_USAGE_AUTO;
        diagnosticsAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        diagnosticsAlloc.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        VmaAllocationInfo diagnosticsAllocationInfo{};
        if (vmaCreateBuffer(m_context->getAllocator(), &diagnosticsInfo, &diagnosticsAlloc,
                            &m_diagnosticsBuffer, &m_diagnosticsAllocation, &diagnosticsAllocationInfo) != VK_SUCCESS)
            throw std::runtime_error("RtDenoiser: failed to create diagnostics buffer");
        m_diagnosticsMapped = static_cast<uint32_t*>(diagnosticsAllocationInfo.pMappedData);
        VkPhysicalDeviceMemoryProperties diagnosticsMemoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(m_context->getPhysicalDevice(), &diagnosticsMemoryProperties);
        m_diagnosticsCoherent = diagnosticsAllocationInfo.memoryType < diagnosticsMemoryProperties.memoryTypeCount &&
            ((diagnosticsMemoryProperties.memoryTypes[diagnosticsAllocationInfo.memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0);
        if (!m_diagnosticsMapped)
            throw std::runtime_error("RtDenoiser: diagnostics allocation is not persistently mapped");
        std::memset(m_diagnosticsMapped, 0, sizeof(uint32_t) * kRtDiagnosticCounterCount);

        // Dedicated 25 cm surface-history hash table (48 bytes/cell, 12 MiB).
        // It is separate from the lower-resolution radiance-probe cache at binding 20.
        constexpr VkDeviceSize kSurfaceHistoryBytes = VkDeviceSize(48u) * 262144u;
        VkBufferCreateInfo surfaceInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        surfaceInfo.size = kSurfaceHistoryBytes;
        surfaceInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        surfaceInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo surfaceAlloc{};
        surfaceAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateBuffer(m_context->getAllocator(), &surfaceInfo, &surfaceAlloc,
                            &m_surfaceHistoryBuffer, &m_surfaceHistoryAllocation, nullptr) != VK_SUCCESS)
            throw std::runtime_error("RtDenoiser: failed to allocate surface-history cache");
        if (!m_diagnosticsCoherent)
            vmaFlushAllocation(m_context->getAllocator(), m_diagnosticsAllocation, 0, sizeof(uint32_t) * kRtDiagnosticCounterCount);
        writeDescriptors(depthView, depthSampler, gNormalRoughnessView, gAlbedoMetallicView, gMotionView);

        const auto& props = m_context->getPhysicalDeviceProperties();
        m_timestampsSupported = (props.limits.timestampPeriod > 0.0f) && (m_context->getTimestampValidBits() > 0);
        if (m_timestampsSupported) {
            VkQueryPoolCreateInfo qpci{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
            qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qpci.queryCount = 19;
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
        m_reflHistoryValid = false;
        m_giHistoryValid = false;
    }

    void RtDenoiser::cleanup() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice dev = m_context->getDevice();
        destroyImages();
        if (m_diagnosticsBuffer) vmaDestroyBuffer(m_context->getAllocator(), m_diagnosticsBuffer, m_diagnosticsAllocation);
        if (m_surfaceHistoryBuffer) vmaDestroyBuffer(m_context->getAllocator(), m_surfaceHistoryBuffer, m_surfaceHistoryAllocation);
        m_diagnosticsBuffer = VK_NULL_HANDLE;
        m_surfaceHistoryBuffer = VK_NULL_HANDLE;
        m_surfaceHistoryAllocation = VK_NULL_HANDLE;
        m_surfaceHistoryNeedsClear = true;
        m_surfaceHistoryLightingEpoch = 1u;
        m_surfaceHistoryCacheStateKnown = false;
        m_surfaceHistoryCacheWasEnabled = true;
        m_worldCacheBuffer = VK_NULL_HANDLE;
        m_diagnosticsAllocation = VK_NULL_HANDLE;
        m_diagnosticsMapped = nullptr;
        for (auto& p : m_atrousPipelines) { if (p) vkDestroyPipeline(dev, p, nullptr); p = VK_NULL_HANDLE; }
        if (m_temporalPipeline) vkDestroyPipeline(dev, m_temporalPipeline, nullptr);
        if (m_tracePipelineHandle) vkDestroyPipeline(dev, m_tracePipelineHandle, nullptr);
        if (m_reflSpatialPipeline) vkDestroyPipeline(dev, m_reflSpatialPipeline, nullptr);
        if (m_reflTemporalPipeline) vkDestroyPipeline(dev, m_reflTemporalPipeline, nullptr);
        if (m_reflPipeline) vkDestroyPipeline(dev, m_reflPipeline, nullptr);
        m_reflSpatialPipeline = m_reflTemporalPipeline = m_reflPipeline = VK_NULL_HANDLE;
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
        m_imgRawGi = makeImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_giWidth, m_giHeight);
        m_imgCaustics = makeImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_giWidth, m_giHeight);
        for (auto& g : m_geom) g = makeImage(VK_FORMAT_R32G32B32A32_SFLOAT, m_giWidth, m_giHeight);
        for (auto& h : m_hist) h = makeImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_giWidth, m_giHeight);
        m_imgRefl = makeImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_width, m_height);
        for (auto& rh : m_imgReflHist) rh = makeImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_width, m_height);
        for (auto& rg : m_imgReflGeom) rg = makeImage(VK_FORMAT_R32G32B32A32_SFLOAT, m_width, m_height);
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
        destroy(m_imgRawGi);
        destroy(m_imgCaustics);
        for (auto& g : m_geom) destroy(g);
        for (auto& h : m_hist) destroy(h);
        destroy(m_imgRefl);
        for (auto& rh : m_imgReflHist) destroy(rh);
        for (auto& rg : m_imgReflGeom) destroy(rg);
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
            VkWriteDescriptorSet writes[22]{};
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

            const VkDescriptorBufferInfo diagnosticsBufferInfo{ m_diagnosticsBuffer, 0, sizeof(uint32_t) * kRtDiagnosticCounterCount };
            writes[15] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[15].dstSet = m_sets[p];
            writes[15].dstBinding = 15;
            writes[15].descriptorCount = 1;
            writes[15].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[15].pBufferInfo = &diagnosticsBufferInfo;

            const VkDescriptorImageInfo rawGiInfo{ VK_NULL_HANDLE, m_imgRawGi.view, VK_IMAGE_LAYOUT_GENERAL };
            writes[16] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[16].dstSet = m_sets[p];
            writes[16].dstBinding = 16;
            writes[16].descriptorCount = 1;
            writes[16].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[16].pImageInfo = &rawGiInfo;

            const VkDescriptorImageInfo causticsInfo{ VK_NULL_HANDLE, m_imgCaustics.view, VK_IMAGE_LAYOUT_GENERAL };
            writes[17] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[17].dstSet = m_sets[p];
            writes[17].dstBinding = 17;
            writes[17].descriptorCount = 1;
            writes[17].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[17].pImageInfo = &causticsInfo;

            const VkDescriptorImageInfo reflGeomCurInfo{ VK_NULL_HANDLE, m_imgReflGeom[p].view, VK_IMAGE_LAYOUT_GENERAL };
            writes[18] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[18].dstSet = m_sets[p];
            writes[18].dstBinding = 18;
            writes[18].descriptorCount = 1;
            writes[18].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[18].pImageInfo = &reflGeomCurInfo;

            const VkDescriptorImageInfo reflGeomPrevInfo{ VK_NULL_HANDLE, m_imgReflGeom[q].view, VK_IMAGE_LAYOUT_GENERAL };
            writes[19] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[19].dstSet = m_sets[p];
            writes[19].dstBinding = 19;
            writes[19].descriptorCount = 1;
            writes[19].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[19].pImageInfo = &reflGeomPrevInfo;
            const VkDescriptorBufferInfo worldCacheInfo{
                m_worldCacheBuffer != VK_NULL_HANDLE ? m_worldCacheBuffer : m_diagnosticsBuffer,
                0, VK_WHOLE_SIZE
            };
            writes[20] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[20].dstSet = m_sets[p];
            writes[20].dstBinding = 20;
            writes[20].descriptorCount = 1;
            writes[20].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[20].pBufferInfo = &worldCacheInfo;
            const VkDescriptorBufferInfo surfaceHistoryInfo{ m_surfaceHistoryBuffer, 0, VK_WHOLE_SIZE };
            writes[21] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[21].dstSet = m_sets[p];
            writes[21].dstBinding = 21;
            writes[21].descriptorCount = 1;
            writes[21].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[21].pBufferInfo = &surfaceHistoryInfo;
            vkUpdateDescriptorSets(m_context->getDevice(), 22, writes, 0, nullptr);
        }
    }

    void RtDenoiser::setWorldRadianceCacheBuffer(VkBuffer buffer) {
        m_worldCacheBuffer = buffer;
        if (!m_context || !m_ready || m_diagnosticsBuffer == VK_NULL_HANDLE) return;
        const VkDescriptorBufferInfo cacheInfo{
            m_worldCacheBuffer != VK_NULL_HANDLE ? m_worldCacheBuffer : m_diagnosticsBuffer,
            0, VK_WHOLE_SIZE
        };
        VkWriteDescriptorSet writes[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            writes[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[i].dstSet = m_sets[i];
            writes[i].dstBinding = 20;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &cacheInfo;
        }
        vkUpdateDescriptorSets(m_context->getDevice(), 2, writes, 0, nullptr);
    }

    void RtDenoiser::record(VkCommandBuffer cmd, VkDescriptorSet sceneSet, const Params& params) {
        if (!m_ready) return;

        // All images live permanently in GENERAL (storage writes + sampled reads in the PBR pass).
        if (m_needsLayoutInit) {
            const Image* all[15] = {
                &m_imgS, &m_imgA, &m_imgRawGi, &m_imgCaustics, &m_geom[0], &m_geom[1], &m_hist[0], &m_hist[1],
                &m_imgRefl, &m_imgReflHist[0], &m_imgReflHist[1],
                &m_moments[0], &m_moments[1], &m_imgReflGeom[0], &m_imgReflGeom[1]
            };
            VkImageMemoryBarrier2 bars[15]{};
            for (int i = 0; i < 15; ++i) {
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
            dep.imageMemoryBarrierCount = 15;
            dep.pImageMemoryBarriers = bars;
            vkCmdPipelineBarrier2(cmd, &dep);
            m_needsLayoutInit = false;
            m_reflHistoryValid = false;
            m_giHistoryValid = false;
        }

        // Initialize the persistent table exactly once. Subsequent lighting changes use
        // an epoch tag, avoiding a 12 MiB clear and preserving GPU ordering.
        if (m_surfaceHistoryNeedsClear && m_surfaceHistoryBuffer != VK_NULL_HANDLE) {
            vkCmdFillBuffer(cmd, m_surfaceHistoryBuffer, 0, VK_WHOLE_SIZE, 0u);
            VkBufferMemoryBarrier2 initBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
            initBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            initBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            initBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            initBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            initBarrier.buffer = m_surfaceHistoryBuffer;
            initBarrier.offset = 0;
            initBarrier.size = VK_WHOLE_SIZE;
            VkDependencyInfo initDep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            initDep.bufferMemoryBarrierCount = 1;
            initDep.pBufferMemoryBarriers = &initBarrier;
            vkCmdPipelineBarrier2(cmd, &initDep);
            m_surfaceHistoryNeedsClear = false;
        }

        // The renderer waits for the previous submission fence before record(); counters are
        // therefore safe to invalidate/read here without stalling the GPU or using queue-idle.
        if (m_diagnosticsMapped) {
            if (!m_diagnosticsCoherent)
                vmaInvalidateAllocation(m_context->getAllocator(), m_diagnosticsAllocation, 0, sizeof(uint32_t) * kRtDiagnosticCounterCount);
            static const char* names[kRtDiagnosticCounterCount] = {
                "rtGiPixels", "rtGiHistoryAccepted", "rtGiHistoryRejected", "rtGiOutOfBounds",
                "rtGiDepthRejected", "rtGiNormalRejected", "rtGiCheckerboardReuse", "rtGiSpatialFallback",
                "rtReflectionPixels", "rtReflectionHistoryAccepted", "rtReflectionHistoryRejected", "rtReflectionOutOfBounds",
                "rtReflectionDepthRejected", "rtReflectionNormalRejected", "rtReflectionCheckerboardReuse", "rtReflectionSpatialFallback",
                "rtGiHistoryAgeBin00", "rtGiHistoryAgeBin01", "rtGiHistoryAgeBin02", "rtGiHistoryAgeBin03",
                "rtGiHistoryAgeBin04", "rtGiHistoryAgeBin05", "rtGiHistoryAgeBin06", "rtGiHistoryAgeBin07",
                "rtGiHistoryAgeBin08", "rtGiHistoryAgeBin09", "rtGiHistoryAgeBin10", "rtGiHistoryAgeBin11",
                "rtGiHistoryAgeBin12", "rtGiHistoryAgeBin13", "rtGiHistoryAgeBin14", "rtGiHistoryAgeBin15",
                "rtReflectionHistoryAgeBin00", "rtReflectionHistoryAgeBin01", "rtReflectionHistoryAgeBin02", "rtReflectionHistoryAgeBin03",
                "rtReflectionHistoryAgeBin04", "rtReflectionHistoryAgeBin05", "rtReflectionHistoryAgeBin06", "rtReflectionHistoryAgeBin07",
                "rtReflectionHistoryAgeBin08", "rtReflectionHistoryAgeBin09", "rtReflectionHistoryAgeBin10", "rtReflectionHistoryAgeBin11",
                "rtReflectionHistoryAgeBin12", "rtReflectionHistoryAgeBin13", "rtReflectionHistoryAgeBin14", "rtReflectionHistoryAgeBin15",
                "rtGiSurfaceHits", "rtGiMirrorSurfaceHits", "rtGiCausticLightTests", "rtGiCausticConeHits",
                "rtGiCausticVisibleHits", "rtGiCausticContributions", "rtGiTraceSamples", "rtGiPathBounces"
            };
            auto& diagnosticsProfiler = core::Profiler::get();
            for (uint32_t i = 0; i < kRtDiagnosticCounterCount; ++i) {
                m_lastDiagnostics[i] = m_diagnosticsMapped[i];
                diagnosticsProfiler.recordMetric(names[i], m_lastDiagnostics[i]);
            }
        }

        // Toggle transitions invalidate the persistent GI surface table by advancing its epoch.
        // This avoids a large buffer clear while preventing stale OFF/ON cache reuse.
        if (!m_surfaceHistoryCacheStateKnown) {
            m_surfaceHistoryCacheWasEnabled = params.giSurfaceHistoryCacheEnabled;
            m_surfaceHistoryCacheStateKnown = true;
        } else if (m_surfaceHistoryCacheWasEnabled != params.giSurfaceHistoryCacheEnabled) {
            m_surfaceHistoryCacheWasEnabled = params.giSurfaceHistoryCacheEnabled;
            ++m_surfaceHistoryLightingEpoch;
            if (m_surfaceHistoryLightingEpoch == 0u) m_surfaceHistoryLightingEpoch = 1u;
            m_giHistoryValid = false;
        }

        // Retrieve previous frame's GPU timestamps if available
        if (params.lightChanged) {
            m_reflHistoryValid = false;
            m_giHistoryValid = false;
            ++m_surfaceHistoryLightingEpoch;
            if (m_surfaceHistoryLightingEpoch == 0u) m_surfaceHistoryLightingEpoch = 1u;
        }
        m_gpuTimings = {};
        if (m_timestampsSupported && m_queryPool && m_hasTimestampResults) {
            uint64_t timestamps[19]{};
            VkResult res = vkGetQueryPoolResults(m_context->getDevice(), m_queryPool, 0, 19,
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
                m_gpuTimings.atrous1Ms      = toMs(6, 8);
                m_gpuTimings.atrous2Ms      = toMs(8, 10);
                m_gpuTimings.atrous3Ms      = toMs(10, 12);
                m_gpuTimings.reflTraceMs    = toMs(12, 14);
                m_gpuTimings.reflTemporalMs = toMs(14, 16);
                m_gpuTimings.reflSpatialMs  = toMs(16, 18);
                m_gpuTimings.totalMs        = toMs(0, 18);

                // Dispatch execution excludes the following dependency barrier.
                m_gpuTimings.traceExecMs        = toMs(0, 1);
                m_gpuTimings.traceSyncMs        = toMs(1, 2);
                m_gpuTimings.temporalExecMs     = toMs(2, 3);
                m_gpuTimings.temporalSyncMs     = toMs(3, 4);
                m_gpuTimings.atrous0ExecMs      = toMs(4, 5);
                m_gpuTimings.atrous0SyncMs      = toMs(5, 6);
                m_gpuTimings.reflStartSyncMs    = toMs(11, 12);
                m_gpuTimings.reflTraceExecMs    = toMs(12, 13);
                m_gpuTimings.reflTraceSyncMs    = toMs(13, 14);
                m_gpuTimings.reflTemporalExecMs = toMs(14, 15);
                m_gpuTimings.reflTemporalSyncMs = toMs(15, 16);
                m_gpuTimings.reflSpatialExecMs  = toMs(16, 17);
                m_gpuTimings.reflSpatialSyncMs  = toMs(17, 18);
            }
        }
        auto& profiler = core::Profiler::get();
        profiler.recordMetric("rtGiTraceMs", m_gpuTimings.traceMs);
        profiler.recordMetric("rtGiTemporalMs", m_gpuTimings.temporalMs);
        profiler.recordMetric("rtGiAtrous0Ms", m_gpuTimings.atrous0Ms);
        profiler.recordMetric("rtGiAtrous1Ms", m_gpuTimings.atrous1Ms);
        profiler.recordMetric("rtGiAtrous2Ms", m_gpuTimings.atrous2Ms);
        profiler.recordMetric("rtGiAtrous3Ms", m_gpuTimings.atrous3Ms);
        profiler.recordMetric("rtReflectionTraceMs", m_gpuTimings.reflTraceMs);
        profiler.recordMetric("rtReflectionTemporalMs", m_gpuTimings.reflTemporalMs);
        profiler.recordMetric("rtReflectionSpatialMs", m_gpuTimings.reflSpatialMs);
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
        profiler.recordMetric("rtWorldRadianceCacheEnabled", params.worldRadianceCacheEnabled ? 1.0 : 0.0);
        profiler.recordMetric("rtGiSurfaceHistoryCacheEnabled", params.giSurfaceHistoryCacheEnabled ? 1.0 : 0.0);
        profiler.recordMetric("rtGiCheckerboard", (params.giCheckerboard && params.giTemporalFilterEnabled) ? 1.0 : 0.0);
        profiler.recordMetric("rtGiTemporalFilterEnabled", params.giTemporalFilterEnabled ? 1.0 : 0.0);
        profiler.recordMetric("rtGiAtrousPass1Enabled", params.giAtrousPassEnabled[0] ? 1.0 : 0.0);
        profiler.recordMetric("rtGiAtrousPass2Enabled", params.giAtrousPassEnabled[1] ? 1.0 : 0.0);
        profiler.recordMetric("rtGiAtrousPass3Enabled", params.giAtrousPassEnabled[2] ? 1.0 : 0.0);
        profiler.recordMetric("rtGiAtrousPass4Enabled", params.giAtrousPassEnabled[3] ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionTemporalFilterEnabled", params.reflectionTemporalFilterEnabled ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionOutlierFilterEnabled", params.reflectionOutlierFilterEnabled ? 1.0 : 0.0);
        profiler.recordMetric("rtGiHistoryClampEnabled", params.giHistoryClampEnabled ? 1.0 : 0.0);
        profiler.recordMetric("rtGiTemporalFireflyFilterEnabled", params.giTemporalFireflyFilterEnabled ? 1.0 : 0.0);
        profiler.recordMetric("rtGiTraceFireflyClampEnabled", params.giTraceFireflyClampEnabled ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionSpatialFilterEnabled", params.reflectionSpatialFilterEnabled ? 1.0 : 0.0);
        profiler.recordMetric("rtGiHistoryValid",
            (params.giEnabled || params.aoEnabled || params.debugMode != RtDebugMode::None) && m_giHistoryValid ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionHistoryValid",
            params.reflectionsEnabled && m_reflHistoryValid ? 1.0 : 0.0);
        profiler.recordMetric("rtHistoryInvalidatedByLightChange", params.lightChanged ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionSamples", params.reflectionSamples);
        profiler.recordMetric("rtReflectionBounces", params.reflectionBounces);
        profiler.recordMetric("rtReflectionInfiniteBounces", params.reflectionInfiniteBounces ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionMaxRoughness", params.reflectionMaxRoughness);
        profiler.recordMetric("rtReflectionCheckerboard", params.reflectionCheckerboard ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionStochasticLights", params.reflectionStochasticLights ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionPrimaryStochasticLights", params.reflectionPrimaryStochasticLights ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionLodBias", params.reflectionLodBias ? 1.0 : 0.0);
        profiler.recordMetric("rtReflectionHalfRes", params.reflectionHalfRes ? 1.0 : 0.0);
        profiler.recordMetric("rtDebugMode", static_cast<uint32_t>(params.debugMode));

        // Reset query pool before recording timestamps
        if (m_timestampsSupported && m_queryPool) {
            vkCmdResetQueryPool(cmd, m_queryPool, 0, 19);
        }

        // Reset per-frame RT rejection counters. Transfer writes become visible to compute atomics.
        vkCmdFillBuffer(cmd, m_diagnosticsBuffer, 0, sizeof(uint32_t) * kRtDiagnosticCounterCount, 0u);
        VkBufferMemoryBarrier2 diagnosticsResetBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        diagnosticsResetBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        diagnosticsResetBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        diagnosticsResetBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        diagnosticsResetBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        diagnosticsResetBarrier.srcQueueFamilyIndex = diagnosticsResetBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        diagnosticsResetBarrier.buffer = m_diagnosticsBuffer;
        diagnosticsResetBarrier.offset = 0;
        diagnosticsResetBarrier.size = VK_WHOLE_SIZE;
        VkDependencyInfo diagnosticsResetDependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        diagnosticsResetDependency.bufferMemoryBarrierCount = 1;
        diagnosticsResetDependency.pBufferMemoryBarriers = &diagnosticsResetBarrier;
        vkCmdPipelineBarrier2(cmd, &diagnosticsResetDependency);

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
                         m_giHistoryValid ? 1.0f : 0.0f,
                         (params.svgfEnabled && params.giTemporalFilterEnabled) ? 1.0f : 0.0f);
        pd.u[0] = samples;
        pd.u[1] = params.frameIndex;
        pd.u[2] = (params.giEnabled ? 1u : 0u) | (params.aoEnabled ? 2u : 0u) |
                  ((params.giCheckerboard && params.giTemporalFilterEnabled) ? 4u : 0u) |
                  (params.giTraceFireflyClampEnabled ? 8u : 0u);
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

            pd.u[0] = (params.giTemporalFilterEnabled ? 0u : 1u) |
                      (params.giSurfaceHistoryCacheEnabled ? 2u : 0u) |
                      (params.worldRadianceCacheEnabled ? 4u : 0u); // bit 0 bypass, bit 1 surface cache, bit 2 world cache
            pd.u[1] = (params.giHistoryClampEnabled ? 1u : 0u) |
                      (params.giTemporalFireflyFilterEnabled ? 2u : 0u) |
                      (params.debugMode == RtDebugMode::WorldRadianceCacheHits ? 4u : 0u) |
                      (params.debugMode == RtDebugMode::SurfaceHistoryCacheHits ? 8u : 0u);
            pd.u[2] = m_surfaceHistoryLightingEpoch;
            pd.u[3] = params.frameIndex;
            bindGI(m_temporalPipeline, pd); // 2. temporal accumulation
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 3);
            sync();
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 4);

            // Pass 3: Atrous step 1 (hist -> A, step 1)
            pd.f.x = params.giAtrousPassEnabled[0] ? std::clamp(params.spatialStrength, 0.0f, 1.0f) : 0.0f;
            pd.f.y = std::max(params.maxHistory, 1.0f);
            pd.u[0] = 0u;
            pd.u[1] = params.giSurfaceHistoryCacheEnabled ? 1u : 0u;
            bindGI(m_atrousPipelines[0], pd);
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 5);
            sync();
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 6);

            // Pass 4: Atrous step 2 (A -> S, step 2)
            pd.f.x = params.giAtrousPassEnabled[1] ? std::clamp(params.spatialStrength, 0.0f, 1.0f) : 0.0f;
            bindGI(m_atrousPipelines[1], pd);
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 7);
            sync();
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 8);

            // Pass 5: Atrous step 3 (S -> A, step 4)
            pd.f.x = params.giAtrousPassEnabled[2] ? std::clamp(params.spatialStrength, 0.0f, 1.0f) : 0.0f;
            bindGI(m_atrousPipelines[2], pd);
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 9);
            sync();
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 10);

            // Pass 6: Atrous step 4 (A -> S, step 8, final output with debugMode)
            pd.f.x = params.giAtrousPassEnabled[3] ? std::clamp(params.spatialStrength, 0.0f, 1.0f) : 0.0f;
            pd.u[0] = static_cast<uint32_t>(params.debugMode);
            bindGI(m_atrousPipelines[3], pd);
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 11);
            m_giHistoryValid = params.giTemporalFilterEnabled;
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
                for (uint32_t query = 0; query <= 11; ++query)
                    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, query);
            }
            m_giHistoryValid = false;
        }

        // Reflection work starts after the GI output dependency. Keep this barrier explicit
        // and measure its scheduling gap separately from the reflection dispatch itself.
        bool reflectionPassRan = false;
        if (params.reflectionsEnabled && m_reflPipeline && m_reflTemporalPipeline) {
            sync();
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 12);

            PushData reflPd{};
            reflPd.m = m_prevViewProj;
            reflPd.f = glm::vec4(std::max(params.maxHistory, 1.0f), static_cast<float>(params.reflectionSamples),
                                 m_reflHistoryValid ? 1.0f : 0.0f, std::clamp(params.reflectionMaxRoughness, 0.02f, 1.0f));
            reflPd.u[0] = std::clamp(params.reflectionSamples, 1u, 16u);
            reflPd.u[1] = params.frameIndex;
            reflPd.u[2] = std::bit_cast<uint32_t>(std::clamp(params.reflectionMinThroughput, 0.001f, 1.0f));

            uint32_t reflFlags = 0;
            if (params.reflectionCheckerboard && params.reflectionTemporalFilterEnabled) reflFlags |= (1u << 8);
            if (params.reflectionStochasticLights) reflFlags |= (1u << 9);
            if (params.reflectionLodBias)          reflFlags |= (1u << 10);
            if (params.reflectionHalfRes)          reflFlags |= (1u << 11);
            if (params.reflectionPrimaryStochasticLights) reflFlags |= (1u << 12);
            if (params.reflectionShowLightBulbs)   reflFlags |= (1u << 13);
            if (params.reflectionDenoiseMirrors)   reflFlags |= (1u << 14);
            if (!params.reflectionTemporalFilterEnabled) reflFlags |= (1u << 15);
            if (params.reflectionOutlierFilterEnabled) reflFlags |= (1u << 16);
            reflPd.u[3] = (params.reflectionInfiniteBounces
                ? (0x80000000u | 8u)
                : std::clamp(params.reflectionBounces, 2u, 4u)) | reflFlags;

            bindRefl(m_reflPipeline, reflPd);
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 13);
            sync();
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 14);

            bindRefl(m_reflTemporalPipeline, reflPd);
            reflectionPassRan = params.reflectionTemporalFilterEnabled;
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 15);
            sync();
            if (m_timestampsSupported && m_queryPool)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 16);

            if (params.reflectionSpatialFilterEnabled && params.spatialStrength > 0.001f && m_reflSpatialPipeline) {
                bindRefl(m_reflSpatialPipeline, reflPd);
                if (m_timestampsSupported && m_queryPool)
                    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 17);
                sync();
                if (m_timestampsSupported && m_queryPool)
                    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 18);
            } else if (m_timestampsSupported && m_queryPool) {
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 17);
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, 18);
            }
        } else if (m_timestampsSupported && m_queryPool) {
            for (uint32_t query = 12; query <= 18; ++query)
                vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, m_queryPool, query);
        }

        memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        VkBufferMemoryBarrier2 diagnosticsReadBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        diagnosticsReadBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        diagnosticsReadBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        diagnosticsReadBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        diagnosticsReadBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        diagnosticsReadBarrier.srcQueueFamilyIndex = diagnosticsReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        diagnosticsReadBarrier.buffer = m_diagnosticsBuffer;
        diagnosticsReadBarrier.offset = 0;
        diagnosticsReadBarrier.size = VK_WHOLE_SIZE;
        VkDependencyInfo diagnosticsReadDependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        diagnosticsReadDependency.bufferMemoryBarrierCount = 1;
        diagnosticsReadDependency.pBufferMemoryBarriers = &diagnosticsReadBarrier;
        vkCmdPipelineBarrier2(cmd, &diagnosticsReadDependency);

        m_prevViewProj = params.viewProj;
        m_reflHistoryValid = reflectionPassRan;
        m_parity ^= 1u;
        m_hasTimestampResults = m_timestampsSupported;
    }
}
