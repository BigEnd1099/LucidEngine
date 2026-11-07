#include "renderer/VulkanRenderer.hpp"
#include "core/Logger.hpp"
#include "core/AssetManager.hpp"
#include <algorithm>
#include <array>

namespace engine::renderer {

    void VulkanRenderer::initRtDenoiser() {
        if (!m_context || !m_context->isRayTracingSupported()) return;
        m_rtDenoiser.init(m_context, m_descriptorSetLayout, m_width, m_height, m_depthImageView,
                          m_depthSampler ? m_depthSampler : m_linearSampler,
                          m_gBufferNormalRoughnessImageView,
                          m_gBufferAlbedoMetallicImageView,
                          m_gBufferMotionImageView);
        refreshRtIndirectDescriptors();
    }

    void VulkanRenderer::updateRtIndirectDescriptor(VkDescriptorSet descriptorSet) {
        if (!m_rtDenoiser.isReady() || descriptorSet == VK_NULL_HANDLE) return;
        VkDescriptorImageInfo infoIndirect{ m_rtDenoiser.outputSampler(), m_rtDenoiser.outputView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo infoRefl{ m_rtDenoiser.outputSampler(), m_rtDenoiser.reflectionOutputView(), VK_IMAGE_LAYOUT_GENERAL };

        VkWriteDescriptorSet writes[2]{};
        writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 16;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &infoIndirect;

        writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 17;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &infoRefl;

        vkUpdateDescriptorSets(m_context->getDevice(), 2, writes, 0, nullptr);
    }

    void VulkanRenderer::refreshRtIndirectDescriptors() {
        updateRtIndirectDescriptor(m_descriptorSet);
        for (const auto& materialSet : m_materialDescriptorSets) {
            updateRtIndirectDescriptor(materialSet.second);
        }
    }

    void VulkanRenderer::updateRayTracingDescriptorSet(VkDescriptorSet descriptorSet) {
        if (!m_context || !m_context->isRayTracingSupported() || descriptorSet == VK_NULL_HANDLE) return;
        const VkAccelerationStructureKHR tlas = m_accelerationStructure.getTLAS();
        if (tlas == VK_NULL_HANDLE) return;

        VkWriteDescriptorSetAccelerationStructureKHR asInfo{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR
        };
        asInfo.accelerationStructureCount = 1;
        asInfo.pAccelerationStructures = &tlas;

        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.pNext = &asInfo;
        write.dstSet = descriptorSet;
        write.dstBinding = 11;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

        VkDescriptorBufferInfo rtDataInfo{
            m_rtInstanceDataBuffer, 0,
            sizeof(RtInstanceGpuData) * RT_MAX_INSTANCE_COUNT
        };
        VkWriteDescriptorSet rtDataWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        rtDataWrite.dstSet = descriptorSet;
        rtDataWrite.dstBinding = 12;
        rtDataWrite.descriptorCount = 1;
        rtDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        rtDataWrite.pBufferInfo = &rtDataInfo;

        const VkWriteDescriptorSet rtWrites[] = { write, rtDataWrite };
        vkUpdateDescriptorSets(m_context->getDevice(), 2, rtWrites, 0, nullptr);
    }

    void VulkanRenderer::updateRayTracingTextureDescriptors(core::AssetManager* assetManager) {
        if (!m_context || !m_context->isRayTracingSupported() || !m_descriptorSet) return;
        const uint32_t textureCount = assetManager ? assetManager->getTextureCount() : 0;
        if (textureCount == m_rtTextureDescriptorCount) return;

        std::array<VkDescriptorImageInfo, RT_MATERIAL_TEXTURE_COUNT> albedoInfos{};
        std::array<VkDescriptorImageInfo, RT_MATERIAL_TEXTURE_COUNT> normalInfos{};
        std::array<VkDescriptorImageInfo, RT_MATERIAL_TEXTURE_COUNT> ormInfos{};
        for (uint32_t i = 0; i < RT_MATERIAL_TEXTURE_COUNT; ++i) {
            albedoInfos[i] = { m_defaultTextureSampler, m_defaultAlbedoView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            normalInfos[i] = { m_defaultTextureSampler, m_defaultNormalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            ormInfos[i] = { m_defaultTextureSampler, m_defaultOrmView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        }

        if (assetManager) {
            const uint32_t lastTextureId = std::min(textureCount, RT_MATERIAL_TEXTURE_COUNT - 1);
            for (uint32_t id = 1; id <= lastTextureId; ++id) {
                if (auto* texture = assetManager->getTexture(id)) {
                    if (texture->imageView != VK_NULL_HANDLE) {
                        const VkDescriptorImageInfo info{ m_defaultTextureSampler, texture->imageView,
                                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
                        // The same glTF image can serve as albedo, normal, or ORM.
                        albedoInfos[id] = info;
                        normalInfos[id] = info;
                        ormInfos[id] = info;
                    }
                }
            }
        }

        auto writeTextureArrays = [&](VkDescriptorSet set) {
            if (set == VK_NULL_HANDLE) return;
            VkWriteDescriptorSet writes[3]{};
            const VkDescriptorImageInfo* arrays[3] = { albedoInfos.data(), normalInfos.data(), ormInfos.data() };
            for (uint32_t i = 0; i < 3; ++i) {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = set;
                writes[i].dstBinding = 13 + i;
                writes[i].descriptorCount = RT_MATERIAL_TEXTURE_COUNT;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].pImageInfo = arrays[i];
            }
            vkUpdateDescriptorSets(m_context->getDevice(), 3, writes, 0, nullptr);
        };

        writeTextureArrays(m_descriptorSet);
        for (const auto& entry : m_materialDescriptorSets) writeTextureArrays(entry.second);
        if (textureCount >= RT_MATERIAL_TEXTURE_COUNT) {
            core::Logger::warn("RT material texture table reached 256 entries; texture IDs above 255 use fallback maps.");
        }
        m_rtTextureDescriptorCount = textureCount;
    }

}
