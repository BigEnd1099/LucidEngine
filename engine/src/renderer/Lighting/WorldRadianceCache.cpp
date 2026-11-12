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
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        if (vmaCreateBuffer(m_context->getAllocator(), &bufferInfo, &allocInfo, &m_cellBuffer, &m_cellAlloc, nullptr) != VK_SUCCESS) {
            core::Logger::error("WorldRadianceCache: failed to allocate cell SSBO buffer!");
            return;
        }

        // Create descriptor set layout for Set 1 (World Radiance Cache)
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

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
            VkDescriptorBufferInfo bufferDescInfo{};
            bufferDescInfo.buffer = m_cellBuffer;
            bufferDescInfo.offset = 0;
            bufferDescInfo.range = bufferSize;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = m_descriptorSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &bufferDescInfo;

            vkUpdateDescriptorSets(m_context->getDevice(), 1, &write, 0, nullptr);
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
    }

    void WorldRadianceCache::clear(VkCommandBuffer cmd) {
        if (!m_cellBuffer) return;
        vkCmdFillBuffer(cmd, m_cellBuffer, 0, sizeof(WorldRadianceCell) * CACHE_CELL_COUNT, 0);
    }

    void WorldRadianceCache::dispatchUpdate(VkCommandBuffer cmd,
                                           VkDescriptorSet sceneSet,
                                           VkAccelerationStructureKHR tlas,
                                           uint32_t frameCounter,
                                           const glm::vec3& cameraPos,
                                           const glm::vec3& sunDirection,
                                           const glm::vec4& sunColor) {
        if (!m_updatePipeline || !m_pipelineLayout || !m_cellBuffer ||
            m_sceneSetLayout == VK_NULL_HANDLE || m_descriptorSet == VK_NULL_HANDLE ||
            tlas == VK_NULL_HANDLE || sceneSet == VK_NULL_HANDLE) return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_updatePipeline);

        // Update 4096 cells per frame in a sliding window
        constexpr uint32_t SLICE_SIZE = 4096;
        uint32_t sliceOffset = (frameCounter * SLICE_SIZE) % 32768u;

        struct PushConstants {
            glm::vec4 cameraPosAndCellSize;
            glm::vec4 sunDirAndIntensity;
            glm::vec4 sunColor;
            glm::uvec4 frameAndSlice;
        } pc{};

        pc.cameraPosAndCellSize = glm::vec4(cameraPos, DEFAULT_CELL_SIZE);
        pc.sunDirAndIntensity = glm::vec4(sunDirection, 2.5f);
        pc.sunColor = sunColor;
        pc.frameAndSlice = glm::uvec4(frameCounter, sliceOffset, SLICE_SIZE, 0u);

        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);

        const VkDescriptorSet sets[2] = { sceneSet, m_descriptorSet };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 2, sets, 0, nullptr);

        vkCmdDispatch(cmd, SLICE_SIZE / 64, 1, 1);

        // Buffer memory barrier to synchronize compute write with downstream compute reads
        VkBufferMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.buffer = m_cellBuffer;
        barrier.offset = 0;
        barrier.size = sizeof(WorldRadianceCell) * CACHE_CELL_COUNT;

        VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        depInfo.bufferMemoryBarrierCount = 1;
        depInfo.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    void WorldRadianceCache::cleanup() {
        if (!m_context) return;
        VkDevice device = m_context->getDevice();
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
        if (m_cellBuffer != VK_NULL_HANDLE && m_context->getAllocator()) {
            vmaDestroyBuffer(m_context->getAllocator(), m_cellBuffer, m_cellAlloc);
            m_cellBuffer = VK_NULL_HANDLE;
            m_cellAlloc = VK_NULL_HANDLE;
        }
        m_initialized = false;
    }

    uint32_t WorldRadianceCache::hashWorldCell(const glm::vec3& pos, const glm::vec3& normal, float cellSize) {
        glm::ivec3 cellCoord = glm::ivec3(glm::floor(pos / cellSize));
        uint32_t normIdx = 0u;
        if (std::abs(normal.x) > std::abs(normal.y) && std::abs(normal.x) > std::abs(normal.z)) {
            normIdx = normal.x > 0.0f ? 0u : 1u;
        } else if (std::abs(normal.y) > std::abs(normal.z)) {
            normIdx = normal.y > 0.0f ? 2u : 3u;
        } else {
            normIdx = normal.z > 0.0f ? 4u : 5u;
        }
        uint32_t h = static_cast<uint32_t>(cellCoord.x) * 73856093u ^
                     static_cast<uint32_t>(cellCoord.y) * 19349663u ^
                     static_cast<uint32_t>(cellCoord.z) * 83492791u ^
                     normIdx * 2654435761u;
        return (h ^ (h >> 16u)) % CACHE_CELL_COUNT;
    }

    uint32_t WorldRadianceCache::packNormalOctahedral(const glm::vec3& n) {
        glm::vec3 norm = n / (std::abs(n.x) + std::abs(n.y) + std::abs(n.z) + 1e-6f);
        glm::vec2 oct = (norm.z >= 0.0f) ? glm::vec2(norm.x, norm.y)
                                         : (1.0f - glm::abs(glm::vec2(norm.y, norm.x))) * glm::sign(glm::vec2(norm.x, norm.y));
        glm::uvec2 u = glm::uvec2(glm::clamp(oct * 0.5f + 0.5f, 0.0f, 1.0f) * 65535.0f);
        return (u.x & 0xFFFFu) | ((u.y & 0xFFFFu) << 16u);
    }

    glm::vec3 WorldRadianceCache::unpackNormalOctahedral(uint32_t packedN) {
        glm::vec2 oct = glm::vec2(static_cast<float>(packedN & 0xFFFFu), static_cast<float>((packedN >> 16u) & 0xFFFFu)) / 65535.0f;
        oct = oct * 2.0f - 1.0f;
        glm::vec3 n = glm::vec3(oct.x, oct.y, 1.0f - std::abs(oct.x) - std::abs(oct.y));
        if (n.z < 0.0f) {
            glm::vec2 signNotZero = glm::vec2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
            n.x = (1.0f - std::abs(n.y)) * signNotZero.x;
            n.y = (1.0f - std::abs(n.x)) * signNotZero.y;
        }
        return glm::normalize(n);
    }

} // namespace engine::renderer
