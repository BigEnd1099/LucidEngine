#include "renderer/Lighting/WorldRadianceCache.hpp"
#include "core/Logger.hpp"
#include "utils/FileIO.hpp"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace engine::renderer {

    WorldRadianceCache::~WorldRadianceCache() {
        cleanup();
    }

    void WorldRadianceCache::init(VulkanContext* context, VkDescriptorPool descriptorPool, VkDescriptorSetLayout sceneSetLayout) {
        if (m_initialized) return;
        m_context = context;
        m_descriptorPool = descriptorPool;
        m_sceneSetLayout = sceneSetLayout;

        createBuffers();
        createPipeline();
        m_initialized = true;
        core::Logger::info("WorldRadianceCache initialized (524,288 cells, 16 MB SSBO).");
    }

    void WorldRadianceCache::createBuffers() {
        VkDeviceSize bufferSize = sizeof(WorldRadianceCell) * CACHE_CELL_COUNT;

        VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        if (vmaCreateBuffer(m_context->getAllocator(), &bufferInfo, &allocInfo, &m_cellBuffer, &m_cellAlloc, nullptr) != VK_SUCCESS) {
            core::Logger::error("WorldRadianceCache: failed to allocate cell SSBO buffer!");
            return;
        }

        // Allocate ping-pong storage buffer for spatial filter output (16 MB)
        if (vmaCreateBuffer(m_context->getAllocator(), &bufferInfo, &allocInfo, &m_filteredCellBuffer, &m_filteredCellAlloc, nullptr) != VK_SUCCESS) {
            core::Logger::error("WorldRadianceCache: failed to allocate filtered cell SSBO buffer!");
            return;
        }

        // Create descriptor set layout for Set 1 (World Radiance Cache)
        // Binding 0: cellBuffer (read/write in update, readonly in filter)
        // Binding 1: filteredCellBuffer (writeonly in filter, readable downstream)
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;

        if (vkCreateDescriptorSetLayout(m_context->getDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
            core::Logger::error("WorldRadianceCache: failed to create descriptor set layout!");
            return;
        }

        // Allocate and bind descriptor set
        VkDescriptorSetAllocateInfo allocSetInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocSetInfo.descriptorPool = m_descriptorPool;
        allocSetInfo.descriptorSetCount = 1;
        allocSetInfo.pSetLayouts = &m_descriptorSetLayout;

        if (vkAllocateDescriptorSets(m_context->getDevice(), &allocSetInfo, &m_descriptorSet) == VK_SUCCESS) {
            VkDescriptorBufferInfo bufferDescInfo[2]{};
            bufferDescInfo[0].buffer = m_cellBuffer;
            bufferDescInfo[0].offset = 0;
            bufferDescInfo[0].range = bufferSize;

            bufferDescInfo[1].buffer = m_filteredCellBuffer;
            bufferDescInfo[1].offset = 0;
            bufferDescInfo[1].range = bufferSize;

            VkWriteDescriptorSet writes[2]{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = m_descriptorSet;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].pBufferInfo = &bufferDescInfo[0];

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = m_descriptorSet;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].pBufferInfo = &bufferDescInfo[1];

            vkUpdateDescriptorSets(m_context->getDevice(), 2, writes, 0, nullptr);
        }
    }

    void WorldRadianceCache::createPipeline() {
        if (!m_context || !m_descriptorSetLayout) return;

        std::vector<char> shaderCode;
        try {
            shaderCode = utils::readFile("gi_world_cache_update.comp.spv");
        } catch (...) {
            return;
        }

        if (shaderCode.empty()) return;

        VkShaderModuleCreateInfo smInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        smInfo.codeSize = shaderCode.size();
        smInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        VkShaderModule compModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_context->getDevice(), &smInfo, nullptr, &compModule) != VK_SUCCESS) {
            return;
        }

        // Push constant layout: 64 bytes
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcRange.offset = 0;
        pcRange.size = 64;

        // Use scene layout (set 0) and cache layout (set 1)
        VkDescriptorSetLayout setLayouts[2] = { m_sceneSetLayout, m_descriptorSetLayout };
        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plInfo.setLayoutCount = (m_sceneSetLayout != VK_NULL_HANDLE) ? 2 : 1;
        plInfo.pSetLayouts = (m_sceneSetLayout != VK_NULL_HANDLE) ? setLayouts : &m_descriptorSetLayout;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pcRange;

        if (vkCreatePipelineLayout(m_context->getDevice(), &plInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(m_context->getDevice(), compModule, nullptr);
            return;
        }

        VkComputePipelineCreateInfo cpInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpInfo.stage.module = compModule;
        cpInfo.stage.pName = "main";
        cpInfo.layout = m_pipelineLayout;

        vkCreateComputePipelines(m_context->getDevice(), VK_NULL_HANDLE, 1, &cpInfo, nullptr, &m_updatePipeline);
        vkDestroyShaderModule(m_context->getDevice(), compModule, nullptr);

        // Load and create spatial filter pipeline (gi_world_cache_filter.comp.spv)
        std::vector<char> filterShaderCode;
        try {
            filterShaderCode = utils::readFile("gi_world_cache_filter.comp.spv");
        } catch (...) {
            return;
        }

        if (!filterShaderCode.empty()) {
            VkShaderModuleCreateInfo filterSmInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            filterSmInfo.codeSize = filterShaderCode.size();
            filterSmInfo.pCode = reinterpret_cast<const uint32_t*>(filterShaderCode.data());

            VkShaderModule filterCompModule = VK_NULL_HANDLE;
            if (vkCreateShaderModule(m_context->getDevice(), &filterSmInfo, nullptr, &filterCompModule) == VK_SUCCESS) {
                VkComputePipelineCreateInfo filterCpInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
                filterCpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                filterCpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                filterCpInfo.stage.module = filterCompModule;
                filterCpInfo.stage.pName = "main";
                filterCpInfo.layout = m_pipelineLayout;

                vkCreateComputePipelines(m_context->getDevice(), VK_NULL_HANDLE, 1, &filterCpInfo, nullptr, &m_filterPipeline);
                vkDestroyShaderModule(m_context->getDevice(), filterCompModule, nullptr);
            }
        }
    }

    void WorldRadianceCache::clear(VkCommandBuffer cmd) {
        if (!m_cellBuffer) return;
        vkCmdFillBuffer(cmd, m_cellBuffer, 0, sizeof(WorldRadianceCell) * CACHE_CELL_COUNT, 0);
        if (m_filteredCellBuffer) {
            vkCmdFillBuffer(cmd, m_filteredCellBuffer, 0, sizeof(WorldRadianceCell) * CACHE_CELL_COUNT, 0);
        }
        m_needsClear = false;

        VkBufferMemoryBarrier2 barriers[2]{};
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barriers[0].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        barriers[0].buffer = m_cellBuffer;
        barriers[0].offset = 0;
        barriers[0].size = VK_WHOLE_SIZE;

        uint32_t barrierCount = 1;
        if (m_filteredCellBuffer) {
            barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barriers[1].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barriers[1].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            barriers[1].buffer = m_filteredCellBuffer;
            barriers[1].offset = 0;
            barriers[1].size = VK_WHOLE_SIZE;
            barrierCount = 2;
        }

        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.bufferMemoryBarrierCount = barrierCount;
        dep.pBufferMemoryBarriers = barriers;
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    bool WorldRadianceCache::dispatchUpdate(VkCommandBuffer cmd,
                                            VkDescriptorSet sceneSet,
                                            VkAccelerationStructureKHR tlas,
                                            uint32_t frameCounter,
                                            const glm::vec3& cameraPos,
                                            const glm::vec3& sunDirection,
                                            const glm::vec4& sunColor,
                                            VkQueryPool timingQueryPool,
                                            float baseCellSize,
                                            float bounceIntensity,
                                            uint32_t cellBatchCount,
                                            uint32_t featureFlags) {
        if (!m_updatePipeline || !m_pipelineLayout || !m_cellBuffer ||
            m_sceneSetLayout == VK_NULL_HANDLE || m_descriptorSet == VK_NULL_HANDLE ||
            tlas == VK_NULL_HANDLE || sceneSet == VK_NULL_HANDLE) return false;

        // Buffer allocations are not zero-initialized. Guarantee a valid empty table
        // even when the first usable TLAS appears after the renderer's initial frame.
        if (m_needsClear) clear(cmd);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_updatePipeline);

        // System #1: 1 warp (32 lanes) = 1 cell. Default 128 cells/frame.
        const uint32_t batchCount = std::clamp(cellBatchCount, 32u, 4096u);
        const uint32_t sliceOffset = (frameCounter * batchCount) % 32768u;

        struct PushConstants {
            glm::vec4 cameraPosAndCellSize;
            glm::vec4 sunDirAndIntensity;
            glm::vec4 sunColor;
            glm::uvec4 frameAndSlice;
        } pc{};

        pc.cameraPosAndCellSize = glm::vec4(cameraPos, std::clamp(baseCellSize, 0.10f, 2.0f));
        pc.sunDirAndIntensity = glm::vec4(sunDirection, 2.5f);
        pc.sunColor = glm::vec4(glm::vec3(sunColor), std::clamp(bounceIntensity, 0.0f, 4.0f));
        pc.frameAndSlice = glm::uvec4(frameCounter, sliceOffset, batchCount, featureFlags);

        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);

        const VkDescriptorSet sets[2] = { sceneSet, m_descriptorSet };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 2, sets, 0, nullptr);

        // Timestamp only the compute dispatch itself.
        if (timingQueryPool != VK_NULL_HANDLE)
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timingQueryPool, 11);

        // Dispatch update pass (writing to m_cellBuffer)
        vkCmdDispatch(cmd, batchCount, 1, 1);

        // Barrier 1: Synchronize update pass writes to m_cellBuffer with filter pass reads from m_cellBuffer
        VkBufferMemoryBarrier2 barrier1{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        barrier1.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier1.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        barrier1.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier1.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        barrier1.buffer = m_cellBuffer;
        barrier1.offset = 0;
        barrier1.size = sizeof(WorldRadianceCell) * CACHE_CELL_COUNT;

        VkDependencyInfo depInfo1{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        depInfo1.bufferMemoryBarrierCount = 1;
        depInfo1.pBufferMemoryBarriers = &barrier1;
        vkCmdPipelineBarrier2(cmd, &depInfo1);

        const bool runSpatialFilter = ((featureFlags & 2u) != 0u) && (m_filterPipeline != VK_NULL_HANDLE);
        // Dispatch filter pass if available (reading m_cellBuffer and writing to m_filteredCellBuffer)
        if (runSpatialFilter && m_filteredCellBuffer) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_filterPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 2, sets, 0, nullptr);
            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);

            // 524,288 cells / 64 threads per workgroup = 8192 workgroups
            vkCmdDispatch(cmd, CACHE_CELL_COUNT / 64, 1, 1);

            // Barrier 2: Synchronize filter pass writes to m_filteredCellBuffer with downstream reads
            VkBufferMemoryBarrier2 barrier2{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
            barrier2.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier2.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            barrier2.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier2.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            barrier2.buffer = m_filteredCellBuffer;
            barrier2.offset = 0;
            barrier2.size = sizeof(WorldRadianceCell) * CACHE_CELL_COUNT;

            VkDependencyInfo depInfo2{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            depInfo2.bufferMemoryBarrierCount = 1;
            depInfo2.pBufferMemoryBarriers = &barrier2;
            vkCmdPipelineBarrier2(cmd, &depInfo2);
        } else if (m_filteredCellBuffer) {
            // Fallback when spatial filter pass is disabled in UI or .spv was not loaded:
            // copy m_cellBuffer -> m_filteredCellBuffer so RtDenoiser binding 20 always has live data.
            VkBufferMemoryBarrier2 preCopyBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
            preCopyBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            preCopyBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            preCopyBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            preCopyBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            preCopyBarrier.buffer = m_cellBuffer;
            preCopyBarrier.offset = 0;
            preCopyBarrier.size = sizeof(WorldRadianceCell) * CACHE_CELL_COUNT;
            VkDependencyInfo depPreCopy{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            depPreCopy.bufferMemoryBarrierCount = 1;
            depPreCopy.pBufferMemoryBarriers = &preCopyBarrier;
            vkCmdPipelineBarrier2(cmd, &depPreCopy);

            VkBufferCopy copyRegion{};
            copyRegion.size = sizeof(WorldRadianceCell) * CACHE_CELL_COUNT;
            vkCmdCopyBuffer(cmd, m_cellBuffer, m_filteredCellBuffer, 1, &copyRegion);

            VkBufferMemoryBarrier2 postCopyBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
            postCopyBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            postCopyBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            postCopyBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            postCopyBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            postCopyBarrier.buffer = m_filteredCellBuffer;
            postCopyBarrier.offset = 0;
            postCopyBarrier.size = sizeof(WorldRadianceCell) * CACHE_CELL_COUNT;
            VkDependencyInfo depPostCopy{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            depPostCopy.bufferMemoryBarrierCount = 1;
            depPostCopy.pBufferMemoryBarriers = &postCopyBarrier;
            vkCmdPipelineBarrier2(cmd, &depPostCopy);
        } else {
            // Fallback barrier if filter pass is inactive
            VkBufferMemoryBarrier2 fallbackBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
            fallbackBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            fallbackBarrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            fallbackBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            fallbackBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            fallbackBarrier.buffer = m_cellBuffer;
            fallbackBarrier.offset = 0;
            fallbackBarrier.size = sizeof(WorldRadianceCell) * CACHE_CELL_COUNT;

            VkDependencyInfo depFallback{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            depFallback.bufferMemoryBarrierCount = 1;
            depFallback.pBufferMemoryBarriers = &fallbackBarrier;
            vkCmdPipelineBarrier2(cmd, &depFallback);
        }

        if (timingQueryPool != VK_NULL_HANDLE)
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timingQueryPool, 12);

        return true;
    }

    void WorldRadianceCache::cleanup() {
        if (!m_context) return;
        VkDevice device = m_context->getDevice();
        if (m_filterPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, m_filterPipeline, nullptr);
            m_filterPipeline = VK_NULL_HANDLE;
        }
        if (m_updatePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, m_updatePipeline, nullptr);
            m_updatePipeline = VK_NULL_HANDLE;
        }
        if (m_pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
        }
        if (m_descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
            m_descriptorSetLayout = VK_NULL_HANDLE;
        }
        if (m_filteredCellBuffer != VK_NULL_HANDLE && m_context->getAllocator()) {
            vmaDestroyBuffer(m_context->getAllocator(), m_filteredCellBuffer, m_filteredCellAlloc);
            m_filteredCellBuffer = VK_NULL_HANDLE;
            m_filteredCellAlloc = VK_NULL_HANDLE;
        }
        if (m_cellBuffer != VK_NULL_HANDLE && m_context->getAllocator()) {
            vmaDestroyBuffer(m_context->getAllocator(), m_cellBuffer, m_cellAlloc);
            m_cellBuffer = VK_NULL_HANDLE;
            m_cellAlloc = VK_NULL_HANDLE;
        }
        m_initialized = false;
        m_needsClear = true;
    }

    uint32_t WorldRadianceCache::hashWorldCell(const glm::vec3& pos, const glm::vec3& normal, float cellSize, uint32_t cascade) {
        return computeWorldCellKey(pos, normal, cellSize, cascade) % CACHE_CELL_COUNT;
    }

    uint32_t WorldRadianceCache::packNormalOctahedral(const glm::vec3& n) {
        glm::vec3 norm = n / (std::abs(n.x) + std::abs(n.y) + std::abs(n.z) + 1e-6f);
        glm::vec2 oct(norm.x, norm.y);
        if (norm.z < 0.0f) {
            const glm::vec2 oldXY = oct;
            const glm::vec2 signNotZero(
                oldXY.x >= 0.0f ? 1.0f : -1.0f,
                oldXY.y >= 0.0f ? 1.0f : -1.0f);
            oct = (1.0f - glm::abs(glm::vec2(oldXY.y, oldXY.x))) * signNotZero;
        }
        oct = glm::clamp(oct * 0.5f + 0.5f, 0.0f, 1.0f);
        const uint32_t ux = static_cast<uint32_t>(oct.x * 65535.0f + 0.5f);
        const uint32_t uy = static_cast<uint32_t>(oct.y * 65535.0f + 0.5f);
        return (uy << 16u) | ux;
    }

    glm::vec3 WorldRadianceCache::unpackNormalOctahedral(uint32_t packedN) {
        glm::vec2 oct = glm::vec2(static_cast<float>(packedN & 0xFFFFu), static_cast<float>((packedN >> 16u) & 0xFFFFu)) / 65535.0f;
        oct = oct * 2.0f - 1.0f;
        glm::vec3 n = glm::vec3(oct.x, oct.y, 1.0f - std::abs(oct.x) - std::abs(oct.y));
        if (n.z < 0.0f) {
            // Both folded components must use the original octahedral coordinates.
            // Updating n.x before evaluating n.y otherwise introduces a CPU/GPU mismatch.
            const float oldX = n.x;
            const float oldY = n.y;
            const float signX = oldX >= 0.0f ? 1.0f : -1.0f;
            const float signY = oldY >= 0.0f ? 1.0f : -1.0f;
            n.x = (1.0f - std::abs(oldY)) * signX;
            n.y = (1.0f - std::abs(oldX)) * signY;
        }
        return glm::normalize(n);
    }

} // namespace engine::renderer
