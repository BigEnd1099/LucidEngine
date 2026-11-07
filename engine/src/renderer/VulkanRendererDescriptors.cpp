#include "renderer/VulkanRenderer.hpp"
#include "core/Logger.hpp"
#include "core/AssetManager.hpp"
#include <stdexcept>
#include <vector>
#include <array>

namespace engine::renderer {

    void VulkanRenderer::createDescriptorSets() {
        VkDevice dev = m_context->getDevice();

        VkBufferCreateInfo uboBufferInfo{};
        uboBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        uboBufferInfo.size = sizeof(SceneUboData);
        uboBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        VmaAllocationCreateInfo uboAllocInfo{};
        uboAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        uboAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo uboResultInfo{};
        if (vmaCreateBuffer(m_context->getAllocator(), &uboBufferInfo, &uboAllocInfo, &m_sceneUboBuffer, &m_sceneUboAlloc, &uboResultInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to create scene ubo buffer!");
        }
        m_sceneUboMappedPtr = uboResultInfo.pMappedData;

        if (m_context->isRayTracingSupported()) {
            VkBufferCreateInfo rtDataInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            rtDataInfo.size = sizeof(RtInstanceGpuData) * RT_MAX_INSTANCE_COUNT;
            rtDataInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            VmaAllocationCreateInfo rtDataAllocInfo{};
            rtDataAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            rtDataAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VmaAllocationInfo rtDataResult{};
            if (vmaCreateBuffer(m_context->getAllocator(), &rtDataInfo, &rtDataAllocInfo,
                                &m_rtInstanceDataBuffer, &m_rtInstanceDataAllocation, &rtDataResult) != VK_SUCCESS) {
                throw std::runtime_error("failed to create RT instance material buffer!");
            }
            m_rtInstanceDataMapped = rtDataResult.pMappedData;

            VkBufferCreateInfo motionDataInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            motionDataInfo.size = sizeof(glm::mat4) * (1 + RT_MAX_INSTANCE_COUNT);
            motionDataInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            VmaAllocationCreateInfo motionDataAllocInfo{};
            motionDataAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            motionDataAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                        VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VmaAllocationInfo motionDataResult{};
            if (vmaCreateBuffer(m_context->getAllocator(), &motionDataInfo, &motionDataAllocInfo,
                                &m_motionBuffer, &m_motionBufferAlloc, &motionDataResult) != VK_SUCCESS) {
                throw std::runtime_error("failed to create motion vectors buffer!");
            }
            m_motionBufferMapped = motionDataResult.pMappedData;
        }

        // Bindings 0..10 are the raster PBR resources. Binding 11 is added only
        // on devices that expose VK_KHR_ray_query / acceleration structures.
        std::vector<VkDescriptorSetLayoutBinding> bindings(11);
        for (uint32_t i = 0; i < 11; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorCount = 1;
            bindings[i].descriptorType = (i == 1) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].stageFlags = (i == 1) ? (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
                                             : VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        if (m_context->isRayTracingSupported()) {
            VkDescriptorSetLayoutBinding rtBinding{};
            rtBinding.binding = 11;
            rtBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            rtBinding.descriptorCount = 1;
            rtBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
            bindings.push_back(rtBinding);

            VkDescriptorSetLayoutBinding rtInstanceBinding{};
            rtInstanceBinding.binding = 12;
            rtInstanceBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            rtInstanceBinding.descriptorCount = 1;
            rtInstanceBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
            bindings.push_back(rtInstanceBinding);

            for (uint32_t binding = 13; binding <= 15; ++binding) {
                VkDescriptorSetLayoutBinding textureArrayBinding{};
                textureArrayBinding.binding = binding;
                textureArrayBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                textureArrayBinding.descriptorCount = RT_MATERIAL_TEXTURE_COUNT;
                textureArrayBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
                bindings.push_back(textureArrayBinding);
            }

            // Binding 16: denoised RT indirect lighting (GI radiance + AO) from the RT compute passes.
            VkDescriptorSetLayoutBinding rtIndirectBinding{};
            rtIndirectBinding.binding = 16;
            rtIndirectBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            rtIndirectBinding.descriptorCount = 1;
            rtIndirectBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings.push_back(rtIndirectBinding);

            // Binding 17: denoised RT specular reflections from RT compute pass.
            VkDescriptorSetLayoutBinding rtReflectionsBinding{};
            rtReflectionsBinding.binding = 17;
            rtReflectionsBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            rtReflectionsBinding.descriptorCount = 1;
            rtReflectionsBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings.push_back(rtReflectionsBinding);

            // Binding 18: per-instance motion vectors buffer (prevViewProj + prevModels)
            VkDescriptorSetLayoutBinding motionBinding{};
            motionBinding.binding = 18;
            motionBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            motionBinding.descriptorCount = 1;
            motionBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            bindings.push_back(motionBinding);
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_descriptorSetLayout);

        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 52000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64 }
        };
        if (m_context->isRayTracingSupported()) {
            poolSizes.push_back({ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 64 });
            poolSizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64 });
        }

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 64;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_descriptorPool);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;
        vkAllocateDescriptorSets(dev, &allocInfo, &m_descriptorSet);

        VkDescriptorBufferInfo uboDesc{ m_sceneUboBuffer, 0, sizeof(SceneUboData) };
        std::array<VkDescriptorImageInfo, 10> imgInfos = {{
            { m_shadowSampler,         m_shadowImageView,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, // 0
            { m_pointShadowSampler,    m_pointShadowCubeView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, // 2
            { m_environmentSampler,    m_envCubeView,          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, // 3
            { m_environmentSampler,    m_irradianceCubeView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, // 4
            { m_environmentSampler,    m_prefilterCubeView,    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, // 5
            { m_environmentSampler,    m_brdfLutView,          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, // 6
            { m_linearSampler,         m_aoBlurImageView,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, // 7
            { m_defaultTextureSampler, m_defaultAlbedoView,    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, // 8
            { m_defaultTextureSampler, m_defaultNormalView,    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, // 9
            { m_defaultTextureSampler, m_defaultOrmView,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }  // 10
        }};

        std::array<VkWriteDescriptorSet, 11> writes{};
        uint32_t imgIdx = 0;
        for (uint32_t b = 0; b < 11; ++b) {
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet = m_descriptorSet;
            writes[b].dstBinding = b;
            writes[b].descriptorCount = 1;
            if (b == 1) {
                writes[b].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[b].pBufferInfo = &uboDesc;
            } else {
                writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[b].pImageInfo = &imgInfos[imgIdx++];
            }
        }
        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        if (m_motionBuffer) {
            VkDescriptorBufferInfo motionDescInfo{ m_motionBuffer, 0, sizeof(glm::mat4) * (1 + RT_MAX_INSTANCE_COUNT) };
            VkWriteDescriptorSet motionWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            motionWrite.dstSet = m_descriptorSet;
            motionWrite.dstBinding = 18;
            motionWrite.descriptorCount = 1;
            motionWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            motionWrite.pBufferInfo = &motionDescInfo;
            vkUpdateDescriptorSets(dev, 1, &motionWrite, 0, nullptr);
        }
        updateRayTracingDescriptorSet(m_descriptorSet);
    }

    VkDescriptorSet VulkanRenderer::getOrCreateMaterialDescriptorSet(uint32_t materialId, core::AssetManager* assetManager) {
        if (materialId == 0 || !assetManager || !m_context || !m_context->getDevice() || !m_descriptorPool) {
            return m_descriptorSet;
        }

        auto it = m_materialDescriptorSets.find(materialId);
        if (it != m_materialDescriptorSets.end()) {
            return it->second;
        }

        core::MaterialAsset* mat = assetManager->getMaterial(materialId);
        if (!mat) {
            return m_descriptorSet;
        }

        VkImageView albedoView = m_defaultAlbedoView;
        VkImageView normalView = m_defaultNormalView;
        VkImageView ormView    = m_defaultOrmView;

        if (mat->albedoTextureId != 0) {
            if (auto* tex = assetManager->getTexture(mat->albedoTextureId)) {
                if (tex->imageView != VK_NULL_HANDLE) albedoView = tex->imageView;
            }
        }
        if (mat->normalTextureId != 0) {
            if (auto* tex = assetManager->getTexture(mat->normalTextureId)) {
                if (tex->imageView != VK_NULL_HANDLE) normalView = tex->imageView;
            }
        }
        if (mat->ormTextureId != 0) {
            if (auto* tex = assetManager->getTexture(mat->ormTextureId)) {
                if (tex->imageView != VK_NULL_HANDLE) ormView = tex->imageView;
            }
        }

        if (albedoView == m_defaultAlbedoView && normalView == m_defaultNormalView && ormView == m_defaultOrmView) {
            m_materialDescriptorSets[materialId] = m_descriptorSet;
            return m_descriptorSet;
        }

        VkDevice dev = m_context->getDevice();
        VkDescriptorSet newSet = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;

        if (vkAllocateDescriptorSets(dev, &allocInfo, &newSet) != VK_SUCCESS) {
            core::Logger::warn("Failed to allocate material descriptor set for material " + std::to_string(materialId));
            return m_descriptorSet;
        }

        VkDescriptorBufferInfo uboDesc{ m_sceneUboBuffer, 0, sizeof(SceneUboData) };
        std::array<VkDescriptorImageInfo, 10> imgInfos = {{
            { m_shadowSampler,         m_shadowImageView,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { m_pointShadowSampler,    m_pointShadowCubeView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { m_environmentSampler,    m_envCubeView,          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { m_environmentSampler,    m_irradianceCubeView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { m_environmentSampler,    m_prefilterCubeView,    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { m_environmentSampler,    m_brdfLutView,          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { m_linearSampler,         m_aoBlurImageView,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { m_defaultTextureSampler, albedoView,             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { m_defaultTextureSampler, normalView,             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { m_defaultTextureSampler, ormView,                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
        }};

        std::array<VkWriteDescriptorSet, 11> writes{};
        uint32_t imgIdx = 0;
        for (uint32_t b = 0; b < 11; ++b) {
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet = newSet;
            writes[b].dstBinding = b;
            writes[b].descriptorCount = 1;
            if (b == 1) {
                writes[b].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[b].pBufferInfo = &uboDesc;
            } else {
                writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[b].pImageInfo = &imgInfos[imgIdx++];
            }
        }
        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        updateRayTracingDescriptorSet(newSet);
        updateRtIndirectDescriptor(newSet);
        if (m_motionBuffer) {
            VkDescriptorBufferInfo motionDescInfo{ m_motionBuffer, 0, sizeof(glm::mat4) * (1 + RT_MAX_INSTANCE_COUNT) };
            VkWriteDescriptorSet motionWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            motionWrite.dstSet = newSet;
            motionWrite.dstBinding = 18;
            motionWrite.descriptorCount = 1;
            motionWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            motionWrite.pBufferInfo = &motionDescInfo;
            vkUpdateDescriptorSets(dev, 1, &motionWrite, 0, nullptr);
        }

        m_materialDescriptorSets[materialId] = newSet;
        m_rtTextureDescriptorCount = UINT32_MAX;
        updateRayTracingTextureDescriptors(assetManager);
        return newSet;
    }

}
