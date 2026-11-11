#include "renderer/VulkanRenderer.hpp"
#include "core/Logger.hpp"
#include <stdexcept>
#include <string>

namespace engine::renderer {
    namespace {
        void requireVkSuccess(VkResult result, const char* operation) {
            if (result == VK_SUCCESS) return;
            throw std::runtime_error(
                std::string(operation) + " failed with VkResult " + std::to_string(static_cast<int>(result)));
        }
    }

    void VulkanRenderer::createPointShadowResources() {
        VkImageCreateInfo cubeInfo{};
        cubeInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        cubeInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        cubeInfo.imageType = VK_IMAGE_TYPE_2D;
        cubeInfo.extent = { POINT_SHADOW_MAP_SIZE, POINT_SHADOW_MAP_SIZE, 1 };
        cubeInfo.mipLevels = 1;
        cubeInfo.arrayLayers = 6;
        cubeInfo.format = VK_FORMAT_D32_SFLOAT;
        cubeInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        cubeInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        cubeInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        cubeInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(m_context->getAllocator(), &cubeInfo, &allocInfo, &m_pointShadowImage, &m_pointShadowAlloc, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("failed to create point shadow cubemap image!");
        }

        VkImageViewCreateInfo cubeViewInfo{};
        cubeViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        cubeViewInfo.image = m_pointShadowImage;
        cubeViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        cubeViewInfo.format = VK_FORMAT_D32_SFLOAT;
        cubeViewInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 6 };
        requireVkSuccess(vkCreateImageView(m_context->getDevice(), &cubeViewInfo, nullptr, &m_pointShadowCubeView), "vkCreateImageView(point shadow cube)");

        for (uint32_t f = 0; f < 6; ++f) {
            VkImageViewCreateInfo faceViewInfo = cubeViewInfo;
            faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            faceViewInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, f, 1 };
            requireVkSuccess(vkCreateImageView(m_context->getDevice(), &faceViewInfo, nullptr, &m_pointShadowFaceViews[f]), "vkCreateImageView(point shadow face)");
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        requireVkSuccess(vkCreateSampler(m_context->getDevice(), &samplerInfo, nullptr, &m_pointShadowSampler), "vkCreateSampler(point shadow)");
    }

    void VulkanRenderer::cleanupPointShadowResources() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice dev = m_context->getDevice();
        if (m_pointShadowSampler) { vkDestroySampler(dev, m_pointShadowSampler, nullptr); m_pointShadowSampler = VK_NULL_HANDLE; }
        for (uint32_t f = 0; f < 6; ++f) {
            if (m_pointShadowFaceViews[f]) { vkDestroyImageView(dev, m_pointShadowFaceViews[f], nullptr); m_pointShadowFaceViews[f] = VK_NULL_HANDLE; }
        }
        if (m_pointShadowCubeView) { vkDestroyImageView(dev, m_pointShadowCubeView, nullptr); m_pointShadowCubeView = VK_NULL_HANDLE; }
        if (m_pointShadowImage) {
            vmaDestroyImage(m_context->getAllocator(), m_pointShadowImage, m_pointShadowAlloc);
            m_pointShadowImage = VK_NULL_HANDLE;
            m_pointShadowAlloc = VK_NULL_HANDLE;
        }
    }

    void VulkanRenderer::createShadowResources() {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { m_shadowWidth, m_shadowHeight, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = SHADOW_CASCADE_COUNT;
        imageInfo.format = VK_FORMAT_D32_SFLOAT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(m_context->getAllocator(), &imageInfo, &allocInfo, &m_shadowImage, &m_shadowAllocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shadow depth image!");
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_shadowImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        viewInfo.format = VK_FORMAT_D32_SFLOAT;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, SHADOW_CASCADE_COUNT };
        requireVkSuccess(vkCreateImageView(m_context->getDevice(), &viewInfo, nullptr, &m_shadowImageView), "vkCreateImageView(cascaded shadow array)");

        for (uint32_t c = 0; c < SHADOW_CASCADE_COUNT; ++c) {
            VkImageViewCreateInfo cv = viewInfo;
            cv.viewType = VK_IMAGE_VIEW_TYPE_2D;
            cv.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, c, 1 };
            requireVkSuccess(vkCreateImageView(m_context->getDevice(), &cv, nullptr, &m_shadowCascadeImageViews[c]), "vkCreateImageView(shadow cascade)");
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.compareEnable = VK_FALSE;
        requireVkSuccess(vkCreateSampler(m_context->getDevice(), &samplerInfo, nullptr, &m_shadowSampler), "vkCreateSampler(shadow map)");
    }

    void VulkanRenderer::cleanupShadowResources() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice dev = m_context->getDevice();

        if (m_shadowPipeline)       { vkDestroyPipeline(dev, m_shadowPipeline, nullptr); m_shadowPipeline = VK_NULL_HANDLE; }
        if (m_shadowPipelineLayout) { vkDestroyPipelineLayout(dev, m_shadowPipelineLayout, nullptr); m_shadowPipelineLayout = VK_NULL_HANDLE; }
        if (m_descriptorPool)       { vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; m_materialDescriptorSets.clear(); }
        if (m_descriptorSetLayout)  { vkDestroyDescriptorSetLayout(dev, m_descriptorSetLayout, nullptr); m_descriptorSetLayout = VK_NULL_HANDLE; }
        if (m_shadowSampler)        { vkDestroySampler(dev, m_shadowSampler, nullptr); m_shadowSampler = VK_NULL_HANDLE; }
        for (uint32_t c = 0; c < SHADOW_CASCADE_COUNT; ++c) {
            if (m_shadowCascadeImageViews[c]) {
                vkDestroyImageView(dev, m_shadowCascadeImageViews[c], nullptr);
                m_shadowCascadeImageViews[c] = VK_NULL_HANDLE;
            }
        }
        if (m_shadowImageView) { vkDestroyImageView(dev, m_shadowImageView, nullptr); m_shadowImageView = VK_NULL_HANDLE; }
        if (m_sceneUboBuffer) {
            vmaDestroyBuffer(m_context->getAllocator(), m_sceneUboBuffer, m_sceneUboAlloc);
            m_sceneUboBuffer = VK_NULL_HANDLE;
            m_sceneUboAlloc = VK_NULL_HANDLE;
            m_sceneUboMappedPtr = nullptr;
        }
        if (m_rtInstanceDataBuffer) {
            vmaDestroyBuffer(m_context->getAllocator(), m_rtInstanceDataBuffer, m_rtInstanceDataAllocation);
            m_rtInstanceDataBuffer = VK_NULL_HANDLE;
            m_rtInstanceDataAllocation = VK_NULL_HANDLE;
            m_rtInstanceDataMapped = nullptr;
        }
        if (m_motionBuffer) {
            vmaDestroyBuffer(m_context->getAllocator(), m_motionBuffer, m_motionBufferAlloc);
            m_motionBuffer = VK_NULL_HANDLE;
            m_motionBufferAlloc = VK_NULL_HANDLE;
            m_motionBufferMapped = nullptr;
        }
        if (m_shadowImage) {
            vmaDestroyImage(m_context->getAllocator(), m_shadowImage, m_shadowAllocation);
            m_shadowImage = VK_NULL_HANDLE;
            m_shadowAllocation = VK_NULL_HANDLE;
        }
    }

}
