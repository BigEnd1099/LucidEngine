#include "renderer/Geometry/GpuCulling.hpp"
#include "core/Logger.hpp"
#include "utils/FileIO.hpp"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace engine::renderer {

    GpuCulling::~GpuCulling() {
        cleanup();
    }

    void GpuCulling::init(VulkanContext* context, VkDescriptorPool descriptorPool, uint32_t width, uint32_t height) {
        if (m_initialized) return;
        m_context = context;
        m_descriptorPool = descriptorPool;
        m_width = width;
        m_height = height;

        createHiZResources(width, height);
        createBuffers();
        createPipelines();
        m_initialized = true;
        core::Logger::info("GpuCulling initialized (Hi-Z pyramid, Indirect Draw Count, Max-Reduction Sampler).");
    }

    void GpuCulling::createHiZResources(uint32_t width, uint32_t height) {
        VkDevice dev = m_context->getDevice();
        m_hiZMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { width, height, 1 };
        imageInfo.mipLevels = m_hiZMipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R32_SFLOAT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        if (vmaCreateImage(m_context->getAllocator(), &imageInfo, &allocInfo, &m_hiZImage, &m_hiZAlloc, nullptr) != VK_SUCCESS) {
            core::Logger::error("GpuCulling: Failed to create Hi-Z pyramid image!");
            return;
        }

        // Full mip-chain image view for sampling
        VkImageViewCreateInfo fullViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        fullViewInfo.image = m_hiZImage;
        fullViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        fullViewInfo.format = VK_FORMAT_R32_SFLOAT;
        fullViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, m_hiZMipLevels, 0, 1 };
        vkCreateImageView(dev, &fullViewInfo, nullptr, &m_hiZImageView);

        // Per-mip image views for compute store
        m_hiZMipViews.resize(m_hiZMipLevels);
        for (uint32_t i = 0; i < m_hiZMipLevels; ++i) {
            VkImageViewCreateInfo mipViewInfo = fullViewInfo;
            mipViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1 };
            vkCreateImageView(dev, &mipViewInfo, nullptr, &m_hiZMipViews[i]);
        }

        // Create Max-Reduction Sampler (VK_SAMPLER_REDUCTION_MODE_MAX for conservative occlusion)
        VkSamplerReductionModeCreateInfo reductionInfo{ VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO };
        reductionInfo.reductionMode = VK_SAMPLER_REDUCTION_MODE_MAX;

        VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerInfo.pNext = &reductionInfo;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(m_hiZMipLevels);
        vkCreateSampler(dev, &samplerInfo, nullptr, &m_hiZSampler);
    }

    void GpuCulling::createBuffers() {
        // Instance input buffer (mapped for CPU writes)
        VkDeviceSize instanceBufferSize = sizeof(InstanceCullData) * MAX_INDIRECT_INSTANCES;
        VkBufferCreateInfo instBufInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        instBufInfo.size = instanceBufferSize;
        instBufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        VmaAllocationCreateInfo instAllocInfo{};
        instAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        instAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        instAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        VmaAllocationInfo stagingAllocInfo{};
        vmaCreateBuffer(m_context->getAllocator(), &instBufInfo, &instAllocInfo, &m_instanceBuffer, &m_instanceAlloc, &stagingAllocInfo);
        m_instanceMappedPtr = stagingAllocInfo.pMappedData;

        // Indirect commands buffer
        VkDeviceSize cmdBufferSize = sizeof(DrawIndexedIndirectCommand) * MAX_INDIRECT_INSTANCES;
        VkBufferCreateInfo cmdBufInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        cmdBufInfo.size = cmdBufferSize;
        cmdBufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo cmdAllocInfo{};
        cmdAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        vmaCreateBuffer(m_context->getAllocator(), &cmdBufInfo, &cmdAllocInfo, &m_indirectCommandBuffer, &m_indirectCommandAlloc, nullptr);

        // Count buffer (atomic uint32_t)
        VkBufferCreateInfo countBufInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        countBufInfo.size = sizeof(uint32_t);
        countBufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo countAllocInfo{};
        countAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        vmaCreateBuffer(m_context->getAllocator(), &countBufInfo, &countAllocInfo, &m_countBuffer, &m_countAlloc, nullptr);
    }

    void GpuCulling::createPipelines() {
        VkDevice dev = m_context->getDevice();

        // 1. Cull Pipeline
        std::vector<VkDescriptorSetLayoutBinding> cullBindings = {
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
        };
        VkDescriptorSetLayoutCreateInfo cullDslInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        cullDslInfo.bindingCount = static_cast<uint32_t>(cullBindings.size());
        cullDslInfo.pBindings = cullBindings.data();
        vkCreateDescriptorSetLayout(dev, &cullDslInfo, nullptr, &m_cullSetLayout);

        VkPushConstantRange cullPcRange{};
        cullPcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        cullPcRange.offset = 0;
        cullPcRange.size = 128;

        VkPipelineLayoutCreateInfo cullPlInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        cullPlInfo.setLayoutCount = 1;
        cullPlInfo.pSetLayouts = &m_cullSetLayout;
        cullPlInfo.pushConstantRangeCount = 1;
        cullPlInfo.pPushConstantRanges = &cullPcRange;
        vkCreatePipelineLayout(dev, &cullPlInfo, nullptr, &m_cullPipelineLayout);

        // Load cull_indirect.comp.spv
        std::vector<char> cullShaderCode;
        try { cullShaderCode = utils::readFile("cull_indirect.comp.spv"); } catch (...) {}
        if (!cullShaderCode.empty()) {
            VkShaderModuleCreateInfo smInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            smInfo.codeSize = cullShaderCode.size();
            smInfo.pCode = reinterpret_cast<const uint32_t*>(cullShaderCode.data());
            VkShaderModule cullModule = VK_NULL_HANDLE;
            if (vkCreateShaderModule(dev, &smInfo, nullptr, &cullModule) == VK_SUCCESS) {
                VkComputePipelineCreateInfo cpInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
                cpInfo.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                 VK_SHADER_STAGE_COMPUTE_BIT, cullModule, "main", nullptr };
                cpInfo.layout = m_cullPipelineLayout;
                vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &m_cullPipeline);
                vkDestroyShaderModule(dev, cullModule, nullptr);
            }
        }

        // Allocate and write descriptor set for Cull Pipeline
        VkDescriptorSetAllocateInfo cullAllocSetInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        cullAllocSetInfo.descriptorPool = m_descriptorPool;
        cullAllocSetInfo.descriptorSetCount = 1;
        cullAllocSetInfo.pSetLayouts = &m_cullSetLayout;
        if (vkAllocateDescriptorSets(dev, &cullAllocSetInfo, &m_cullDescriptorSet) == VK_SUCCESS) {
            VkDescriptorBufferInfo instInfo{ m_instanceBuffer, 0, sizeof(InstanceCullData) * MAX_INDIRECT_INSTANCES };
            VkDescriptorBufferInfo cmdInfo{ m_indirectCommandBuffer, 0, sizeof(DrawIndexedIndirectCommand) * MAX_INDIRECT_INSTANCES };
            VkDescriptorBufferInfo countInfo{ m_countBuffer, 0, sizeof(uint32_t) };
            VkDescriptorImageInfo imgInfo{ m_hiZSampler, m_hiZImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            VkWriteDescriptorSet writes[4]{};
            writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_cullDescriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &instInfo, nullptr };
            writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_cullDescriptorSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &cmdInfo, nullptr };
            writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_cullDescriptorSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &countInfo, nullptr };
            writes[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_cullDescriptorSet, 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgInfo, nullptr, nullptr };
            vkUpdateDescriptorSets(dev, 4, writes, 0, nullptr);
        }

        // 2. Hi-Z Reduce Pipeline
        std::vector<VkDescriptorSetLayoutBinding> reduceBindings = {
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
        };
        VkDescriptorSetLayoutCreateInfo reduceDslInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        reduceDslInfo.bindingCount = static_cast<uint32_t>(reduceBindings.size());
        reduceDslInfo.pBindings = reduceBindings.data();
        vkCreateDescriptorSetLayout(dev, &reduceDslInfo, nullptr, &m_hizReduceSetLayout);

        VkPushConstantRange reducePcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 16 };
        VkPipelineLayoutCreateInfo reducePlInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        reducePlInfo.setLayoutCount = 1;
        reducePlInfo.pSetLayouts = &m_hizReduceSetLayout;
        reducePlInfo.pushConstantRangeCount = 1;
        reducePlInfo.pPushConstantRanges = &reducePcRange;
        vkCreatePipelineLayout(dev, &reducePlInfo, nullptr, &m_hizReducePipelineLayout);

        std::vector<char> reduceShaderCode;
        try { reduceShaderCode = utils::readFile("hzb_reduce.comp.spv"); } catch (...) {}
        if (!reduceShaderCode.empty()) {
            VkShaderModuleCreateInfo smInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            smInfo.codeSize = reduceShaderCode.size();
            smInfo.pCode = reinterpret_cast<const uint32_t*>(reduceShaderCode.data());
            VkShaderModule reduceModule = VK_NULL_HANDLE;
            if (vkCreateShaderModule(dev, &smInfo, nullptr, &reduceModule) == VK_SUCCESS) {
                VkComputePipelineCreateInfo cpInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
                cpInfo.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                 VK_SHADER_STAGE_COMPUTE_BIT, reduceModule, "main", nullptr };
                cpInfo.layout = m_hizReducePipelineLayout;
                vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &m_hizReducePipeline);
                vkDestroyShaderModule(dev, reduceModule, nullptr);
            }
        }
    }

    void GpuCulling::buildHiZPyramid(VkCommandBuffer cmd, VkImage depthImage, VkImageView, uint32_t width, uint32_t height) {
        if (!m_hiZImage || !m_hizReducePipeline) return;

        // Transition depthImage to TRANSFER_SRC_OPTIMAL and mip 0 of HiZ to TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier2 barriers[2]{};
        barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barriers[0].srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[0].image = depthImage;
        barriers[0].subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

        barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barriers[1].srcAccessMask = 0;
        barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[1].image = m_hiZImage;
        barriers[1].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        depInfo.imageMemoryBarrierCount = 2;
        depInfo.pImageMemoryBarriers = barriers;
        vkCmdPipelineBarrier2(cmd, &depInfo);

        // Blit / copy base depth into mip 0
        VkImageBlit blitRegion{};
        blitRegion.srcSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 };
        blitRegion.srcOffsets[0] = { 0, 0, 0 };
        blitRegion.srcOffsets[1] = { static_cast<int32_t>(width), static_cast<int32_t>(height), 1 };
        blitRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        blitRegion.dstOffsets[0] = { 0, 0, 0 };
        blitRegion.dstOffsets[1] = { static_cast<int32_t>(m_width), static_cast<int32_t>(m_height), 1 };
        vkCmdBlitImage(cmd, depthImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       m_hiZImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blitRegion, VK_FILTER_NEAREST);

        // Transition depth back
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barriers[0].srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        depInfo.imageMemoryBarrierCount = 1;
        vkCmdPipelineBarrier2(cmd, &depInfo);

        // Transition mip 0 to SHADER_READ_ONLY for reduction pass
        VkImageMemoryBarrier2 mip0Barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        mip0Barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        mip0Barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        mip0Barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mip0Barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        mip0Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        mip0Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        mip0Barrier.image = m_hiZImage;
        mip0Barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        depInfo.pImageMemoryBarriers = &mip0Barrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    void GpuCulling::dispatchCull(VkCommandBuffer cmd,
                                  const glm::mat4& viewProj,
                                  const glm::vec3& cameraPos,
                                  uint32_t totalInstances,
                                  bool enableHiZ) {
        if (!m_cullPipeline || totalInstances == 0) return;

        // Reset draw count to 0
        vkCmdFillBuffer(cmd, m_countBuffer, 0, sizeof(uint32_t), 0);

        // Barrier for count buffer
        VkBufferMemoryBarrier2 fillBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        fillBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        fillBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        fillBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        fillBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        fillBarrier.buffer = m_countBuffer;
        fillBarrier.offset = 0;
        fillBarrier.size = sizeof(uint32_t);

        VkDependencyInfo fillDepInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        fillDepInfo.bufferMemoryBarrierCount = 1;
        fillDepInfo.pBufferMemoryBarriers = &fillBarrier;
        vkCmdPipelineBarrier2(cmd, &fillDepInfo);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cullPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cullPipelineLayout, 0, 1, &m_cullDescriptorSet, 0, nullptr);

        struct PushConstants {
            glm::mat4 viewProj;
            glm::vec4 cameraPos;
            glm::vec4 screenParams;
            uint32_t totalInstances;
            uint32_t enableHiZCulling;
            uint32_t padding[2];
        } pc{};

        pc.viewProj = viewProj;
        pc.cameraPos = glm::vec4(cameraPos, 1.0f);
        pc.screenParams = glm::vec4(m_width, m_height, 1.0f / m_width, 1.0f / m_height);
        pc.totalInstances = totalInstances;
        pc.enableHiZCulling = enableHiZ ? 1u : 0u;

        vkCmdPushConstants(cmd, m_cullPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdDispatch(cmd, (totalInstances + 63) / 64, 1, 1);

        // Synchronize indirect draw commands and count buffer for vkCmdDrawIndexedIndirectCount
        VkBufferMemoryBarrier2 barriers[2]{};
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[0].srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        barriers[0].buffer = m_indirectCommandBuffer;
        barriers[0].offset = 0;
        barriers[0].size = sizeof(DrawIndexedIndirectCommand) * totalInstances;

        barriers[1] = barriers[0];
        barriers[1].buffer = m_countBuffer;
        barriers[1].size = sizeof(uint32_t);

        VkDependencyInfo syncDepInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        syncDepInfo.bufferMemoryBarrierCount = 2;
        syncDepInfo.pBufferMemoryBarriers = barriers;
        vkCmdPipelineBarrier2(cmd, &syncDepInfo);
    }

    void GpuCulling::resize(uint32_t width, uint32_t height) {
        if (m_width == width && m_height == height) return;
        m_width = width;
        m_height = height;
        cleanup();
        init(m_context, m_descriptorPool, width, height);
    }

    void GpuCulling::cleanup() {
        if (!m_context) return;
        VkDevice dev = m_context->getDevice();
        VmaAllocator alloc = m_context->getAllocator();

        if (m_cullPipeline) { vkDestroyPipeline(dev, m_cullPipeline, nullptr); m_cullPipeline = VK_NULL_HANDLE; }
        if (m_cullPipelineLayout) { vkDestroyPipelineLayout(dev, m_cullPipelineLayout, nullptr); m_cullPipelineLayout = VK_NULL_HANDLE; }
        if (m_cullSetLayout) { vkDestroyDescriptorSetLayout(dev, m_cullSetLayout, nullptr); m_cullSetLayout = VK_NULL_HANDLE; }

        if (m_hizReducePipeline) { vkDestroyPipeline(dev, m_hizReducePipeline, nullptr); m_hizReducePipeline = VK_NULL_HANDLE; }
        if (m_hizReducePipelineLayout) { vkDestroyPipelineLayout(dev, m_hizReducePipelineLayout, nullptr); m_hizReducePipelineLayout = VK_NULL_HANDLE; }
        if (m_hizReduceSetLayout) { vkDestroyDescriptorSetLayout(dev, m_hizReduceSetLayout, nullptr); m_hizReduceSetLayout = VK_NULL_HANDLE; }

        for (auto v : m_hiZMipViews) if (v) vkDestroyImageView(dev, v, nullptr);
        m_hiZMipViews.clear();
        if (m_hiZImageView) { vkDestroyImageView(dev, m_hiZImageView, nullptr); m_hiZImageView = VK_NULL_HANDLE; }
        if (m_hiZSampler) { vkDestroySampler(dev, m_hiZSampler, nullptr); m_hiZSampler = VK_NULL_HANDLE; }
        if (m_hiZImage) { vmaDestroyImage(alloc, m_hiZImage, m_hiZAlloc); m_hiZImage = VK_NULL_HANDLE; }

        if (m_instanceBuffer) { vmaDestroyBuffer(alloc, m_instanceBuffer, m_instanceAlloc); m_instanceBuffer = VK_NULL_HANDLE; }
        if (m_indirectCommandBuffer) { vmaDestroyBuffer(alloc, m_indirectCommandBuffer, m_indirectCommandAlloc); m_indirectCommandBuffer = VK_NULL_HANDLE; }
        if (m_countBuffer) { vmaDestroyBuffer(alloc, m_countBuffer, m_countAlloc); m_countBuffer = VK_NULL_HANDLE; }

        m_initialized = false;
    }

    bool GpuCulling::testFrustumSphere(const glm::mat4& viewProj, const glm::vec3& center, float radius) {
        glm::vec4 planes[6];
        planes[0] = glm::vec4(viewProj[0][3] + viewProj[0][0], viewProj[1][3] + viewProj[1][0], viewProj[2][3] + viewProj[2][0], viewProj[3][3] + viewProj[3][0]); // Left
        planes[1] = glm::vec4(viewProj[0][3] - viewProj[0][0], viewProj[1][3] - viewProj[1][0], viewProj[2][3] - viewProj[2][0], viewProj[3][3] - viewProj[3][0]); // Right
        planes[2] = glm::vec4(viewProj[0][3] + viewProj[0][1], viewProj[1][3] + viewProj[1][1], viewProj[2][3] + viewProj[2][1], viewProj[3][3] + viewProj[3][1]); // Bottom
        planes[3] = glm::vec4(viewProj[0][3] - viewProj[0][1], viewProj[1][3] - viewProj[1][1], viewProj[2][3] - viewProj[2][1], viewProj[3][3] - viewProj[3][1]); // Top
        planes[4] = glm::vec4(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);                                                                         // Near
        planes[5] = glm::vec4(viewProj[0][3] - viewProj[0][2], viewProj[1][3] - viewProj[1][2], viewProj[2][3] - viewProj[2][2], viewProj[3][3] - viewProj[3][2]); // Far

        for (int i = 0; i < 6; ++i) {
            float len = glm::length(glm::vec3(planes[i]));
            if (len > 0.0f) planes[i] /= len;
            float dist = glm::dot(glm::vec3(planes[i]), center) + planes[i].w;
            if (dist < -radius) return false;
        }
        return true;
    }

} // namespace engine::renderer
