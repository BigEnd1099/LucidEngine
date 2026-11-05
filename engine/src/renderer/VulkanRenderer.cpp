#include "renderer/VulkanRenderer.hpp"
#include "core/Logger.hpp"
#include "renderer/Vertex.hpp"
#include "core/AssetManager.hpp"
#include "core/Engine.hpp"
#include "utils/FileIO.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <stdexcept>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <iostream>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "renderer/stb_image_write.h"

#ifdef USE_GLFW
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#endif

namespace engine::renderer {

    uint32_t g_pendingRenderWidth = 0;
    uint32_t g_pendingRenderHeight = 0;
    int g_pendingHdriIndex = -1;

    namespace {
        struct PbrPushConstants {
            glm::mat4 model;
            glm::vec4 normalCol0;      // xyz = col0 macierzy normalnych, w = metallic
            glm::vec4 normalCol1;      // xyz = col1 macierzy normalnych, w = roughness
            glm::vec4 normalCol2;      // xyz = col2 macierzy normalnych, w = emissiveIntensity
            glm::vec4 baseColorFactor; // rgb = baseColor, a = aoOverride
        };
        static_assert(sizeof(PbrPushConstants) == 128, "PbrPushConstants must fit within 128-byte Vulkan guarantee");

        struct DrawItem {
            core::MeshAsset* mesh{nullptr};
            PbrPushConstants pcs{};
        };
    }

    VulkanRenderer::VulkanRenderer(VulkanContext* context, uint32_t width, uint32_t height)
        : m_context(context), m_width(width), m_height(height), m_baseWidth(width), m_baseHeight(height) {
        createOffscreenTarget();
        createCommandBuffer();
        createShadowResources();
        createPointShadowResources();
        createDefaultTextures();
        createEnvironmentResources();
        createDescriptorSets();
        createSkyboxPipeline();
        createShadowPipeline();
        createComputePipelines();
        createGraphicsPipeline();
        createParticlePipeline();
        createPostProcessResources();
        initCaptureResources();

#ifdef USE_GLFW
        if (!m_context->isHeadless()) {
            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            vkCreateSemaphore(m_context->getDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphore);
            vkCreateSemaphore(m_context->getDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphore);
            vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr, &m_inFlightFence);

            initImGui();
        }
#endif
    }

    VulkanRenderer::~VulkanRenderer() {
        if (m_context && m_context->getDevice()) {
            VkDevice dev = m_context->getDevice();
            vkDeviceWaitIdle(dev);

            cleanupCaptureResources();
            cleanupParticlePipeline();
            cleanupSkyboxPipeline();
            cleanupOffscreenTarget();
            cleanupPostProcessResources();
            cleanupEnvironmentResources();
            cleanupDefaultTextures();
            cleanupPointShadowResources();
            cleanupShadowResources();

#ifdef USE_GLFW
            if (!m_context->isHeadless()) {
                ImGui_ImplVulkan_Shutdown();
                ImGui_ImplGlfw_Shutdown();
                ImGui::DestroyContext();

                vkDestroySemaphore(dev, m_imageAvailableSemaphore, nullptr);
                vkDestroySemaphore(dev, m_renderFinishedSemaphore, nullptr);
                vkDestroyFence(dev, m_inFlightFence, nullptr);
            }
#endif

            if (m_depthPrepassPipeline) {
                vkDestroyPipeline(dev, m_depthPrepassPipeline, nullptr);
                m_depthPrepassPipeline = VK_NULL_HANDLE;
            }
            if (m_graphicsPipeline) {
                vkDestroyPipeline(dev, m_graphicsPipeline, nullptr);
                m_graphicsPipeline = VK_NULL_HANDLE;
            }
            if (m_pipelineLayout) {
                vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr);
                m_pipelineLayout = VK_NULL_HANDLE;
            }
            if (m_commandBuffer) {
                vkFreeCommandBuffers(dev, m_context->getCommandPool(), 1, &m_commandBuffer);
                m_commandBuffer = VK_NULL_HANDLE;
            }
        }
    }

    void VulkanRenderer::setRenderResolution(uint32_t width, uint32_t height) {
        width  = std::clamp(width,  64u, 7680u);
        height = std::clamp(height, 64u, 4320u);
        if (m_width == width && m_height == height) return;

        if (m_context && m_context->getDevice()) {
            vkDeviceWaitIdle(m_context->getDevice());
        }

        cleanupCaptureResources();
        cleanupOffscreenTarget();

        m_width = width;
        m_height = height;

        createOffscreenTarget();
        updatePostProcessDescriptorSets();
        initCaptureResources();

        core::Logger::info("Render resolution updated to " + std::to_string(m_width) + "x" + std::to_string(m_height));
    }

    void VulkanRenderer::setResolutionScale(float scale) {
        m_resolutionScale = std::clamp(scale, 0.25f, 2.5f);
        uint32_t targetW = (static_cast<uint32_t>(static_cast<float>(m_baseWidth)  * m_resolutionScale) + 1u) & ~1u;
        uint32_t targetH = (static_cast<uint32_t>(static_cast<float>(m_baseHeight) * m_resolutionScale) + 1u) & ~1u;
        setRenderResolution(targetW, targetH);
    }

    void VulkanRenderer::createCommandBuffer() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_context->getCommandPool();
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_context->getDevice(), &allocInfo, &m_commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers!");
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
        vkCreateImageView(m_context->getDevice(), &cubeViewInfo, nullptr, &m_pointShadowCubeView);

        for (uint32_t f = 0; f < 6; ++f) {
            VkImageViewCreateInfo faceViewInfo = cubeViewInfo;
            faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            faceViewInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, f, 1 };
            vkCreateImageView(m_context->getDevice(), &faceViewInfo, nullptr, &m_pointShadowFaceViews[f]);
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
        vkCreateSampler(m_context->getDevice(), &samplerInfo, nullptr, &m_pointShadowSampler);
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
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
        vkCreateImageView(m_context->getDevice(), &viewInfo, nullptr, &m_shadowImageView);

        for (uint32_t c = 0; c < SHADOW_CASCADE_COUNT; ++c) {
            VkImageViewCreateInfo cv = viewInfo;
            cv.viewType = VK_IMAGE_VIEW_TYPE_2D;
            cv.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, c, 1 };
            vkCreateImageView(m_context->getDevice(), &cv, nullptr, &m_shadowCascadeImageViews[c]);
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
        vkCreateSampler(m_context->getDevice(), &samplerInfo, nullptr, &m_shadowSampler);
    }

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

        // 11 bindowań (0: CSM Array, 1: SceneUbo, 2: PointShadowCube, 3: SkyCube, 4: IrradianceCube,
        // 5: PrefilterCube, 6: BrdfLut2D, 7: GTAO, 8: Albedo, 9: Normal, 10: ORM)
        std::array<VkDescriptorSetLayoutBinding, 11> bindings{};
        for (uint32_t i = 0; i < 11; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorCount = 1;
            bindings[i].descriptorType = (i == 1) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].stageFlags = (i == 1) ? (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT) : VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_descriptorSetLayout);

        std::array<VkDescriptorPoolSize, 2> poolSizes = {{
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64 }
        }};

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

        m_materialDescriptorSets[materialId] = newSet;
        return newSet;
    }

    void VulkanRenderer::createShadowPipeline() {
        VkDevice dev = m_context->getDevice();
        VkShaderModule vertModule = createShaderModule(utils::readFile("shadow_depth.vert.spv"));
        VkShaderModule fragModule = createShaderModule(utils::readFile("shadow_depth.frag.spv"));

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr };

        auto bindingDescriptions = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
        vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 1.25f;
        rasterizer.depthBiasSlopeFactor = 1.75f;

        VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) };
        VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;
        vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &m_shadowPipelineLayout);

        const VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        renderingInfo.depthAttachmentFormat = depthFormat;

        VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_shadowPipelineLayout;

        if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_shadowPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shadow pipeline!");
        }

        vkDestroyShaderModule(dev, fragModule, nullptr);
        vkDestroyShaderModule(dev, vertModule, nullptr);
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
        if (m_shadowImage) {
            vmaDestroyImage(m_context->getAllocator(), m_shadowImage, m_shadowAllocation);
            m_shadowImage = VK_NULL_HANDLE;
            m_shadowAllocation = VK_NULL_HANDLE;
        }
    }

    void VulkanRenderer::createGraphicsPipeline() {
        VkDevice dev = m_context->getDevice();
        VkShaderModule vertShaderModule = createShaderModule(utils::readFile("pbr.vert.spv"));
        VkShaderModule fragShaderModule = createShaderModule(utils::readFile("pbr.frag.spv"));

        VkPipelineShaderStageCreateInfo shaderStages[2]{};
        shaderStages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule, "main", nullptr };
        shaderStages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, "main", nullptr };

        auto bindingDescriptions = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
        vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE; // Zapis głębi odbywa się w Depth Prepass (zero overdraw w PBR!)
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PbrPushConstants) };
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        if (vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create PBR pipeline layout!");
        }

        const VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        const VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;
        renderingInfo.depthAttachmentFormat = depthFormat;

        VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_pipelineLayout;

        if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create PBR graphics pipeline!");
        }

        // Depth Prepass Pipeline (tylko pbr.vert, 0 załączników koloru, pełny zapis Z dla GTAO oraz Early-Z)
        VkPipelineDepthStencilStateCreateInfo prepassDepth = depthStencil;
        prepassDepth.depthWriteEnable = VK_TRUE;
        prepassDepth.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendStateCreateInfo prepassBlend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        VkPipelineRenderingCreateInfo prepassRendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        prepassRendering.depthAttachmentFormat = depthFormat;

        VkGraphicsPipelineCreateInfo prepassInfo = pipelineInfo;
        prepassInfo.pNext = &prepassRendering;
        prepassInfo.stageCount = 1;
        prepassInfo.pColorBlendState = &prepassBlend;
        prepassInfo.pDepthStencilState = &prepassDepth;

        if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &prepassInfo, nullptr, &m_depthPrepassPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create Depth Prepass pipeline!");
        }

        vkDestroyShaderModule(dev, fragShaderModule, nullptr);
        vkDestroyShaderModule(dev, vertShaderModule, nullptr);
    }

    void VulkanRenderer::createParticlePipeline() {
        VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = sizeof(ParticleVertex) * MAX_PARTICLES;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo vmaAllocInfo{};
        if (vmaCreateBuffer(m_context->getAllocator(), &bufferInfo, &allocInfo, &m_particleVertexBuffer, &m_particleVertexAlloc, &vmaAllocInfo) != VK_SUCCESS) {
            return;
        }
        m_particleMappedPtr = vmaAllocInfo.pMappedData;

        VkShaderModule vertMod = VK_NULL_HANDLE;
        VkShaderModule fragMod = VK_NULL_HANDLE;
        try {
            vertMod = createShaderModule(utils::readFile("particle.vert.spv"));
            fragMod = createShaderModule(utils::readFile("particle.frag.spv"));
        } catch (const std::exception& e) {
            std::cerr << "[Renderer] Warning: Could not load particle shaders: " << e.what() << std::endl;
            return;
        }

        VkPipelineShaderStageCreateInfo shaderStages[2]{};
        shaderStages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertMod, "main", nullptr };
        shaderStages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragMod, "main", nullptr };

        VkVertexInputBindingDescription bindingDesc{ 0, sizeof(ParticleVertex), VK_VERTEX_INPUT_RATE_VERTEX };
        std::array<VkVertexInputAttributeDescription, 3> attrDescs = {{
            { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(ParticleVertex, pos)   },
            { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ParticleVertex, color) },
            { 2, 0, VK_FORMAT_R32_SFLOAT,          offsetof(ParticleVertex, size)  }
        }};

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
        vertexInputInfo.pVertexAttributeDescriptions = attrDescs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

        VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlend;

        VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkPushConstantRange pcr{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) };
        VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pcr;
        vkCreatePipelineLayout(m_context->getDevice(), &layoutInfo, nullptr, &m_particlePipelineLayout);

        const VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        const VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;
        renderingInfo.depthAttachmentFormat = depthFormat;

        VkGraphicsPipelineCreateInfo pipeInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeInfo.pNext = &renderingInfo;
        pipeInfo.stageCount = 2;
        pipeInfo.pStages = shaderStages;
        pipeInfo.pVertexInputState = &vertexInputInfo;
        pipeInfo.pInputAssemblyState = &inputAssembly;
        pipeInfo.pViewportState = &viewportState;
        pipeInfo.pRasterizationState = &rasterizer;
        pipeInfo.pMultisampleState = &multisampling;
        pipeInfo.pColorBlendState = &colorBlending;
        pipeInfo.pDepthStencilState = &depthStencil;
        pipeInfo.pDynamicState = &dynamicState;
        pipeInfo.layout = m_particlePipelineLayout;

        vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_particlePipeline);
        vkDestroyShaderModule(m_context->getDevice(), fragMod, nullptr);
        vkDestroyShaderModule(m_context->getDevice(), vertMod, nullptr);
    }

    void VulkanRenderer::cleanupParticlePipeline() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice dev = m_context->getDevice();
        if (m_particlePipeline)       { vkDestroyPipeline(dev, m_particlePipeline, nullptr); m_particlePipeline = VK_NULL_HANDLE; }
        if (m_particlePipelineLayout) { vkDestroyPipelineLayout(dev, m_particlePipelineLayout, nullptr); m_particlePipelineLayout = VK_NULL_HANDLE; }
        if (m_particleVertexBuffer) {
            vmaDestroyBuffer(m_context->getAllocator(), m_particleVertexBuffer, m_particleVertexAlloc);
            m_particleVertexBuffer = VK_NULL_HANDLE;
            m_particleVertexAlloc = VK_NULL_HANDLE;
            m_particleMappedPtr = nullptr;
        }
    }

    void VulkanRenderer::render(ecs::Registry& registry, core::AssetManager* assetManager, glm::mat4 viewMatrix, core::Engine* engine) {
        if (g_pendingRenderWidth > 0 && g_pendingRenderHeight > 0) {
            setRenderResolution(g_pendingRenderWidth, g_pendingRenderHeight);
            g_pendingRenderWidth = 0;
            g_pendingRenderHeight = 0;
        }
        if (g_pendingHdriIndex >= 0) {
            vkDeviceWaitIdle(m_context->getDevice());
            loadEnvironmentHdri(g_pendingHdriIndex);
            m_selectedHdriIndex = g_pendingHdriIndex;
            g_pendingHdriIndex = -1;
        }

#ifdef USE_GLFW
        uint32_t imageIndex = 0;
        if (!m_context->isHeadless()) {
            vkWaitForFences(m_context->getDevice(), 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
            VkResult result = vkAcquireNextImageKHR(m_context->getDevice(), m_context->getSwapchain(), UINT64_MAX, m_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
                m_context->recreateSwapchain();
                return;
            } else if (result != VK_SUCCESS) {
                return;
            }
            vkResetFences(m_context->getDevice(), 1, &m_inFlightFence);
        }
#endif

        vkResetCommandBuffer(m_commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

        // =========================================================================
        // PRZYGOTOWANIE KAMERY, SŁOŃCA, 4 KASKAD CSM ORAZ LISTY OBIEKTÓW SCENY
        // =========================================================================
        glm::vec3 sunDir = glm::normalize(glm::vec3(0.45f, 0.85f, 0.30f));
        glm::vec3 sunColor(1.0f, 0.98f, 0.92f);
        float sunIntensity = 2.8f;
        glm::vec3 ambientColor(0.20f, 0.24f, 0.34f);
        float ambientIntensity = 0.85f;
        bool sunEnabled = true;
        bool sunDirectionFromPosition = false;

        if (engine) {
            const auto& env = engine->getEnvironmentSettings();
            sunDir = env.sunDirection;
            sunColor = env.sunColor;
            sunIntensity = env.sunIntensity;
            ambientColor = env.ambientColor;
            ambientIntensity = env.ambientIntensity;
            sunEnabled = env.sunEnabled;
            sunDirectionFromPosition = env.sunDirectionFromPosition;
        }

        const glm::vec3 camPos = engine ? engine->getCameraPosition() : glm::vec3(glm::inverse(viewMatrix)[3]);

        glm::vec3 sunPos(0.0f, 18.0f, 0.0f);
        auto dirLightView = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
        bool hasDirLight = false;
        for (auto dlEntity : dirLightView) {
            const auto& lc = dirLightView.get<ecs::LightComponent>(dlEntity);
            if (lc.type == "directional" || lc.type == "sun") {
                hasDirLight = true;
                const auto& lt = dirLightView.get<ecs::TransformComponent>(dlEntity);
                sunPos = lt.position;
                if (sunDirectionFromPosition) {
                    sunDir = glm::length(sunPos) > 0.001f ? glm::normalize(sunPos) : glm::vec3(0.0f, 1.0f, 0.0f);
                } else {
                    const float pitch = glm::radians(lt.rotation.x);
                    const float yaw   = glm::radians(lt.rotation.y);
                    glm::vec3 sunForward = glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
                    sunDir = glm::normalize(-sunForward);
                }
                sunColor = lc.color;
                sunIntensity = lc.intensity;
                break;
            }
        }

        if (!hasDirLight && engine && !m_standaloneGameMode) {
            auto sunEntity = registry.createEntity();
            registry.get().emplace<ecs::TagComponent>(sunEntity, ecs::TagComponent{"DirectionalLight (Sun)"});
            registry.get().emplace<ecs::TransformComponent>(sunEntity, glm::vec3(0.0f, 18.0f, 0.0f), glm::vec3(-45.0f, -40.0f, 0.0f), glm::vec3(1.0f));
            ecs::LightComponent sl;
            sl.type = "directional";
            sl.color = glm::vec3(1.0f, 0.98f, 0.92f);
            sl.intensity = 3.5f;
            sl.range = 0.0f;
            registry.get().emplace<ecs::LightComponent>(sunEntity, sl);
            sunPos = glm::vec3(0.0f, 18.0f, 0.0f);
            if (sunDirectionFromPosition) {
                sunDir = glm::length(sunPos) > 0.001f ? glm::normalize(sunPos) : glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                const float pitch = glm::radians(-45.0f);
                const float yaw   = glm::radians(-40.0f);
                sunDir = glm::normalize(-glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
            }
            sunColor = sl.color;
            sunIntensity = sl.intensity;
        }

        if (!sunEnabled) {
            sunIntensity = 0.0f;
        } else {
            float horizonMetric = sunDirectionFromPosition ? (sunPos.y / std::max(1.0f, glm::length(sunPos))) : sunDir.y;
            if (horizonMetric <= 0.0f) {
                sunIntensity = 0.0f;
            } else if (horizonMetric < 0.08f) {
                sunIntensity *= (horizonMetric / 0.08f); // Plynne sciemnianie zmierzchowe / switu
            }
        }
#ifdef USE_GLFW
        const float sceneAspect = (m_standaloneGameMode || m_viewportAspectRatio <= 0.1f)
            ? (static_cast<float>(m_width) / static_cast<float>(std::max(1u, m_height)))
            : m_viewportAspectRatio;
#else
        const float sceneAspect = static_cast<float>(m_width) / static_cast<float>(std::max(1u, m_height));
#endif
        float sceneFov = engine ? engine->getCameraFov() : 45.0f;
        if (sceneFov < 10.0f || sceneFov > 140.0f) sceneFov = 45.0f;

        glm::mat4 projection = glm::perspective(glm::radians(sceneFov), sceneAspect, 0.1f, 1000.0f);
        projection[1][1] *= -1.0f;
        const glm::mat4 vp = projection * viewMatrix;

        glm::vec3 camFront(0.0f, 0.0f, -1.0f);
        if (engine) {
            const float yRad = glm::radians(engine->getCameraYaw());
            const float pRad = glm::radians(engine->getCameraPitch());
            camFront = glm::normalize(glm::vec3(std::cos(yRad) * std::cos(pRad), std::sin(pRad), std::sin(yRad) * std::cos(pRad)));
        }
        const glm::vec3 camRight = glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 camUp    = glm::normalize(glm::cross(camRight, camFront));

        const float cSplits[SHADOW_CASCADE_COUNT] = {
            std::min(m_shadowDistance * 0.10f, 8.0f),
            std::min(m_shadowDistance * 0.28f, 22.0f),
            std::min(m_shadowDistance * 0.60f, 54.0f),
            m_shadowDistance
        };
        float cRadii[SHADOW_CASCADE_COUNT]{};
        glm::mat4 cascadeLightSpace[SHADOW_CASCADE_COUNT]{};
        const float fovRad = glm::radians(sceneFov);

        for (uint32_t c = 0; c < SHADOW_CASCADE_COUNT; ++c) {
            const float nDist = (c == 0) ? 0.1f : (cSplits[c - 1] * 0.85f);
            const float fDist = cSplits[c];
            const float hFar  = std::tan(fovRad * 0.5f) * fDist;
            const float wFar  = hFar * sceneAspect;

            const glm::vec3 nCenter = camPos + camFront * nDist;
            const glm::vec3 fCenter = camPos + camFront * fDist;
            const glm::vec3 fMid    = (nCenter + fCenter) * 0.5f;
            const glm::vec3 farTopRight = fCenter + camRight * wFar + camUp * hFar;
            const float radius = std::ceil(glm::length(farTopRight - fMid) * 16.0f) / 16.0f;
            cRadii[c] = radius;

            // Stabilizacja tekselowa eliminująca drżenie krawędzi cieni podczas ruchu kamery
            const glm::vec3 upRef = (std::abs(sunDir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            const float texelsPerUnit = static_cast<float>(m_shadowWidth) / (radius * 2.0f);
            const glm::mat4 tempLook = glm::lookAt(sunDir * 100.0f, glm::vec3(0.0f), upRef);
            glm::vec3 lightMid = glm::vec3(tempLook * glm::vec4(fMid, 1.0f));
            lightMid.x = std::floor(lightMid.x * texelsPerUnit) / texelsPerUnit;
            lightMid.y = std::floor(lightMid.y * texelsPerUnit) / texelsPerUnit;
            const glm::vec3 snappedMid = glm::vec3(glm::inverse(tempLook) * glm::vec4(lightMid, 1.0f));

            const float cHeight = std::max(20.0f, std::max(0.5f, sunPos.y));
            const float cDist = std::max(radius + 35.0f, cHeight / std::max(0.15f, std::max(0.05f, sunDir.y)));
            const glm::mat4 lView = glm::lookAt(snappedMid + sunDir * cDist, snappedMid, upRef);
            glm::mat4 lProj = glm::ortho(-radius, radius, -radius, radius, 0.5f, cDist * 2.5f);
            lProj[1][1] *= -1.0f;
            cascadeLightSpace[c] = lProj * lView;
        }

        // Wybór głównego światła punktowego rzucającego cienie Cubemap
        int shadowedPointLightIdx = -1;
        float shadowedPointLightRange = 25.0f;
        glm::vec3 shadowedPointLightPos(0.0f);
        {
            auto lightView = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
            int localIdx = 0;
            for (auto lEntity : lightView) {
                const auto& lc = lightView.get<ecs::LightComponent>(lEntity);
                if (lc.type == "directional" || lc.type == "sun") continue;
                if (localIdx >= 8) break;
                if ((lc.type == "point" || lc.type == "spot") && shadowedPointLightIdx < 0 && lc.range > 0.1f && lc.intensity > 0.05f) {
                    shadowedPointLightPos = lightView.get<ecs::TransformComponent>(lEntity).position;
                    shadowedPointLightRange = lc.range;
                    shadowedPointLightIdx = localIdx;
                }
                localIdx++;
            }
        }

        // Zapis SceneUboData przed wszystkimi przebiegami (używane przez Depth Prepass, PBR i Skybox)
        if (m_sceneUboMappedPtr) {
            SceneUboData ubo{};
            ubo.viewProj = vp;
            ubo.view = viewMatrix;
            ubo.invViewProj = glm::inverse(vp);
            for (int c = 0; c < 4; ++c) ubo.cascadeLightSpace[c] = cascadeLightSpace[c];
            ubo.cascadeSplits = glm::vec4(cSplits[0], cSplits[1], cSplits[2], cSplits[3]);
            ubo.cascadeRadii  = glm::vec4(cRadii[0],  cRadii[1],  cRadii[2],  cRadii[3]);
            ubo.cameraPos     = glm::vec4(camPos, 1.0f);
            ubo.sunDirection  = glm::vec4(sunDir, sunIntensity);
            ubo.sunPosition   = glm::vec4(sunPos, 1.0f);
            ubo.sunColor      = glm::vec4(sunColor, 1.0f);
            ubo.ambientColor  = glm::vec4(ambientColor, ambientIntensity);
            ubo.fogParams     = glm::vec4(m_fogEnabled ? 1.0f : 0.0f, m_fogDensity, m_fogHeightFalloff, 0.0f);
            ubo.fogColor      = glm::vec4(m_fogColor, 1.0f);
            ubo.screenParams  = glm::vec4(static_cast<float>(m_width), static_cast<float>(m_height),
                                          1.0f / static_cast<float>(std::max(1u, m_width)),
                                          1.0f / static_cast<float>(std::max(1u, m_height)));
            ubo.pointShadowParams = glm::vec4(static_cast<float>(shadowedPointLightIdx), 0.1f, shadowedPointLightRange, 0.015f);
            ubo.bounceParams = glm::vec4(
                m_lightBounceEnabled ? 1.0f : 0.0f,
                m_lightBounceIntensity,
                m_lightBounceGroundReflectance,
                m_lightBounceMultiBounce
            );
            auto srgbToLinear = [](float c) {
                return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
            };
            glm::vec3 linearGroundColor(
                srgbToLinear(m_lightBounceGroundColor.r),
                srgbToLinear(m_lightBounceGroundColor.g),
                srgbToLinear(m_lightBounceGroundColor.b)
            );
            ubo.bounceColor = glm::vec4(linearGroundColor, 1.0f);
            ubo.frameIndex = static_cast<int>(m_frameCounter++);
            ubo.skyboxMode = m_skyboxEnabled ? 1 : 0;

            auto lightView = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
            int lightIdx = 0;
            for (auto lEntity : lightView) {
                if (lightIdx >= 8) break;
                const auto& lt = lightView.get<ecs::TransformComponent>(lEntity);
                const auto& lc = lightView.get<ecs::LightComponent>(lEntity);
                if (lc.type == "directional" || lc.type == "sun") continue;

                float lType = 0.0f;
                if (lc.type == "spot") lType = 1.0f;
                else if (lc.type == "area") lType = 2.0f;

                const float pitch = glm::radians(lt.rotation.x);
                const float yaw   = glm::radians(lt.rotation.y);
                const glm::vec3 forward = glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));

                ubo.lightsPos[lightIdx] = glm::vec4(lt.position, std::max(1.0f, lc.range));
                ubo.lightsCol[lightIdx] = glm::vec4(lc.color, std::max(0.0f, lc.intensity));
                ubo.lightsDir[lightIdx] = glm::vec4(forward, lType);
                ubo.lightsParams[lightIdx] = glm::vec4(
                    std::cos(glm::radians(std::clamp(lc.innerConeAngle, 1.0f, 85.0f))),
                    std::cos(glm::radians(std::clamp(lc.outerConeAngle, lc.innerConeAngle + 1.0f, 89.0f))),
                    std::max(0.1f, lc.areaSizeX),
                    std::max(0.1f, lc.areaSizeY)
                );
                lightIdx++;
            }
            ubo.numLights = lightIdx;

            // Analityczne cienie sferyczne usuniete - zastepuja je rzeczywiste cienie mapowane
            ubo.numOccluders = 0;
            std::memcpy(m_sceneUboMappedPtr, &ubo, sizeof(SceneUboData));
        }

        // Budowa listy rysowania PBR (wspólna dla Depth Prepass oraz Main PBR Pass)
        std::vector<DrawItem> drawItems;
        if (assetManager) {
            auto meshEntities = registry.get().view<ecs::TransformComponent, ecs::MeshComponent>();
            drawItems.reserve(64);
            for (auto entity : meshEntities) {
                const auto& transform = meshEntities.get<ecs::TransformComponent>(entity);
                const auto& meshComp  = meshEntities.get<ecs::MeshComponent>(entity);
                std::string tag = registry.get().all_of<ecs::TagComponent>(entity) ? registry.get().get<ecs::TagComponent>(entity).name : "";

                const bool isLight = registry.get().all_of<ecs::LightComponent>(entity) ||
                                     (tag.find("Light") != std::string::npos) || (tag.find("light") != std::string::npos);
                if (isLight) continue;

                core::MeshAsset* meshAsset = assetManager->getMesh(meshComp.meshId);
                if (!meshAsset || !meshAsset->vertexBuffer || !meshAsset->indexBuffer) continue;

                glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.position)
                                * glm::mat4_cast(transform.orientation)
                                * glm::scale(glm::mat4(1.0f), transform.scale);

                const bool isPlane = (meshComp.meshId == 1 || tag == "GroundPlane" || tag == "Ground");
                glm::vec4 baseColor(0.22f, 0.74f, 0.97f, 1.0f);
                float metallic = 0.18f;
                float roughness = 0.42f;
                float emissive = 0.0f;

                if (isPlane) {
                    if (meshComp.meshId == 1) model = glm::scale(model, glm::vec3(100.0f, 4.0f, 100.0f));
                    baseColor = glm::vec4(0.16f, 0.19f, 0.25f, 1.0f);
                    metallic  = 0.05f;
                    roughness = 0.78f;
                } else if (registry.get().all_of<ecs::MaterialComponent>(entity)) {
                    const auto& mat = registry.get().get<ecs::MaterialComponent>(entity);
                    baseColor = mat.baseColor;
                    metallic  = mat.metallic;
                    roughness = mat.roughness;
                    emissive  = mat.emissiveIntensity;
                } else if (meshAsset->materialId != 0 && assetManager->getMaterial(meshAsset->materialId)) {
                    core::MaterialAsset* mat = assetManager->getMaterial(meshAsset->materialId);
                    baseColor = mat->baseColorFactor;
                    metallic  = mat->metallicFactor;
                    roughness = mat->roughnessFactor;
                    emissive  = 0.0f;
                } else {
                    bool isCapsule = false;
                    bool isSensor  = false;
                    if (registry.get().all_of<ecs::RigidBodyComponent>(entity)) {
                        const auto& rb = registry.get().get<ecs::RigidBodyComponent>(entity);
                        isCapsule = (rb.shapeType == 2);
                        isSensor  = rb.isSensor;
                    }
                    glm::vec3 col(0.22f, 0.74f, 0.97f);
                    if (tag == "Sun" || tag == "Coin" || tag == "CoinTrigger" || tag.find("Coin") != std::string::npos) {
                        col = glm::vec3(0.980f, 0.804f, 0.082f); metallic = 0.85f; roughness = 0.22f; emissive = 0.45f;
                    } else if (tag == "Earth" || tag.find("Player") != std::string::npos || isCapsule) {
                        col = glm::vec3(0.000f, 0.941f, 1.000f); emissive = 0.35f;
                    } else if (tag == "Paddle") {
                        col = glm::vec3(0.063f, 0.725f, 0.506f);
                    } else if (tag == "Ball") {
                        col = glm::vec3(0.957f, 0.247f, 0.369f); emissive = 0.45f;
                    } else if (tag == "Brick") {
                        col = glm::vec3(0.659f, 0.333f, 0.969f);
                    } else if (tag == "LaserBullet") {
                        col = glm::vec3(0.000f, 1.000f, 1.000f); emissive = 0.8f;
                    } else if (tag.find("KillZ") != std::string::npos) {
                        col = glm::vec3(0.937f, 0.267f, 0.267f);
                    } else if (tag == "Checkpoint" || tag == "SpeedBoost") {
                        col = glm::vec3(0.133f, 0.773f, 0.369f);
                    } else if (isSensor) {
                        col = glm::vec3(0.918f, 0.702f, 0.031f);
                    } else if (tag.find("Enemy") != std::string::npos || tag.find("Meteor") != std::string::npos || tag == "Mars") {
                        col = glm::vec3(0.973f, 0.443f, 0.443f);
                    } else {
                        const uint32_t eId = static_cast<uint32_t>(entity);
                        col = glm::vec3(
                            0.20f + 0.80f * static_cast<float>((eId * 31u) % 100u) / 100.0f,
                            0.20f + 0.80f * static_cast<float>((eId * 53u) % 100u) / 100.0f,
                            0.20f + 0.80f * static_cast<float>((eId * 97u) % 100u) / 100.0f
                        );
                    }
                    baseColor = glm::vec4(col, 1.0f);
                }

                const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));
                DrawItem item{};
                item.mesh = meshAsset;
                item.pcs.model = model;
                item.pcs.normalCol0 = glm::vec4(normalMat[0], metallic);
                item.pcs.normalCol1 = glm::vec4(normalMat[1], roughness);
                item.pcs.normalCol2 = glm::vec4(normalMat[2], emissive);
                item.pcs.baseColorFactor = glm::vec4(glm::vec3(baseColor), 1.0f);
                drawItems.push_back(item);
            }
        }

        // =========================================================================
        // PASS 1A: DIRECTIONAL 4-CASCADE SHADOW MAPS (4096x4096)
        // =========================================================================
        if (m_shadowPipeline && m_shadowImage) {
            transitionImage(m_commandBuffer, m_shadowImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, SHADOW_CASCADE_COUNT,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
            VkViewport shadowVp{ 0.0f, 0.0f, static_cast<float>(m_shadowWidth), static_cast<float>(m_shadowHeight), 0.0f, 1.0f };
            VkRect2D shadowSc{ {0, 0}, {m_shadowWidth, m_shadowHeight} };
            vkCmdSetViewport(m_commandBuffer, 0, 1, &shadowVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &shadowSc);

            auto shadowView = registry.get().view<ecs::TransformComponent, ecs::MeshComponent>();
            for (uint32_t c = 0; c < SHADOW_CASCADE_COUNT; ++c) {
                VkRenderingAttachmentInfo depthAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
                depthAtt.imageView = m_shadowCascadeImageViews[c];
                depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                depthAtt.clearValue.depthStencil = { 1.0f, 0 };

                VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
                ri.renderArea = { {0, 0}, {m_shadowWidth, m_shadowHeight} };
                ri.layerCount = 1;
                ri.pDepthAttachment = &depthAtt;
                vkCmdBeginRendering(m_commandBuffer, &ri);

                if (sunIntensity > 0.001f && assetManager) {
                    for (auto entity : shadowView) {
                        const auto& tc = shadowView.get<ecs::TransformComponent>(entity);
                        const auto& mc = shadowView.get<ecs::MeshComponent>(entity);
                        std::string tag = registry.get().all_of<ecs::TagComponent>(entity) ? registry.get().get<ecs::TagComponent>(entity).name : "";
                        if (mc.meshId == 1 || tag == "GroundPlane" || tag == "Ground") continue;
                        if (registry.get().all_of<ecs::LightComponent>(entity) || tag.find("Light") != std::string::npos) continue;
                        if (registry.get().all_of<ecs::RigidBodyComponent>(entity) && registry.get().get<ecs::RigidBodyComponent>(entity).isSensor) continue;

                        core::MeshAsset* meshAsset = assetManager->getMesh(mc.meshId);
                        if (!meshAsset || !meshAsset->vertexBuffer || !meshAsset->indexBuffer) continue;

                        const glm::mat4 model = glm::translate(glm::mat4(1.0f), tc.position)
                                              * glm::mat4_cast(tc.orientation)
                                              * glm::scale(glm::mat4(1.0f), tc.scale);
                        const glm::mat4 lsMvp = cascadeLightSpace[c] * model;
                        vkCmdPushConstants(m_commandBuffer, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &lsMvp);

                        VkDeviceSize offset = 0;
                        vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &meshAsset->vertexBuffer, &offset);
                        vkCmdBindIndexBuffer(m_commandBuffer, meshAsset->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(m_commandBuffer, meshAsset->indexCount, 1, 0, 0, 0);
                    }
                }
                vkCmdEndRendering(m_commandBuffer);
            }

            transitionImage(m_commandBuffer, m_shadowImage,
                            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, SHADOW_CASCADE_COUNT,
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }

        // =========================================================================
        // PASS 1B: OMNIDIRECTIONAL POINT LIGHT SHADOW CUBEMAP (1024x1024 x 6)
        // =========================================================================
        if (m_pointShadowImage && m_shadowPipeline) {
            transitionImage(m_commandBuffer, m_pointShadowImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 6,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

            if (shadowedPointLightIdx >= 0 && assetManager) {
                const glm::vec3 targets[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
                const glm::vec3 ups[6]     = { {0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0} };
                glm::mat4 pProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, shadowedPointLightRange);
                // Uwaga: Dla cubemapy Vulkan NIE odwracamy pProj[1][1], poniewaz wektory ups[6]
                // juz dokladnie definiuja orientacje scian cubemapy zgodnie ze specyfikacja Vulkan/OpenGL.
                // Odwrocenie pProj[1][1] powodowalo lustrzane odbicie cieni wzdluz osi Z na scianie dolnej (-Y).

                VkViewport vpCube{ 0.0f, 0.0f, static_cast<float>(POINT_SHADOW_MAP_SIZE), static_cast<float>(POINT_SHADOW_MAP_SIZE), 0.0f, 1.0f };
                VkRect2D scCube{ {0, 0}, {POINT_SHADOW_MAP_SIZE, POINT_SHADOW_MAP_SIZE} };
                auto view = registry.get().view<ecs::TransformComponent, ecs::MeshComponent>();

                for (uint32_t face = 0; face < 6; ++face) {
                    VkRenderingAttachmentInfo faceAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
                    faceAtt.imageView = m_pointShadowFaceViews[face];
                    faceAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    faceAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    faceAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    faceAtt.clearValue.depthStencil = { 1.0f, 0 };

                    VkRenderingInfo faceRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
                    faceRi.renderArea = { {0, 0}, {POINT_SHADOW_MAP_SIZE, POINT_SHADOW_MAP_SIZE} };
                    faceRi.layerCount = 1;
                    faceRi.pDepthAttachment = &faceAtt;

                    vkCmdBeginRendering(m_commandBuffer, &faceRi);
                    vkCmdSetViewport(m_commandBuffer, 0, 1, &vpCube);
                    vkCmdSetScissor(m_commandBuffer, 0, 1, &scCube);
                    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);

                    const glm::mat4 pVp = pProj * glm::lookAt(shadowedPointLightPos, shadowedPointLightPos + targets[face], ups[face]);
                    for (auto entity : view) {
                        const auto& tc = view.get<ecs::TransformComponent>(entity);
                        const auto& mc = view.get<ecs::MeshComponent>(entity);
                        if (mc.meshId == 1) continue;
                        std::string tag = registry.get().all_of<ecs::TagComponent>(entity) ? registry.get().get<ecs::TagComponent>(entity).name : "";
                        if (tag.find("Light") != std::string::npos || tag.find("light") != std::string::npos) continue;

                        core::MeshAsset* mesh = assetManager->getMesh(mc.meshId);
                        if (!mesh || !mesh->vertexBuffer || !mesh->indexBuffer) continue;

                        const glm::mat4 mvp = pVp * glm::translate(glm::mat4(1.0f), tc.position)
                                            * glm::mat4_cast(tc.orientation)
                                            * glm::scale(glm::mat4(1.0f), tc.scale);
                        vkCmdPushConstants(m_commandBuffer, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);

                        VkDeviceSize offset = 0;
                        vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &mesh->vertexBuffer, &offset);
                        vkCmdBindIndexBuffer(m_commandBuffer, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(m_commandBuffer, mesh->indexCount, 1, 0, 0, 0);
                    }
                    vkCmdEndRendering(m_commandBuffer);
                }
            }

            transitionImage(m_commandBuffer, m_pointShadowImage,
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 6,
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }

        const VkViewport fullVp{ 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f };
        const VkRect2D fullSc{ {0, 0}, {m_width, m_height} };

        // =========================================================================
        // PASS 2A: DEPTH PREPASS (Wypełnienie Z-Bufora dla GTAO oraz Early-Z)
        // =========================================================================
        transitionImage(m_commandBuffer, m_depthImage,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, 0,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

        {
            VkRenderingAttachmentInfo depthPreAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            depthPreAtt.imageView = m_depthImageView;
            depthPreAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthPreAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthPreAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthPreAtt.clearValue.depthStencil = { 1.0f, 0 };

            VkRenderingInfo preRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            preRi.renderArea = { {0, 0}, {m_width, m_height} };
            preRi.layerCount = 1;
            preRi.pDepthAttachment = &depthPreAtt;

            vkCmdBeginRendering(m_commandBuffer, &preRi);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);
            if (m_depthPrepassPipeline && m_descriptorSet) {
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_depthPrepassPipeline);
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
                for (const auto& item : drawItems) {
                    vkCmdPushConstants(m_commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PbrPushConstants), &item.pcs);
                    VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &item.mesh->vertexBuffer, &offset);
                    vkCmdBindIndexBuffer(m_commandBuffer, item.mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(m_commandBuffer, item.mesh->indexCount, 1, 0, 0, 0);
                }
            }
            vkCmdEndRendering(m_commandBuffer);
        }

        // =========================================================================
        // PASS 2B: MULTI-BOUNCE GTAO + BILATERAL BLUR (przed cieniowaniem IBL PBR!)
        // =========================================================================
        transitionImage(m_commandBuffer, m_depthImage,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        if ((m_gtaoEnabled || m_lightBounceEnabled) && m_gtaoPipeline && m_gtaoBlurPipeline) {
            transitionImage(m_commandBuffer, m_aoImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            VkRenderingAttachmentInfo aoAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            aoAtt.imageView = m_aoImageView;
            aoAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            aoAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            aoAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo aoRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            aoRi.renderArea = { {0, 0}, {m_width, m_height} };
            aoRi.layerCount = 1;
            aoRi.colorAttachmentCount = 1;
            aoRi.pColorAttachments = &aoAtt;

            vkCmdBeginRendering(m_commandBuffer, &aoRi);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gtaoPipeline);
            vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gtaoPipelineLayout, 0, 1, &m_gtaoDescSet, 0, nullptr);

            const float tanHalfFovY = std::tan(glm::radians(sceneFov) * 0.5f);
            const glm::vec4 gtaoPcs[3] = {
                glm::vec4(m_gtaoRadius, m_gtaoIntensity, tanHalfFovY, sceneAspect),
                glm::vec4(1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height), 0.1f, 1000.0f),
                glm::vec4(m_gtaoEnabled ? 1.0f : 0.0f, m_lightBounceEnabled ? 1.0f : 0.0f, 0.0f, 0.0f)
            };
            vkCmdPushConstants(m_commandBuffer, m_gtaoPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(gtaoPcs), gtaoPcs);
            vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            vkCmdEndRendering(m_commandBuffer);

            transitionImage(m_commandBuffer, m_aoImage,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

            transitionImage(m_commandBuffer, m_aoBlurImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            VkRenderingAttachmentInfo blurAtt = aoAtt;
            blurAtt.imageView = m_aoBlurImageView;
            VkRenderingInfo blurRi = aoRi;
            blurRi.pColorAttachments = &blurAtt;

            vkCmdBeginRendering(m_commandBuffer, &blurRi);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gtaoBlurPipeline);
            vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gtaoBlurPipelineLayout, 0, 1, &m_gtaoBlurDescSet, 0, nullptr);

            const glm::vec4 blurPcs(1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height), 0.1f, 1000.0f);
            vkCmdPushConstants(m_commandBuffer, m_gtaoBlurPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), &blurPcs);
            vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            vkCmdEndRendering(m_commandBuffer);

            transitionImage(m_commandBuffer, m_aoBlurImage,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        } else {
            transitionImage(m_commandBuffer, m_aoBlurImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            VkRenderingAttachmentInfo clearAo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            clearAo.imageView = m_aoBlurImageView;
            clearAo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            clearAo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            clearAo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            clearAo.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

            VkRenderingInfo clearRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            clearRi.renderArea = { {0, 0}, {m_width, m_height} };
            clearRi.layerCount = 1;
            clearRi.colorAttachmentCount = 1;
            clearRi.pColorAttachments = &clearAo;
            vkCmdBeginRendering(m_commandBuffer, &clearRi);
            vkCmdEndRendering(m_commandBuffer);

            transitionImage(m_commandBuffer, m_aoBlurImage,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }

        // =========================================================================
        // PASS 3: MAIN HDR PBR PASS + CUBEMAP SKYBOX + SOFT GAUSSIAN PARTICLES
        // =========================================================================
        transitionImage(m_commandBuffer, m_hdrImage,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        transitionImage(m_commandBuffer, m_depthImage,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);

        {
            VkRenderingAttachmentInfo hdrColorAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            hdrColorAtt.imageView = m_hdrImageView;
            hdrColorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            hdrColorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            hdrColorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            hdrColorAtt.clearValue.color = { {0.015f, 0.018f, 0.032f, 1.0f} };

            VkRenderingAttachmentInfo hdrDepthAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            hdrDepthAtt.imageView = m_depthImageView;
            hdrDepthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            hdrDepthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Zachowujemy Z-bufor z Depth Prepass!
            hdrDepthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo hdrRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            hdrRi.renderArea = { {0, 0}, {m_width, m_height} };
            hdrRi.layerCount = 1;
            hdrRi.colorAttachmentCount = 1;
            hdrRi.pColorAttachments = &hdrColorAtt;
            hdrRi.pDepthAttachment = &hdrDepthAtt;

            vkCmdBeginRendering(m_commandBuffer, &hdrRi);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);

            if (m_graphicsPipeline && m_descriptorSet) {
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
                VkDescriptorSet currentBoundSet = VK_NULL_HANDLE;
                for (const auto& item : drawItems) {
                    VkDescriptorSet set = m_descriptorSet;
                    if (item.mesh && item.mesh->materialId != 0) {
                        set = getOrCreateMaterialDescriptorSet(item.mesh->materialId, assetManager);
                    }
                    if (set != currentBoundSet) {
                        vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &set, 0, nullptr);
                        currentBoundSet = set;
                    }
                    vkCmdPushConstants(m_commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PbrPushConstants), &item.pcs);
                    VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &item.mesh->vertexBuffer, &offset);
                    vkCmdBindIndexBuffer(m_commandBuffer, item.mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(m_commandBuffer, item.mesh->indexCount, 1, 0, 0, 0);
                }
            }

            // Fizyczny Skybox HDR z prefiltrowanej Cubemapy (renderowany tylko tam, gdzie depth == 1.0)
            if (m_skyboxEnabled && m_skyboxPipeline && m_descriptorSet) {
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyboxPipeline);
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyboxPipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
                vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            }

            // Energetyczne cząsteczki HDR (Niagara-style)
            if (m_particlePipeline && m_particleMappedPtr) {
                std::vector<ParticleVertex> particles;
                particles.reserve(2048);

                static auto particleClockStart = std::chrono::steady_clock::now();
                const float curTime = std::chrono::duration<float>(std::chrono::steady_clock::now() - particleClockStart).count();

                auto particleView = registry.get().view<ecs::TransformComponent, ecs::ParticleEmitterComponent>();
                for (auto entity : particleView) {
                    const auto& transform = particleView.get<ecs::TransformComponent>(entity);
                    const auto& emitter   = particleView.get<ecs::ParticleEmitterComponent>(entity);
                    const uint32_t numParticles = std::min(emitter.count, 500u);
                    const glm::vec3 basePos = transform.position;
                    const glm::vec3 emitColor = emitter.color;
                    const float emitSpeed = emitter.speed > 0.01f ? emitter.speed : 3.0f;
                    const float emitSize  = emitter.size  > 0.01f ? emitter.size  : 0.4f;

                    for (uint32_t i = 0; i < numParticles && particles.size() < MAX_PARTICLES; ++i) {
                        const float seed  = static_cast<float>((i * 1337u) % 1000u) / 1000.0f;
                        const float seed2 = static_cast<float>((i * 7919u) % 1000u) / 1000.0f;
                        const float seed3 = static_cast<float>((i * 4973u) % 1000u) / 1000.0f;
                        const float lifetime = 1.0f + seed * 1.5f;
                        const float age = std::fmod(curTime * emitSpeed + seed * lifetime, lifetime);
                        const float progress = age / lifetime;

                        ParticleVertex pv{};
                        if (emitter.type == "sparks") {
                            const float phi = seed2 * 6.283185f;
                            const float theta = seed3 * 3.14159f;
                            const glm::vec3 dir(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
                            pv.pos = basePos + dir * (age * emitSpeed) + glm::vec3(0.0f, -4.9f * age * age, 0.0f);
                            pv.color = glm::vec4(emitColor * 1.6f, 1.0f - progress);
                            pv.size = emitSize * (1.0f - progress * 0.5f);
                        } else if (emitter.type == "smoke") {
                            const glm::vec3 dir(std::sin(seed2 * 6.28f) * 0.5f, 1.0f, std::cos(seed3 * 6.28f) * 0.5f);
                            pv.pos = basePos + dir * (age * emitSpeed * 0.6f);
                            pv.color = glm::vec4(emitColor * 0.6f, std::sin(progress * 3.14159f) * 0.35f);
                            pv.size = emitSize * (1.0f + progress * 2.5f);
                        } else if (emitter.type == "portal") {
                            const float angle = curTime * emitSpeed * 2.0f + seed * 6.283185f;
                            const float radius = 0.8f + 0.3f * std::sin(angle * 2.0f);
                            pv.pos = basePos + glm::vec3(std::cos(angle) * radius, (seed2 - 0.5f) * 0.5f + std::sin(angle * 3.0f) * 0.2f, std::sin(angle) * radius);
                            pv.color = glm::vec4(emitColor * 1.4f, 0.85f);
                            pv.size = emitSize * (0.8f + 0.4f * std::sin(curTime * 5.0f + seed));
                        } else if (emitter.type == "explosion") {
                            const float phi = seed2 * 6.283185f;
                            const float theta = seed3 * 3.14159f;
                            const glm::vec3 dir(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
                            pv.pos = basePos + dir * (progress * emitSpeed * 2.5f);
                            pv.color = glm::vec4(glm::mix(emitColor * 2.2f, glm::vec3(0.2f, 0.05f, 0.0f), progress), std::max(0.0f, 1.0f - progress));
                            pv.size = emitSize * (1.5f - progress);
                        } else {
                            const float phi = seed2 * 6.283185f;
                            const float r = (1.0f - progress) * 0.3f;
                            pv.pos = basePos + glm::vec3(std::cos(phi) * r, age * emitSpeed, std::sin(phi) * r);
                            pv.color = glm::vec4(glm::mix(emitColor * 1.8f, glm::vec3(0.8f, 0.1f, 0.0f), progress), 1.0f - progress);
                            pv.size = emitSize * (1.0f - progress * 0.6f);
                        }
                        particles.push_back(pv);
                    }
                }

                if (!particles.empty()) {
                    std::memcpy(m_particleMappedPtr, particles.data(), particles.size() * sizeof(ParticleVertex));
                    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_particlePipeline);
                    vkCmdPushConstants(m_commandBuffer, m_particlePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &vp);
                    VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &m_particleVertexBuffer, &offset);
                    vkCmdDraw(m_commandBuffer, static_cast<uint32_t>(particles.size()), 1, 0, 0);
                }
            }

            vkCmdEndRendering(m_commandBuffer);
        }

        // =========================================================================
        // PASS 4: 5-MIP JIMENEZ BLOOM PYRAMID (13-Tap Karis Downsample + 3x3 Tent Up)
        // =========================================================================
        transitionImage(m_commandBuffer, m_hdrImage,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        if (m_bloomEnabled && m_bloomDownPipeline && m_bloomUpPipeline && m_activeBloomMips > 0) {
            // Progresywny Downsampling: HDR -> Mip0 (Karis Average) -> Mip1 -> ... -> Mip(N-1)
            for (uint32_t m = 0; m < m_activeBloomMips; ++m) {
                transitionImage(m_commandBuffer, m_bloomImage,
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, m, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

                const VkExtent2D dstExt = m_bloomMipExtents[m];
                const VkExtent2D srcExt = (m == 0) ? VkExtent2D{ m_width, m_height } : m_bloomMipExtents[m - 1];

                VkRenderingAttachmentInfo bAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
                bAtt.imageView = m_bloomMipViews[m];
                bAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                bAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                bAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                VkRenderingInfo bRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
                bRi.renderArea = { {0, 0}, dstExt };
                bRi.layerCount = 1;
                bRi.colorAttachmentCount = 1;
                bRi.pColorAttachments = &bAtt;

                vkCmdBeginRendering(m_commandBuffer, &bRi);
                VkViewport bVp{ 0.0f, 0.0f, static_cast<float>(dstExt.width), static_cast<float>(dstExt.height), 0.0f, 1.0f };
                VkRect2D bSc{ {0, 0}, dstExt };
                vkCmdSetViewport(m_commandBuffer, 0, 1, &bVp);
                vkCmdSetScissor(m_commandBuffer, 0, 1, &bSc);
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomDownPipeline);

                VkDescriptorSet srcDs = (m == 0) ? m_bloomDownHdrDescSet : m_bloomMipDescSets[m - 1];
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomPipelineLayout, 0, 1, &srcDs, 0, nullptr);

                const glm::vec4 downParams(
                    1.0f / static_cast<float>(std::max(1u, srcExt.width)),
                    1.0f / static_cast<float>(std::max(1u, srcExt.height)),
                    m_bloomThreshold,
                    static_cast<float>(m)
                );
                vkCmdPushConstants(m_commandBuffer, m_bloomPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), &downParams);
                vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
                vkCmdEndRendering(m_commandBuffer);

                transitionImage(m_commandBuffer, m_bloomImage,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, m, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            }

            // Progresywny addytywny Upsampling 3x3 Tent: Mip(N-1) -> ... -> Mip1 -> Mip0
            for (int m = static_cast<int>(m_activeBloomMips) - 2; m >= 0; --m) {
                transitionImage(m_commandBuffer, m_bloomImage,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(m), 1, 0, 1,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

                const VkExtent2D dstExt = m_bloomMipExtents[m];
                const VkExtent2D srcExt = m_bloomMipExtents[m + 1];

                VkRenderingAttachmentInfo uAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
                uAtt.imageView = m_bloomMipViews[m];
                uAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                uAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Kumulacja addytywna (ONE + ONE)
                uAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                VkRenderingInfo uRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
                uRi.renderArea = { {0, 0}, dstExt };
                uRi.layerCount = 1;
                uRi.colorAttachmentCount = 1;
                uRi.pColorAttachments = &uAtt;

                vkCmdBeginRendering(m_commandBuffer, &uRi);
                VkViewport uVp{ 0.0f, 0.0f, static_cast<float>(dstExt.width), static_cast<float>(dstExt.height), 0.0f, 1.0f };
                VkRect2D uSc{ {0, 0}, dstExt };
                vkCmdSetViewport(m_commandBuffer, 0, 1, &uVp);
                vkCmdSetScissor(m_commandBuffer, 0, 1, &uSc);
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomUpPipeline);
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomPipelineLayout, 0, 1, &m_bloomMipDescSets[m + 1], 0, nullptr);

                const glm::vec4 upParams(
                    1.0f / static_cast<float>(std::max(1u, srcExt.width)),
                    1.0f / static_cast<float>(std::max(1u, srcExt.height)),
                    1.0f,
                    0.85f
                );
                vkCmdPushConstants(m_commandBuffer, m_bloomPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), &upParams);
                vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
                vkCmdEndRendering(m_commandBuffer);

                transitionImage(m_commandBuffer, m_bloomImage,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(m), 1, 0, 1,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            }
        }

        // =========================================================================
        // PASS 5: ACES RRT+ODT COMPOSITE + DEDICATED FXAA 3.11 PASS
        // =========================================================================
        const bool useFxaa = (m_antiAliasingMode > 0) && (m_fxaaPipeline != VK_NULL_HANDLE);
        VkImage compositeTargetImg = useFxaa ? m_ldrTempImage : m_colorImage;
        VkImageView compositeTargetView = useFxaa ? m_ldrTempImageView : m_colorImageView;

        transitionImage(m_commandBuffer, compositeTargetImg,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        {
            VkRenderingAttachmentInfo compAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            compAtt.imageView = compositeTargetView;
            compAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            compAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            compAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo compRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            compRi.renderArea = { {0, 0}, {m_width, m_height} };
            compRi.layerCount = 1;
            compRi.colorAttachmentCount = 1;
            compRi.pColorAttachments = &compAtt;

            vkCmdBeginRendering(m_commandBuffer, &compRi);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);
            if (m_compositePipeline) {
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePipeline);
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePipelineLayout, 0, 1, &m_compositeDescSet, 0, nullptr);
                struct CompositePushData {
                    glm::vec4 params;
                    glm::vec4 params2;
                } compData;
                compData.params = glm::vec4(
                    m_bloomIntensity,
                    m_exposure,
                    m_bloomEnabled ? 1.0f : 0.0f,
                    static_cast<float>(m_frameCounter)
                );
                compData.params2 = glm::vec4(useFxaa ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
                vkCmdPushConstants(m_commandBuffer, m_compositePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CompositePushData), &compData);
                vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            }
            vkCmdEndRendering(m_commandBuffer);
        }

        if (useFxaa) {
            transitionImage(m_commandBuffer, m_ldrTempImage,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

            transitionImage(m_commandBuffer, m_colorImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            VkRenderingAttachmentInfo fxaaAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            fxaaAtt.imageView = m_colorImageView;
            fxaaAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            fxaaAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            fxaaAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo fxaaRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            fxaaRi.renderArea = { {0, 0}, {m_width, m_height} };
            fxaaRi.layerCount = 1;
            fxaaRi.colorAttachmentCount = 1;
            fxaaRi.pColorAttachments = &fxaaAtt;

            vkCmdBeginRendering(m_commandBuffer, &fxaaRi);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fxaaPipeline);
            vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fxaaPipelineLayout, 0, 1, &m_fxaaDescSet, 0, nullptr);

            const float subpixQuality = (m_antiAliasingMode == 1) ? 0.50f : ((m_antiAliasingMode == 2) ? 0.75f : 0.90f);
            const float edgeThreshold = (m_antiAliasingMode == 1) ? 0.166f : ((m_antiAliasingMode == 2) ? 0.125f : 0.083f);
            const glm::vec4 fxaaParams(1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height), subpixQuality, edgeThreshold);
            vkCmdPushConstants(m_commandBuffer, m_fxaaPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), &fxaaParams);
            vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            vkCmdEndRendering(m_commandBuffer);
        }

        // =========================================================================
        // PASS 6: SWAPCHAIN BLIT / IMGUI EDITOR RENDERING & PRESENT
        // =========================================================================
#ifdef USE_GLFW
        if (!m_context->isHeadless()) {
            VkImage swapchainImg = m_context->getSwapchainImages()[imageIndex];

            if (m_standaloneGameMode) {
                transitionImage(m_commandBuffer, swapchainImg,
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
                transitionImage(m_commandBuffer, m_colorImage,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

                VkImageBlit blit{};
                blit.srcOffsets[1] = { static_cast<int32_t>(m_width), static_cast<int32_t>(m_height), 1 };
                blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                blit.dstOffsets[1] = { static_cast<int32_t>(m_context->getSwapchainExtent().width), static_cast<int32_t>(m_context->getSwapchainExtent().height), 1 };
                blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                vkCmdBlitImage(m_commandBuffer, m_colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               swapchainImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

                transitionImage(m_commandBuffer, swapchainImg,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
                transitionImage(m_commandBuffer, m_colorImage,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            } else {
                transitionImage(m_commandBuffer, swapchainImg,
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
                transitionImage(m_commandBuffer, m_colorImage,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            }

            renderImGui(m_commandBuffer, m_context->getSwapchainImageViews()[imageIndex], registry, assetManager, engine);

            transitionImage(m_commandBuffer, swapchainImg,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
        } else {
            transitionImage(m_commandBuffer, m_colorImage,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
        }
#else
        transitionImage(m_commandBuffer, m_colorImage,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
#endif

        vkEndCommandBuffer(m_commandBuffer);

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffer;

#ifdef USE_GLFW
        const VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        if (!m_context->isHeadless()) {
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &m_imageAvailableSemaphore;
            submitInfo.pWaitDstStageMask = waitStages;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &m_renderFinishedSemaphore;
            vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, m_inFlightFence);

            VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &m_renderFinishedSemaphore;
            VkSwapchainKHR swapChains[] = { m_context->getSwapchain() };
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = swapChains;
            presentInfo.pImageIndices = &imageIndex;
            VkResult res = vkQueuePresentKHR(m_context->getGraphicsQueue(), &presentInfo);
            if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_SURFACE_LOST_KHR) {
                m_context->recreateSwapchain();
            }
        } else {
            vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(m_context->getGraphicsQueue());
        }
#else
        vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_context->getGraphicsQueue());
#endif
    }

    void VulkanRenderer::createComputePipelines() {}

    void VulkanRenderer::initCaptureResources() {
        VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = static_cast<VkDeviceSize>(m_width) * m_height * 4u;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        VmaAllocationInfo stagingAllocInfo{};
        if (vmaCreateBuffer(m_context->getAllocator(), &bufferInfo, &allocInfo, &m_captureStagingBuffer, &m_captureStagingAlloc, &stagingAllocInfo) == VK_SUCCESS) {
            m_captureMappedPtr = stagingAllocInfo.pMappedData;
        }

        VkCommandBufferAllocateInfo cmdAllocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cmdAllocInfo.commandPool = m_context->getCommandPool();
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(m_context->getDevice(), &cmdAllocInfo, &m_captureCommandBuffer);
    }

    void VulkanRenderer::cleanupCaptureResources() {
        if (m_captureCommandBuffer != VK_NULL_HANDLE && m_context->getDevice()) {
            vkFreeCommandBuffers(m_context->getDevice(), m_context->getCommandPool(), 1, &m_captureCommandBuffer);
            m_captureCommandBuffer = VK_NULL_HANDLE;
        }
        if (m_captureStagingBuffer != VK_NULL_HANDLE && m_context->getAllocator()) {
            vmaDestroyBuffer(m_context->getAllocator(), m_captureStagingBuffer, m_captureStagingAlloc);
            m_captureStagingBuffer = VK_NULL_HANDLE;
            m_captureStagingAlloc = VK_NULL_HANDLE;
            m_captureMappedPtr = nullptr;
        }
    }

    static void writeJpegBuffer(void* context, void* data, int size) {
        auto* vec = static_cast<std::vector<uint8_t>*>(context);
        const auto* bytes = static_cast<const uint8_t*>(data);
        vec->insert(vec->end(), bytes, bytes + size);
    }

    static const char s_b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static std::string encodeBase64Data(const unsigned char* data, size_t len) {
        std::string ret;
        ret.reserve(4 * ((len + 2) / 3));
        for (size_t i = 0; i < len; i += 3) {
            uint32_t b = (static_cast<uint32_t>(data[i]) << 16);
            if (i + 1 < len) b |= (static_cast<uint32_t>(data[i + 1]) << 8);
            if (i + 2 < len) b |= static_cast<uint32_t>(data[i + 2]);
            ret.push_back(s_b64chars[(b >> 18) & 0x3F]);
            ret.push_back(s_b64chars[(b >> 12) & 0x3F]);
            ret.push_back((i + 1 < len) ? s_b64chars[(b >> 6) & 0x3F] : '=');
            ret.push_back((i + 2 < len) ? s_b64chars[b & 0x3F] : '=');
        }
        return ret;
    }

    std::string VulkanRenderer::captureFrameBase64() {
        if (m_width == 0 || m_height == 0) return "";
        if (!m_captureStagingBuffer || !m_captureMappedPtr || !m_captureCommandBuffer) {
            initCaptureResources();
            if (!m_captureStagingBuffer || !m_captureMappedPtr || !m_captureCommandBuffer) return "";
        }

        static std::string s_cachedFrame;
        static auto s_lastTime = std::chrono::steady_clock::now();
        const auto nowTime = std::chrono::steady_clock::now();
        if (!s_cachedFrame.empty() && std::chrono::duration<float, std::milli>(nowTime - s_lastTime).count() < 14.0f) {
            return s_cachedFrame;
        }

        vkResetCommandBuffer(m_captureCommandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(m_captureCommandBuffer, &beginInfo);

        transitionImage(m_captureCommandBuffer, m_colorImage,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { m_width, m_height, 1 };
        vkCmdCopyImageToBuffer(m_captureCommandBuffer, m_colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_captureStagingBuffer, 1, &region);

        transitionImage(m_captureCommandBuffer, m_colorImage,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        vkEndCommandBuffer(m_captureCommandBuffer);

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_captureCommandBuffer;
        vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_context->getGraphicsQueue());

        const uint8_t* srcPixels = static_cast<const uint8_t*>(m_captureMappedPtr);
        std::vector<uint8_t> jpegBytes;
        jpegBytes.reserve(256 * 1024);
        stbi_write_jpg_to_func(writeJpegBuffer, &jpegBytes, static_cast<int>(m_width), static_cast<int>(m_height), 4, srcPixels, 82);

        s_cachedFrame = "data:image/jpeg;base64," + encodeBase64Data(jpegBytes.data(), jpegBytes.size());
        s_lastTime = std::chrono::steady_clock::now();
        return s_cachedFrame;
    }

    void VulkanRenderer::submitBufferBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size) {
        VkBufferMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
        barrier.buffer = buffer;
        barrier.offset = 0;
        barrier.size = size;

        VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        depInfo.bufferMemoryBarrierCount = 1;
        depInfo.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

}
