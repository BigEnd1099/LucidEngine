#include "renderer/VulkanRenderer.hpp"
#include "core/Logger.hpp"
#include "renderer/Vertex.hpp"
#include "core/AssetManager.hpp"
#include "core/Engine.hpp"
#include <stdexcept>

#include "utils/FileIO.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <filesystem>
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
    VulkanRenderer::VulkanRenderer(VulkanContext* context, uint32_t width, uint32_t height)
        : m_context(context), m_width(width), m_height(height), m_baseWidth(width), m_baseHeight(height) {
        createOffscreenTarget();
        createCommandBuffer();
        createShadowResources();
        createDescriptorSets();
        createShadowPipeline();
        createComputePipelines();
        createGraphicsPipeline();
        createParticlePipeline();
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
        if (m_context->getDevice()) {
            vkDeviceWaitIdle(m_context->getDevice());
            cleanupCaptureResources();
            cleanupParticlePipeline();
            cleanupShadowResources();
            cleanupOffscreenTarget();

#ifdef USE_GLFW
            if (!m_context->isHeadless()) {
                ImGui_ImplVulkan_Shutdown();
                ImGui_ImplGlfw_Shutdown();
                ImGui::DestroyContext();

                vkDestroySemaphore(m_context->getDevice(), m_imageAvailableSemaphore, nullptr);
                vkDestroySemaphore(m_context->getDevice(), m_renderFinishedSemaphore, nullptr);
                vkDestroyFence(m_context->getDevice(), m_inFlightFence, nullptr);
            }
#endif

            if (m_graphicsPipeline) {
                vkDestroyPipeline(m_context->getDevice(), m_graphicsPipeline, nullptr);
                vkDestroyPipelineLayout(m_context->getDevice(), m_pipelineLayout, nullptr);
            }

            if (m_viewportSampler) {
                vkDestroySampler(m_context->getDevice(), m_viewportSampler, nullptr);
                m_viewportSampler = VK_NULL_HANDLE;
            }
            vkFreeCommandBuffers(m_context->getDevice(), m_context->getCommandPool(), 1, &m_commandBuffer);
        }
    }

    void VulkanRenderer::cleanupOffscreenTarget() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice device = m_context->getDevice();
        VmaAllocator allocator = m_context->getAllocator();

#ifdef USE_GLFW
        if (m_viewportDescriptor) {
            ImGui_ImplVulkan_RemoveTexture(m_viewportDescriptor);
            m_viewportDescriptor = VK_NULL_HANDLE;
        }
#endif
        if (m_depthImageView) {
            vkDestroyImageView(device, m_depthImageView, nullptr);
            m_depthImageView = VK_NULL_HANDLE;
        }
        if (m_depthImage) {
            vmaDestroyImage(allocator, m_depthImage, m_depthAllocation);
            m_depthImage = VK_NULL_HANDLE;
            m_depthAllocation = VK_NULL_HANDLE;
        }
        if (m_objectIdImageView) {
            vkDestroyImageView(device, m_objectIdImageView, nullptr);
            m_objectIdImageView = VK_NULL_HANDLE;
        }
        if (m_objectIdImage) {
            vmaDestroyImage(allocator, m_objectIdImage, m_objectIdAllocation);
            m_objectIdImage = VK_NULL_HANDLE;
            m_objectIdAllocation = VK_NULL_HANDLE;
        }
        if (m_colorImageView) {
            vkDestroyImageView(device, m_colorImageView, nullptr);
            m_colorImageView = VK_NULL_HANDLE;
        }
        if (m_colorImage) {
            vmaDestroyImage(allocator, m_colorImage, m_colorAllocation);
            m_colorImage = VK_NULL_HANDLE;
            m_colorAllocation = VK_NULL_HANDLE;
        }
    }

    void VulkanRenderer::setRenderResolution(uint32_t width, uint32_t height) {
        if (width < 64) width = 64;
        if (height < 64) height = 64;
        if (width > 7680) width = 7680;
        if (height > 4320) height = 4320;

        if (m_width == width && m_height == height) return;

        if (m_context && m_context->getDevice()) {
            vkDeviceWaitIdle(m_context->getDevice());
        }

        cleanupCaptureResources();
        cleanupOffscreenTarget();

        m_width = width;
        m_height = height;

        createOffscreenTarget();
        initCaptureResources();

#ifdef USE_GLFW
        if (!m_context->isHeadless() && m_viewportSampler && m_colorImageView) {
            m_viewportDescriptor = ImGui_ImplVulkan_AddTexture(m_viewportSampler, m_colorImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
#endif
        core::Logger::info("Render resolution updated to " + std::to_string(m_width) + "x" + std::to_string(m_height));
    }

    void VulkanRenderer::setResolutionScale(float scale) {
        m_resolutionScale = std::clamp(scale, 0.25f, 2.5f);
        uint32_t targetW = static_cast<uint32_t>(static_cast<float>(m_baseWidth) * m_resolutionScale);
        uint32_t targetH = static_cast<uint32_t>(static_cast<float>(m_baseHeight) * m_resolutionScale);
        targetW = (targetW + 1) & ~1u;
        targetH = (targetH + 1) & ~1u;
        setRenderResolution(targetW, targetH);
    }

    void VulkanRenderer::createOffscreenTarget() {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = m_width;
        imageInfo.extent.height = m_height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        if (vmaCreateImage(m_context->getAllocator(), &imageInfo, &allocInfo, &m_colorImage, &m_colorAllocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("failed to create offscreen image!");
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_colorImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_context->getDevice(), &viewInfo, nullptr, &m_colorImageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create texture image view!");
        }

        // --- Semantic Object ID Buffer (R32_UINT) ---
        VkImageCreateInfo idImageInfo = imageInfo;
        idImageInfo.format = VK_FORMAT_R32_UINT;

        if (vmaCreateImage(m_context->getAllocator(), &idImageInfo, &allocInfo, &m_objectIdImage, &m_objectIdAllocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("failed to create object ID offscreen image!");
        }

        VkImageViewCreateInfo idViewInfo = viewInfo;
        idViewInfo.image = m_objectIdImage;
        idViewInfo.format = VK_FORMAT_R32_UINT;

        if (vkCreateImageView(m_context->getDevice(), &idViewInfo, nullptr, &m_objectIdImageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create object ID image view!");
        }

        // --- Depth Buffer ---
        VkImageCreateInfo depthImageInfo = imageInfo;
        depthImageInfo.format = VK_FORMAT_D32_SFLOAT;
        depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (vmaCreateImage(m_context->getAllocator(), &depthImageInfo, &allocInfo, &m_depthImage, &m_depthAllocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("failed to create depth image!");
        }
        VkImageViewCreateInfo depthViewInfo = viewInfo;
        depthViewInfo.image = m_depthImage;
        depthViewInfo.format = VK_FORMAT_D32_SFLOAT;
        depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (vkCreateImageView(m_context->getDevice(), &depthViewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create depth image view!");
        }
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

    void VulkanRenderer::createShadowResources() {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = m_shadowWidth;
        imageInfo.extent.height = m_shadowHeight;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
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
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_D32_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_context->getDevice(), &viewInfo, nullptr, &m_shadowImageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shadow depth image view!");
        }

        // Exact raw depth sampling without pre-comparison linear depth distortion
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

        if (vkCreateSampler(m_context->getDevice(), &samplerInfo, nullptr, &m_shadowSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shadow sampler!");
        }
    }

    void VulkanRenderer::createDescriptorSets() {
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

        std::vector<VkDescriptorSetLayoutBinding> bindings(2);
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_context->getDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout!");
        }

        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 }
        };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 4;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        if (vkCreateDescriptorPool(m_context->getDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool!");
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;

        if (vkAllocateDescriptorSets(m_context->getDevice(), &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate descriptor set!");
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_shadowImageView;
        imageInfo.sampler = m_shadowSampler;

        VkDescriptorBufferInfo uboDescInfo{};
        uboDescInfo.buffer = m_sceneUboBuffer;
        uboDescInfo.offset = 0;
        uboDescInfo.range = sizeof(SceneUboData);

        std::vector<VkWriteDescriptorSet> descriptorWrites(2);
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_descriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &imageInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = m_descriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &uboDescInfo;

        vkUpdateDescriptorSets(m_context->getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    void VulkanRenderer::createShadowPipeline() {
        auto vertCode = utils::readFile("shadow_depth.vert.spv");
        auto fragCode = utils::readFile("shadow_depth.frag.spv");

        auto createShaderModule = [&](const std::vector<char>& code) {
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = code.size();
            createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
            VkShaderModule shaderModule;
            if (vkCreateShaderModule(m_context->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                throw std::runtime_error("failed to create shader module!");
            }
            return shaderModule;
        };

        VkShaderModule vertModule = createShaderModule(vertCode);
        VkShaderModule fragModule = createShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        auto bindingDescriptions = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
        vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 1.1f;
        rasterizer.depthBiasSlopeFactor = 1.5f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        struct ShadowPushConstants {
            glm::mat4 lightSpaceMvp;
        };

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(ShadowPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(m_context->getDevice(), &layoutInfo, nullptr, &m_shadowPipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shadow pipeline layout!");
        }

        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 0;
        renderingInfo.depthAttachmentFormat = depthFormat;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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

        if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_shadowPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shadow pipeline!");
        }

        vkDestroyShaderModule(m_context->getDevice(), fragModule, nullptr);
        vkDestroyShaderModule(m_context->getDevice(), vertModule, nullptr);
    }

    void VulkanRenderer::cleanupShadowResources() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice device = m_context->getDevice();

        if (m_shadowPipeline) {
            vkDestroyPipeline(device, m_shadowPipeline, nullptr);
            m_shadowPipeline = VK_NULL_HANDLE;
        }
        if (m_shadowPipelineLayout) {
            vkDestroyPipelineLayout(device, m_shadowPipelineLayout, nullptr);
            m_shadowPipelineLayout = VK_NULL_HANDLE;
        }
        if (m_descriptorPool) {
            vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
            m_descriptorPool = VK_NULL_HANDLE;
        }
        if (m_descriptorSetLayout) {
            vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
            m_descriptorSetLayout = VK_NULL_HANDLE;
        }
        if (m_shadowSampler) {
            vkDestroySampler(device, m_shadowSampler, nullptr);
            m_shadowSampler = VK_NULL_HANDLE;
        }
        if (m_shadowImageView) {
            vkDestroyImageView(device, m_shadowImageView, nullptr);
            m_shadowImageView = VK_NULL_HANDLE;
        }
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
        auto vertShaderCode = utils::readFile("pbr.vert.spv");
        auto fragShaderCode = utils::readFile("pbr.frag.spv");

        auto createShaderModule = [&](const std::vector<char>& code) {
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = code.size();
            createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
            VkShaderModule shaderModule;
            if (vkCreateShaderModule(m_context->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                throw std::runtime_error("failed to create shader module!");
            }
            return shaderModule;
        };

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        auto bindingDescriptions = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
        vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        struct PushConstants {
            glm::mat4 mvp;
            glm::mat4 model;
            glm::mat4 lightSpaceMatrix;
            glm::vec4 baseColorFactor;
            glm::vec4 cameraPos;
            glm::vec4 materialParams;
        };

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(m_context->getDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }

        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;
        renderingInfo.depthAttachmentFormat = depthFormat;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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
        pipelineInfo.renderPass = VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create graphics pipeline!");
        }

        vkDestroyShaderModule(m_context->getDevice(), fragShaderModule, nullptr);
        vkDestroyShaderModule(m_context->getDevice(), vertShaderModule, nullptr);
    }

    void VulkanRenderer::createParticlePipeline() {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(ParticleVertex) * MAX_PARTICLES;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo vmaAllocInfo{};
        if (vmaCreateBuffer(m_context->getAllocator(), &bufferInfo, &allocInfo, &m_particleVertexBuffer, &m_particleVertexAlloc, &vmaAllocInfo) != VK_SUCCESS) {
            std::cerr << "[Renderer] Warning: Failed to create particle vertex buffer" << std::endl;
            return;
        }
        m_particleMappedPtr = vmaAllocInfo.pMappedData;

        std::vector<char> vertShaderCode;
        std::vector<char> fragShaderCode;
        try {
            vertShaderCode = utils::readFile("particle.vert.spv");
            fragShaderCode = utils::readFile("particle.frag.spv");
        } catch (const std::exception& e) {
            std::cerr << "[Renderer] Warning: Could not load particle shaders: " << e.what() << std::endl;
            return;
        }

        auto createShaderModule = [&](const std::vector<char>& code) {
            VkShaderModuleCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            ci.codeSize = code.size();
            ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
            VkShaderModule mod{VK_NULL_HANDLE};
            vkCreateShaderModule(m_context->getDevice(), &ci, nullptr, &mod);
            return mod;
        };

        VkShaderModule vertMod = createShaderModule(vertShaderCode);
        VkShaderModule fragMod = createShaderModule(fragShaderCode);
        if (!vertMod || !fragMod) {
            if (vertMod) vkDestroyShaderModule(m_context->getDevice(), vertMod, nullptr);
            if (fragMod) vkDestroyShaderModule(m_context->getDevice(), fragMod, nullptr);
            return;
        }

        VkPipelineShaderStageCreateInfo shaderStages[2]{};
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = vertMod;
        shaderStages[0].pName = "main";

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragMod;
        shaderStages[1].pName = "main";

        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = 0;
        bindingDesc.stride = sizeof(ParticleVertex);
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> attrDescs{};
        attrDescs[0].location = 0;
        attrDescs[0].binding = 0;
        attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrDescs[0].offset = offsetof(ParticleVertex, pos);

        attrDescs[1].location = 1;
        attrDescs[1].binding = 0;
        attrDescs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrDescs[1].offset = offsetof(ParticleVertex, color);

        attrDescs[2].location = 2;
        attrDescs[2].binding = 0;
        attrDescs[2].format = VK_FORMAT_R32_SFLOAT;
        attrDescs[2].offset = offsetof(ParticleVertex, size);

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
        vertexInputInfo.pVertexAttributeDescriptions = attrDescs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
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

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlend;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(glm::mat4);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pcr;

        if (vkCreatePipelineLayout(m_context->getDevice(), &layoutInfo, nullptr, &m_particlePipelineLayout) != VK_SUCCESS) {
            std::cerr << "[Renderer] Failed to create particle pipeline layout" << std::endl;
            vkDestroyShaderModule(m_context->getDevice(), fragMod, nullptr);
            vkDestroyShaderModule(m_context->getDevice(), vertMod, nullptr);
            return;
        }

        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;
        renderingInfo.depthAttachmentFormat = depthFormat;

        VkGraphicsPipelineCreateInfo pipeInfo{};
        pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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
        pipeInfo.renderPass = VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_particlePipeline) != VK_SUCCESS) {
            std::cerr << "[Renderer] Failed to create particle graphics pipeline" << std::endl;
        }

        vkDestroyShaderModule(m_context->getDevice(), fragMod, nullptr);
        vkDestroyShaderModule(m_context->getDevice(), vertMod, nullptr);
    }

    void VulkanRenderer::cleanupParticlePipeline() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice dev = m_context->getDevice();
        if (m_particlePipeline) {
            vkDestroyPipeline(dev, m_particlePipeline, nullptr);
            m_particlePipeline = VK_NULL_HANDLE;
        }
        if (m_particlePipelineLayout) {
            vkDestroyPipelineLayout(dev, m_particlePipelineLayout, nullptr);
            m_particlePipelineLayout = VK_NULL_HANDLE;
        }
        if (m_particleVertexBuffer) {
            vmaDestroyBuffer(m_context->getAllocator(), m_particleVertexBuffer, m_particleVertexAlloc);
            m_particleVertexBuffer = VK_NULL_HANDLE;
            m_particleVertexAlloc = VK_NULL_HANDLE;
            m_particleMappedPtr = nullptr;
        }
    }

    void VulkanRenderer::render(ecs::Registry& registry, core::AssetManager* assetManager, glm::mat4 viewMatrix, core::Engine* engine) {
#ifdef USE_GLFW
        uint32_t imageIndex = 0;
        if (!m_context->isHeadless()) {
            vkWaitForFences(m_context->getDevice(), 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);

            VkResult result = vkAcquireNextImageKHR(m_context->getDevice(), m_context->getSwapchain(), UINT64_MAX, m_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
                m_context->recreateSwapchain();
                return;
            } else if (result != VK_SUCCESS) {
                if (result == VK_ERROR_SURFACE_LOST_KHR) {
                    m_context->recreateSwapchain();
                } else {
                    core::Logger::warn("Acquire swapchain image non-fatal error: " + std::to_string(result));
                }
                return;
            }

            vkResetFences(m_context->getDevice(), 1, &m_inFlightFence);
        }
#endif

        vkResetCommandBuffer(m_commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

        // =========================================================================
        // PASS 1: DIRECTIONAL SHADOW PASS (Matched to App.tsx Sun [25, 50, 15])
        // =========================================================================
        glm::vec3 sunDir = glm::normalize(glm::vec3(25.0f, 50.0f, 15.0f));
        glm::vec3 camPos = engine ? engine->getCameraPosition() : glm::vec3(glm::inverse(viewMatrix)[3]);

        glm::vec3 lightTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        if (engine) {
            float yaw = glm::radians(engine->getCameraYaw());
            float pitch = glm::radians(engine->getCameraPitch());
            glm::vec3 front = glm::normalize(glm::vec3(
                cos(yaw) * cos(pitch),
                sin(pitch),
                sin(yaw) * cos(pitch)
            ));
            if (std::abs(front.y) > 0.05f) {
                float t = -camPos.y / front.y;
                if (t > 0.0f && t < 80.0f) {
                    lightTarget = camPos + front * t;
                }
            }
        }
        lightTarget.y = 0.0f;

        float orthoExtent = 24.0f;
        float worldUnitsPerTexel = (2.0f * orthoExtent) / static_cast<float>(m_shadowWidth);
        lightTarget.x = std::round(lightTarget.x / worldUnitsPerTexel) * worldUnitsPerTexel;
        lightTarget.z = std::round(lightTarget.z / worldUnitsPerTexel) * worldUnitsPerTexel;

        glm::vec3 lightPos = lightTarget + sunDir * 60.0f;
        glm::mat4 lightView = glm::lookAt(lightPos, lightTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightProj = glm::ortho(-orthoExtent, orthoExtent, -orthoExtent, orthoExtent, 1.0f, 120.0f);
        lightProj[1][1] *= -1.0f;
        glm::mat4 lightSpaceMatrix = lightProj * lightView;

        if (m_shadowPipeline && m_shadowImage) {
            VkImageMemoryBarrier2 shadowBarrierInit{};
            shadowBarrierInit.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            shadowBarrierInit.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            shadowBarrierInit.srcAccessMask = 0;
            shadowBarrierInit.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            shadowBarrierInit.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            shadowBarrierInit.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            shadowBarrierInit.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            shadowBarrierInit.image = m_shadowImage;
            shadowBarrierInit.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            shadowBarrierInit.subresourceRange.baseMipLevel = 0;
            shadowBarrierInit.subresourceRange.levelCount = 1;
            shadowBarrierInit.subresourceRange.baseArrayLayer = 0;
            shadowBarrierInit.subresourceRange.layerCount = 1;

            VkDependencyInfo shadowDepInfo{};
            shadowDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            shadowDepInfo.imageMemoryBarrierCount = 1;
            shadowDepInfo.pImageMemoryBarriers = &shadowBarrierInit;
            vkCmdPipelineBarrier2(m_commandBuffer, &shadowDepInfo);

            VkRenderingAttachmentInfo shadowDepthAttachment{};
            shadowDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            shadowDepthAttachment.imageView = m_shadowImageView;
            shadowDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            shadowDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            shadowDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            shadowDepthAttachment.clearValue.depthStencil = {1.0f, 0};

            VkRenderingInfo shadowRenderingInfo{};
            shadowRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            shadowRenderingInfo.renderArea.offset = {0, 0};
            shadowRenderingInfo.renderArea.extent = {m_shadowWidth, m_shadowHeight};
            shadowRenderingInfo.layerCount = 1;
            shadowRenderingInfo.pDepthAttachment = &shadowDepthAttachment;

            vkCmdBeginRendering(m_commandBuffer, &shadowRenderingInfo);

            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
            VkViewport shadowVp{0.0f, 0.0f, static_cast<float>(m_shadowWidth), static_cast<float>(m_shadowHeight), 0.0f, 1.0f};
            vkCmdSetViewport(m_commandBuffer, 0, 1, &shadowVp);
            VkRect2D shadowScissor{{0, 0}, {m_shadowWidth, m_shadowHeight}};
            vkCmdSetScissor(m_commandBuffer, 0, 1, &shadowScissor);

            auto shadowView = registry.get().view<ecs::TransformComponent, ecs::MeshComponent>();
            for (auto entity : shadowView) {
                const auto& transform = shadowView.get<ecs::TransformComponent>(entity);
                const auto& meshComp = shadowView.get<ecs::MeshComponent>(entity);

                if (meshComp.meshId == 1) continue;
                if (registry.get().all_of<ecs::RigidBodyComponent>(entity) &&
                    registry.get().get<ecs::RigidBodyComponent>(entity).isSensor) {
                    continue;
                }

                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, transform.position);
                model *= glm::mat4_cast(transform.orientation);
                model = glm::scale(model, transform.scale);

                struct ShadowPushConstants {
                    glm::mat4 lightSpaceMvp;
                } shadowPcs;
                shadowPcs.lightSpaceMvp = lightSpaceMatrix * model;

                vkCmdPushConstants(m_commandBuffer, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &shadowPcs);

                if (assetManager) {
                    auto* meshAsset = assetManager->getMesh(meshComp.meshId);
                    if (meshAsset && meshAsset->vertexBuffer && meshAsset->indexBuffer) {
                        VkDeviceSize offset = 0;
                        vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &meshAsset->vertexBuffer, &offset);
                        vkCmdBindIndexBuffer(m_commandBuffer, meshAsset->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(m_commandBuffer, meshAsset->indexCount, 1, 0, 0, 0);
                    }
                }
            }

            vkCmdEndRendering(m_commandBuffer);

            VkImageMemoryBarrier2 shadowToSampleBarrier{};
            shadowToSampleBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            shadowToSampleBarrier.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            shadowToSampleBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            shadowToSampleBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            shadowToSampleBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            shadowToSampleBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            shadowToSampleBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            shadowToSampleBarrier.image = m_shadowImage;
            shadowToSampleBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            shadowToSampleBarrier.subresourceRange.baseMipLevel = 0;
            shadowToSampleBarrier.subresourceRange.levelCount = 1;
            shadowToSampleBarrier.subresourceRange.baseArrayLayer = 0;
            shadowToSampleBarrier.subresourceRange.layerCount = 1;

            VkDependencyInfo shadowReadDepInfo{};
            shadowReadDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            shadowReadDepInfo.imageMemoryBarrierCount = 1;
            shadowReadDepInfo.pImageMemoryBarriers = &shadowToSampleBarrier;
            vkCmdPipelineBarrier2(m_commandBuffer, &shadowReadDepInfo);
        }

        // =========================================================================
        // PASS 2: CLEAN UNFILTERED 3D RENDER PASS (Matched 1:1 to App.tsx)
        // =========================================================================
        VkImageMemoryBarrier2 colorBarrier{};
        colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        colorBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        colorBarrier.srcAccessMask = 0;
        colorBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        colorBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        colorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorBarrier.image = m_colorImage;
        colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorBarrier.subresourceRange.baseMipLevel = 0;
        colorBarrier.subresourceRange.levelCount = 1;
        colorBarrier.subresourceRange.baseArrayLayer = 0;
        colorBarrier.subresourceRange.layerCount = 1;

        VkImageMemoryBarrier2 depthBarrierInit{};
        depthBarrierInit.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        depthBarrierInit.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        depthBarrierInit.srcAccessMask = 0;
        depthBarrierInit.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        depthBarrierInit.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthBarrierInit.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthBarrierInit.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthBarrierInit.image = m_depthImage;
        depthBarrierInit.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthBarrierInit.subresourceRange.baseMipLevel = 0;
        depthBarrierInit.subresourceRange.levelCount = 1;
        depthBarrierInit.subresourceRange.baseArrayLayer = 0;
        depthBarrierInit.subresourceRange.layerCount = 1;

        VkImageMemoryBarrier2 initBarriers[] = {colorBarrier, depthBarrierInit};
        VkDependencyInfo initDepInfo{};
        initDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        initDepInfo.imageMemoryBarrierCount = 2;
        initDepInfo.pImageMemoryBarriers = initBarriers;
        vkCmdPipelineBarrier2(m_commandBuffer, &initDepInfo);

        // App.tsx exact background color: 0x0a0a14 -> RGB(10, 10, 20)
        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = m_colorImageView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = { {0.039f, 0.039f, 0.078f, 1.0f} };

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = m_depthImageView;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea.offset = {0, 0};
        renderInfo.renderArea.extent = {m_width, m_height};
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;
        renderInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(m_commandBuffer, &renderInfo);

        if (m_graphicsPipeline) {
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

            // Pure, unfiltered lighting matching App.tsx (no fog / haze filters)
            if (m_sceneUboMappedPtr) {
                SceneUboData ubo{};
                ubo.sunDirection = glm::vec4(sunDir, 2.0f);
                ubo.sunColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                ubo.ambientColor = glm::vec4(0.32f, 0.34f, 0.44f, 0.90f);
                ubo.fogParams = glm::vec4(0.0f, 1000.0f, 2000.0f, 0.0f); // Fog filter strictly disabled
                ubo.fogColor = glm::vec4(0.039f, 0.039f, 0.078f, 1.0f);

                auto lightView = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
                int lightIdx = 0;
                for (auto lEntity : lightView) {
                    if (lightIdx >= 8) break;
                    const auto& lt = lightView.get<ecs::TransformComponent>(lEntity);
                    const auto& lc = lightView.get<ecs::LightComponent>(lEntity);
                    if (lc.type == "point" || lc.type.empty()) {
                        ubo.pointLightsPos[lightIdx] = glm::vec4(lt.position, lc.range);
                        ubo.pointLightsCol[lightIdx] = glm::vec4(lc.color, lc.intensity);
                        lightIdx++;
                    }
                }
                ubo.numPointLights = lightIdx;
                std::memcpy(m_sceneUboMappedPtr, &ubo, sizeof(SceneUboData));
            }

            if (m_descriptorSet) {
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
            }

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(m_width);
            viewport.height = static_cast<float>(m_height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = {m_width, m_height};
            vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);

            struct PushConstants {
                glm::mat4 mvp;
                glm::mat4 model;
                glm::mat4 lightSpaceMatrix;
                glm::vec4 baseColorFactor;
                glm::vec4 cameraPos;
                glm::vec4 materialParams;
            } pcs;

            pcs.lightSpaceMatrix = lightSpaceMatrix;
            pcs.cameraPos = glm::vec4(camPos, 0.0f);

#ifdef USE_GLFW
            float aspect = (m_standaloneGameMode || m_viewportAspectRatio <= 0.1f) ? (viewport.width / viewport.height) : m_viewportAspectRatio;
#else
            float aspect = viewport.width / viewport.height;
#endif
            float fovDeg = engine ? engine->getCameraFov() : 45.0f;
            if (fovDeg < 10.0f || fovDeg > 140.0f) fovDeg = 45.0f;
            glm::mat4 projection = glm::perspective(glm::radians(fovDeg), aspect, 0.1f, 1000.0f);
            projection[1][1] *= -1.0f;
            glm::mat4 vp = projection * viewMatrix;

            auto view = registry.get().view<ecs::TransformComponent, ecs::MeshComponent>();
            for (auto entity : view) {
                const auto& transform = view.get<ecs::TransformComponent>(entity);
                const auto& meshComp = view.get<ecs::MeshComponent>(entity);

                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, transform.position);
                model *= glm::mat4_cast(transform.orientation);
                model = glm::scale(model, transform.scale);

                std::string tag = "";
                if (registry.get().all_of<ecs::TagComponent>(entity)) {
                    tag = registry.get().get<ecs::TagComponent>(entity).name;
                }
                bool isPlane = (meshComp.meshId == 1 || tag == "GroundPlane" || tag == "Ground");

                if (isPlane) {
                    if (meshComp.meshId == 1) {
                        model = glm::scale(model, glm::vec3(100.0f, 4.0f, 100.0f));
                    }
                    // App.tsx slate floor (0x334155) with clean matte finish
                    pcs.baseColorFactor = glm::vec4(0.16f, 0.19f, 0.25f, 1.0f);
                    pcs.materialParams = glm::vec4(0.08f, 0.75f, 1.0f, 0.0f);
                } else if (registry.get().all_of<ecs::MaterialComponent>(entity)) {
                    const auto& mat = registry.get().get<ecs::MaterialComponent>(entity);
                    pcs.baseColorFactor = mat.baseColor;
                    pcs.materialParams = glm::vec4(mat.metallic, mat.roughness, 0.0f, mat.emissiveIntensity);
                } else {
                    // Exact App.tsx color & material assignment rules
                    bool isCapsule = false;
                    bool isSensor = false;
                    if (registry.get().all_of<ecs::RigidBodyComponent>(entity)) {
                        const auto& rb = registry.get().get<ecs::RigidBodyComponent>(entity);
                        isCapsule = (rb.shapeType == 2);
                        isSensor = rb.isSensor;
                    }
                    bool isLight = registry.get().all_of<ecs::LightComponent>(entity) ||
                                   (tag.find("Light") != std::string::npos) || (tag.find("light") != std::string::npos);

                    glm::vec3 col(0.22f, 0.74f, 0.97f);
                    float emissive = 0.0f;

                    if (isLight || tag == "Sun" || tag == "Coin" || tag == "CoinTrigger") {
                        col = glm::vec3(0.980f, 0.804f, 0.082f); // 0xfacc15
                        emissive = 0.5f;
                    } else if (tag == "Earth" || tag.find("Player") != std::string::npos || isCapsule) {
                        col = glm::vec3(0.000f, 0.941f, 1.000f); // 0x00f0ff
                        emissive = 0.4f;
                    } else if (tag == "Paddle") {
                        col = glm::vec3(0.063f, 0.725f, 0.506f); // 0x10b981
                    } else if (tag == "Ball") {
                        col = glm::vec3(0.957f, 0.247f, 0.369f); // 0xf43f5e
                        emissive = 0.45f;
                    } else if (tag == "Brick") {
                        col = glm::vec3(0.659f, 0.333f, 0.969f); // 0xa855f7
                    } else if (tag == "LaserBullet") {
                        col = glm::vec3(0.000f, 1.000f, 1.000f); // 0x00ffff
                        emissive = 0.6f;
                    } else if (tag.find("KillZ") != std::string::npos) {
                        col = glm::vec3(0.937f, 0.267f, 0.267f); // 0xef4444
                    } else if (tag == "Checkpoint" || tag == "SpeedBoost") {
                        col = glm::vec3(0.133f, 0.773f, 0.369f); // 0x22c55e
                    } else if (isSensor) {
                        col = glm::vec3(0.918f, 0.702f, 0.031f); // 0xeab308
                    } else if (tag.find("Enemy") != std::string::npos || tag.find("Meteor") != std::string::npos || tag == "Mars") {
                        col = glm::vec3(0.973f, 0.443f, 0.443f); // 0xf87171
                    } else {
                        // App.tsx exact entity ID hash color formula:
                        // r = 0.2 + 0.8 * ((ent.id * 31) % 100) / 100
                        // g = 0.2 + 0.8 * ((ent.id * 53) % 100) / 100
                        // b = 0.2 + 0.8 * ((ent.id * 97) % 100) / 100
                        uint32_t eId = static_cast<uint32_t>(entity);
                        float r = 0.20f + 0.80f * static_cast<float>((eId * 31u) % 100u) / 100.0f;
                        float g = 0.20f + 0.80f * static_cast<float>((eId * 53u) % 100u) / 100.0f;
                        float b = 0.20f + 0.80f * static_cast<float>((eId * 97u) % 100u) / 100.0f;
                        col = glm::vec3(r, g, b);
                    }

                    pcs.baseColorFactor = glm::vec4(col, 1.0f);
                    pcs.materialParams = glm::vec4(0.18f, 0.42f, 0.0f, emissive);
                }

                pcs.mvp = vp * model;
                pcs.model = model;

                vkCmdPushConstants(m_commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pcs);

                if (assetManager) {
                    auto* meshAsset = assetManager->getMesh(meshComp.meshId);
                    if (meshAsset && meshAsset->vertexBuffer && meshAsset->indexBuffer) {
                        VkDeviceSize offset = 0;
                        vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &meshAsset->vertexBuffer, &offset);
                        vkCmdBindIndexBuffer(m_commandBuffer, meshAsset->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(m_commandBuffer, meshAsset->indexCount, 1, 0, 0, 0);
                    }
                }
            }
        }

        // Render Particles
        if (m_particlePipeline && m_particleMappedPtr) {
            std::vector<ParticleVertex> particles;
            particles.reserve(2048);

            static auto particleClockStart = std::chrono::steady_clock::now();
            float curTime = std::chrono::duration<float>(std::chrono::steady_clock::now() - particleClockStart).count();

            auto particleView = registry.get().view<ecs::TransformComponent, ecs::ParticleEmitterComponent>();
            for (auto entity : particleView) {
                const auto& transform = particleView.get<ecs::TransformComponent>(entity);
                const auto& emitter = particleView.get<ecs::ParticleEmitterComponent>(entity);

                uint32_t numParticles = std::min(emitter.count, 500u);
                glm::vec3 basePos = transform.position;
                glm::vec3 emitColor = emitter.color;
                float emitSpeed = emitter.speed > 0.01f ? emitter.speed : 3.0f;
                float emitSize = emitter.size > 0.01f ? emitter.size : 0.4f;

                for (uint32_t i = 0; i < numParticles; ++i) {
                    if (particles.size() >= MAX_PARTICLES) break;

                    float seed = static_cast<float>((i * 1337) % 1000) / 1000.0f;
                    float seed2 = static_cast<float>((i * 7919) % 1000) / 1000.0f;
                    float seed3 = static_cast<float>((i * 4973) % 1000) / 1000.0f;
                    float lifetime = 1.0f + seed * 1.5f;
                    float age = fmod(curTime * emitSpeed + seed * lifetime, lifetime);
                    float progress = age / lifetime;

                    ParticleVertex pv{};
                    if (emitter.type == "sparks") {
                        float phi = seed2 * 6.283185f;
                        float theta = seed3 * 3.14159f;
                        glm::vec3 dir(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));
                        pv.pos = basePos + dir * (age * emitSpeed) + glm::vec3(0.0f, -4.9f * age * age, 0.0f);
                        float alpha = (1.0f - progress);
                        pv.color = glm::vec4(emitColor * 1.5f, alpha);
                        pv.size = emitSize * (1.0f - progress * 0.5f);
                    } else if (emitter.type == "smoke") {
                        glm::vec3 dir(sin(seed2 * 6.28f) * 0.5f, 1.0f, cos(seed3 * 6.28f) * 0.5f);
                        pv.pos = basePos + dir * (age * emitSpeed * 0.6f);
                        float alpha = sin(progress * 3.14159f) * 0.35f;
                        pv.color = glm::vec4(emitColor * 0.6f, alpha);
                        pv.size = emitSize * (1.0f + progress * 2.5f);
                    } else if (emitter.type == "portal") {
                        float angle = curTime * emitSpeed * 2.0f + seed * 6.283185f;
                        float radius = 0.8f + 0.3f * sin(angle * 2.0f);
                        pv.pos = basePos + glm::vec3(cos(angle) * radius, (seed2 - 0.5f) * 0.5f + sin(angle * 3.0f) * 0.2f, sin(angle) * radius);
                        pv.color = glm::vec4(emitColor, 0.8f);
                        pv.size = emitSize * (0.8f + 0.4f * sin(curTime * 5.0f + seed));
                    } else if (emitter.type == "explosion") {
                        float phi = seed2 * 6.283185f;
                        float theta = seed3 * 3.14159f;
                        glm::vec3 dir(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));
                        pv.pos = basePos + dir * (progress * emitSpeed * 2.5f);
                        float alpha = std::max(0.0f, 1.0f - progress);
                        glm::vec3 c = glm::mix(emitColor * 2.0f, glm::vec3(0.2f, 0.05f, 0.0f), progress);
                        pv.color = glm::vec4(c, alpha);
                        pv.size = emitSize * (1.5f - progress);
                    } else {
                        float phi = seed2 * 6.283185f;
                        float r = (1.0f - progress) * 0.3f;
                        pv.pos = basePos + glm::vec3(cos(phi) * r, age * emitSpeed, sin(phi) * r);
                        glm::vec3 c = glm::mix(emitColor * 1.8f, glm::vec3(0.8f, 0.1f, 0.0f), progress);
                        float alpha = (1.0f - progress);
                        pv.color = glm::vec4(c, alpha);
                        pv.size = emitSize * (1.0f - progress * 0.6f);
                    }
                    particles.push_back(pv);
                }
            }

            if (!particles.empty()) {
                std::memcpy(m_particleMappedPtr, particles.data(), particles.size() * sizeof(ParticleVertex));
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_particlePipeline);

                float pAspect = static_cast<float>(m_width) / static_cast<float>(m_height);
#ifdef USE_GLFW
                if (!m_standaloneGameMode && m_viewportAspectRatio > 0.1f) {
                    pAspect = m_viewportAspectRatio;
                }
#endif
                float fovDeg = engine ? engine->getCameraFov() : 45.0f;
                if (fovDeg < 10.0f || fovDeg > 140.0f) fovDeg = 45.0f;
                glm::mat4 pProj = glm::perspective(glm::radians(fovDeg), pAspect, 0.1f, 1000.0f);
                pProj[1][1] *= -1.0f;
                glm::mat4 pVp = pProj * viewMatrix;

                vkCmdPushConstants(m_commandBuffer, m_particlePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &pVp);

                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &m_particleVertexBuffer, &offset);
                vkCmdDraw(m_commandBuffer, static_cast<uint32_t>(particles.size()), 1, 0, 0);
            }
        }
        vkCmdEndRendering(m_commandBuffer);

#ifdef USE_GLFW
        if (!m_context->isHeadless()) {
            VkImage swapchainImg = m_context->getSwapchainImages()[imageIndex];

            // Direct Swapchain Blit: copy full 3D scene from m_colorImage to swapchainImg
            VkImageMemoryBarrier2 imgBarrierDst{};
            imgBarrierDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            imgBarrierDst.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            imgBarrierDst.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            imgBarrierDst.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            imgBarrierDst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            imgBarrierDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imgBarrierDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imgBarrierDst.image = swapchainImg;
            imgBarrierDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            VkImageMemoryBarrier2 imgBarrierSrc{};
            imgBarrierSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            imgBarrierSrc.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            imgBarrierSrc.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            imgBarrierSrc.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            imgBarrierSrc.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            imgBarrierSrc.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            imgBarrierSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imgBarrierSrc.image = m_colorImage;
            imgBarrierSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            VkImageMemoryBarrier2 copyBarriers[] = {imgBarrierDst, imgBarrierSrc};
            VkDependencyInfo copyDepInfo{};
            copyDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            copyDepInfo.imageMemoryBarrierCount = 2;
            copyDepInfo.pImageMemoryBarriers = copyBarriers;
            vkCmdPipelineBarrier2(m_commandBuffer, &copyDepInfo);

            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {(int32_t)m_width, (int32_t)m_height, 1};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {(int32_t)m_context->getSwapchainExtent().width, (int32_t)m_context->getSwapchainExtent().height, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};

            vkCmdBlitImage(m_commandBuffer,
                m_colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                swapchainImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit, VK_FILTER_LINEAR);

            imgBarrierDst.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            imgBarrierDst.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            imgBarrierDst.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            imgBarrierDst.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
            imgBarrierDst.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imgBarrierDst.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            imgBarrierSrc.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            imgBarrierSrc.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            imgBarrierSrc.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            imgBarrierSrc.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            imgBarrierSrc.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imgBarrierSrc.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkImageMemoryBarrier2 renderBarriers[] = {imgBarrierDst, imgBarrierSrc};
            copyDepInfo.pImageMemoryBarriers = renderBarriers;
            vkCmdPipelineBarrier2(m_commandBuffer, &copyDepInfo);

            renderImGui(m_commandBuffer, m_context->getSwapchainImageViews()[imageIndex], registry, assetManager, engine);

            VkImageMemoryBarrier2 presentBarrier{};
            presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            presentBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            presentBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            presentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            presentBarrier.dstAccessMask = 0;
            presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            presentBarrier.image = swapchainImg;
            presentBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            VkDependencyInfo presentDep{};
            presentDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            presentDep.imageMemoryBarrierCount = 1;
            presentDep.pImageMemoryBarriers = &presentBarrier;
            vkCmdPipelineBarrier2(m_commandBuffer, &presentDep);
        } else {
            VkImageMemoryBarrier2 imgBarrierSrc{};
            imgBarrierSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            imgBarrierSrc.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            imgBarrierSrc.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            imgBarrierSrc.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            imgBarrierSrc.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
            imgBarrierSrc.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            imgBarrierSrc.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imgBarrierSrc.image = m_colorImage;
            imgBarrierSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            VkDependencyInfo headlessDep{};
            headlessDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            headlessDep.imageMemoryBarrierCount = 1;
            headlessDep.pImageMemoryBarriers = &imgBarrierSrc;
            vkCmdPipelineBarrier2(m_commandBuffer, &headlessDep);
        }
#else
        VkImageMemoryBarrier2 imgBarrierSrc{};
        imgBarrierSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imgBarrierSrc.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        imgBarrierSrc.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        imgBarrierSrc.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        imgBarrierSrc.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
        imgBarrierSrc.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imgBarrierSrc.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgBarrierSrc.image = m_colorImage;
        imgBarrierSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo headlessDep{};
        headlessDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        headlessDep.imageMemoryBarrierCount = 1;
        headlessDep.pImageMemoryBarriers = &imgBarrierSrc;
        vkCmdPipelineBarrier2(m_commandBuffer, &headlessDep);
#endif

        vkEndCommandBuffer(m_commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffer;

#ifdef USE_GLFW
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        if (!m_context->isHeadless()) {
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &m_imageAvailableSemaphore;
            submitInfo.pWaitDstStageMask = waitStages;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &m_renderFinishedSemaphore;
            vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, m_inFlightFence);

            VkPresentInfoKHR presentInfo{};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &m_renderFinishedSemaphore;
            VkSwapchainKHR swapChains[] = {m_context->getSwapchain()};
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = swapChains;
            presentInfo.pImageIndices = &imageIndex;
            VkResult result = vkQueuePresentKHR(m_context->getGraphicsQueue(), &presentInfo);

            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
                m_context->recreateSwapchain();
            } else if (result != VK_SUCCESS) {
                if (result == VK_ERROR_SURFACE_LOST_KHR) {
                    m_context->recreateSwapchain();
                } else {
                    core::Logger::warn("Present swapchain image non-fatal error: " + std::to_string(result));
                }
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

    void VulkanRenderer::createComputePipelines() {
        // Reserved for future compute passes
    }

    void VulkanRenderer::initCaptureResources() {
        VkDeviceSize bufferSize = m_width * m_height * 4;
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        VmaAllocationInfo stagingAllocInfo{};
        if (vmaCreateBuffer(m_context->getAllocator(), &bufferInfo, &allocInfo, &m_captureStagingBuffer, &m_captureStagingAlloc, &stagingAllocInfo) == VK_SUCCESS) {
            m_captureMappedPtr = stagingAllocInfo.pMappedData;
        }

        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
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
        size_t out_len = 4 * ((len + 2) / 3);
        std::string ret;
        ret.reserve(out_len);
        for (size_t i = 0; i < len; i += 3) {
            uint32_t b = (static_cast<uint32_t>(data[i]) << 16);
            if (i + 1 < len) b |= (static_cast<uint32_t>(data[i + 1]) << 8);
            if (i + 2 < len) b |= static_cast<uint32_t>(data[i + 2]);
            ret.push_back(s_b64chars[(b >> 18) & 0x3F]);
            ret.push_back(s_b64chars[(b >> 12) & 0x3F]);
            if (i + 1 < len) ret.push_back(s_b64chars[(b >> 6) & 0x3F]);
            else ret.push_back('=');
            if (i + 2 < len) ret.push_back(s_b64chars[b & 0x3F]);
            else ret.push_back('=');
        }
        return ret;
    }

    std::string VulkanRenderer::captureFrameBase64() {
        if (m_width == 0 || m_height == 0) return "";
        if (!m_captureStagingBuffer || !m_captureMappedPtr || !m_captureCommandBuffer) {
            initCaptureResources();
            if (!m_captureStagingBuffer || !m_captureMappedPtr || !m_captureCommandBuffer) {
                return "";
            }
        }

        static std::string s_cachedFrame = "";
        static auto s_lastTime = std::chrono::steady_clock::now();
        auto nowTime = std::chrono::steady_clock::now();
        if (!s_cachedFrame.empty() && std::chrono::duration<float, std::milli>(nowTime - s_lastTime).count() < 14.0f) {
            return s_cachedFrame;
        }

        vkResetCommandBuffer(m_captureCommandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(m_captureCommandBuffer, &beginInfo);

        VkImageMemoryBarrier2 toSrcBarrier{};
        toSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toSrcBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        toSrcBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
        toSrcBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toSrcBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        toSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toSrcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrcBarrier.image = m_colorImage;
        toSrcBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &toSrcBarrier;
        vkCmdPipelineBarrier2(m_captureCommandBuffer, &depInfo);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {m_width, m_height, 1};
        vkCmdCopyImageToBuffer(m_captureCommandBuffer, m_colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_captureStagingBuffer, 1, &region);

        VkImageMemoryBarrier2 backBarrier = toSrcBarrier;
        backBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        backBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        backBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        backBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        backBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        backBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        depInfo.pImageMemoryBarriers = &backBarrier;
        vkCmdPipelineBarrier2(m_captureCommandBuffer, &depInfo);

        vkEndCommandBuffer(m_captureCommandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_captureCommandBuffer;
        vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_context->getGraphicsQueue());

        const uint8_t* srcPixels = static_cast<const uint8_t*>(m_captureMappedPtr);
        std::vector<uint8_t> jpegBytes;
        jpegBytes.reserve(256 * 1024);
        stbi_write_jpg_to_func(writeJpegBuffer, &jpegBytes, static_cast<int>(m_width), static_cast<int>(m_height), 4, srcPixels, 82);

        std::string encoded = encodeBase64Data(jpegBytes.data(), jpegBytes.size());
        s_cachedFrame = "data:image/jpeg;base64," + encoded;
        s_lastTime = std::chrono::steady_clock::now();
        return s_cachedFrame;
    }

    void VulkanRenderer::submitBufferBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size) {
        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
        barrier.buffer = buffer;
        barrier.offset = 0;
        barrier.size = size;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.bufferMemoryBarrierCount = 1;
        depInfo.pBufferMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

#ifdef USE_GLFW
    // =========================================================================
    // THEME & VECTOR ICON HELPERS MATCHING App.tsx (Image B)
    // =========================================================================
    static void applyUnrealWebTheme() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        // High-contrast, modern Linear.app & UE5 theme matching Image 4
        colors[ImGuiCol_WindowBg]             = ImVec4(0.051f, 0.067f, 0.090f, 1.00f); // #0d1117
        colors[ImGuiCol_ChildBg]              = ImVec4(0.086f, 0.106f, 0.133f, 1.00f); // #161b22
        colors[ImGuiCol_PopupBg]              = ImVec4(0.086f, 0.106f, 0.133f, 0.98f); // #161b22
        colors[ImGuiCol_Border]               = ImVec4(0.188f, 0.212f, 0.239f, 1.00f); // #30363d
        colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

        colors[ImGuiCol_TitleBg]              = ImVec4(0.043f, 0.055f, 0.075f, 1.00f);
        colors[ImGuiCol_TitleBgActive]        = ImVec4(0.051f, 0.067f, 0.090f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.043f, 0.055f, 0.075f, 0.75f);
        colors[ImGuiCol_MenuBarBg]            = ImVec4(0.043f, 0.055f, 0.075f, 1.00f); // #0b0e13

        colors[ImGuiCol_Text]                 = ImVec4(0.945f, 0.960f, 0.980f, 1.00f); // High-contrast white (#f1f5f9)
        colors[ImGuiCol_TextDisabled]         = ImVec4(0.580f, 0.635f, 0.720f, 1.00f); // Slate-400 (#94a3b8)

        colors[ImGuiCol_FrameBg]              = ImVec4(0.086f, 0.106f, 0.133f, 1.00f); // #161b22
        colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.129f, 0.149f, 0.176f, 1.00f); // #21262d
        colors[ImGuiCol_FrameBgActive]        = ImVec4(0.188f, 0.212f, 0.239f, 1.00f);

        colors[ImGuiCol_Button]               = ImVec4(0.129f, 0.149f, 0.176f, 1.00f); // #21262d
        colors[ImGuiCol_ButtonHovered]        = ImVec4(0.188f, 0.212f, 0.239f, 1.00f); // #30363d
        colors[ImGuiCol_ButtonActive]         = ImVec4(0.000f, 0.440f, 0.950f, 1.00f); // Electric Blue #0070f3

        colors[ImGuiCol_Header]               = ImVec4(0.000f, 0.440f, 0.950f, 0.50f);
        colors[ImGuiCol_HeaderHovered]        = ImVec4(0.000f, 0.440f, 0.950f, 0.75f);
        colors[ImGuiCol_HeaderActive]         = ImVec4(0.000f, 0.440f, 0.950f, 1.00f);

        colors[ImGuiCol_Tab]                  = ImVec4(0.051f, 0.067f, 0.090f, 1.00f);
        colors[ImGuiCol_TabHovered]           = ImVec4(0.129f, 0.149f, 0.176f, 1.00f);
        colors[ImGuiCol_TabActive]            = ImVec4(0.086f, 0.106f, 0.133f, 1.00f);
        colors[ImGuiCol_TabUnfocused]         = ImVec4(0.043f, 0.055f, 0.075f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.065f, 0.080f, 0.105f, 1.00f);

        colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.051f, 0.067f, 0.090f, 0.50f);
        colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.188f, 0.212f, 0.239f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.250f, 0.280f, 0.320f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.000f, 0.440f, 0.950f, 1.00f);
        colors[ImGuiCol_CheckMark]            = ImVec4(0.000f, 0.850f, 1.000f, 1.00f);
        colors[ImGuiCol_SliderGrab]           = ImVec4(0.000f, 0.440f, 0.950f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.200f, 0.650f, 1.000f, 1.00f);
        colors[ImGuiCol_Separator]            = ImVec4(0.188f, 0.212f, 0.239f, 1.00f);
        colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.000f, 0.440f, 0.950f, 0.78f);
        colors[ImGuiCol_SeparatorActive]      = ImVec4(0.000f, 0.440f, 0.950f, 1.00f);

        style.WindowRounding    = 0.0f;
        style.ChildRounding     = 6.0f;
        style.FrameRounding     = 5.0f;
        style.PopupRounding     = 6.0f;
        style.ScrollbarRounding = 4.0f;
        style.GrabRounding      = 4.0f;
        style.TabRounding       = 4.0f;

        style.WindowBorderSize  = 1.0f;
        style.ChildBorderSize   = 1.0f;
        style.PopupBorderSize   = 1.0f;
        style.FrameBorderSize   = 1.0f;

        style.WindowPadding     = ImVec2(8.0f, 6.0f);
        style.FramePadding      = ImVec2(8.0f, 4.0f);
        style.ItemSpacing       = ImVec2(6.0f, 5.0f);
        style.ItemInnerSpacing  = ImVec2(5.0f, 4.0f);
    }

    static void loadUnrealFont(ImGuiIO& io) {
        const std::vector<std::string> fontCandidates = {
            "C:/Windows/Fonts/segoeui.ttf",
            "C:/Windows/Fonts/segoeuib.ttf",
            "assets/fonts/Roboto-Medium.ttf",
            "engine/assets/fonts/Roboto-Medium.ttf",
            "../assets/fonts/Roboto-Medium.ttf",
            "../../engine/assets/fonts/Roboto-Medium.ttf",
            "../../assets/fonts/Roboto-Medium.ttf",
            "../../../assets/fonts/Roboto-Medium.ttf",
            "public/fonts/Roboto-Medium.ttf",
            "../public/fonts/Roboto-Medium.ttf",
            "../../public/fonts/Roboto-Medium.ttf",
            "dist/fonts/Roboto-Medium.ttf",
            "C:/Windows/Fonts/arial.ttf"
        };

        static const ImWchar glyphRanges[] = {
            0x0020, 0x00FF,
            0x0100, 0x017F, // Polish characters (ąćęłńóśźż)
            0x2000, 0x206F, // Punctuation (•, —, …)
            0x25A0, 0x25FF, // Geometric shapes
            0,
        };

        ImFont* loadedFont = nullptr;
        for (const auto& path : fontCandidates) {
            if (std::filesystem::exists(path)) {
                ImFontConfig config;
                config.OversampleH = 3;
                config.OversampleV = 2;
                config.RasterizerMultiply = 1.25f;
                loadedFont = io.Fonts->AddFontFromFileTTF(path.c_str(), 18.5f, &config, glyphRanges);
                if (loadedFont) {
                    engine::core::Logger::info("Loaded crisp TrueType UI font: " + path);
                    break;
                }
            }
        }
        if (!loadedFont) {
            io.Fonts->AddFontDefault();
        }
    }

    void VulkanRenderer::initImGui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        loadUnrealFont(io);
        applyUnrealWebTheme();

        ImGui_ImplGlfw_InitForVulkan(m_context->getWindow(), true);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(m_context->getWindow(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }

        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = m_context->getInstance();
        init_info.PhysicalDevice = m_context->getPhysicalDevice();
        init_info.Device = m_context->getDevice();
        init_info.QueueFamily = m_context->getGraphicsQueueFamily();
        init_info.Queue = m_context->getGraphicsQueue();
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = m_context->getImGuiPool();
        init_info.RenderPass = VK_NULL_HANDLE;
        init_info.Subpass = 0;
        init_info.MinImageCount = 2;
        init_info.ImageCount = 2;
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = nullptr;
        init_info.UseDynamicRendering = true;

        static VkFormat colorFormat;
        colorFormat = m_context->getSwapchainImageFormat();
        init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;

        ImGui_ImplVulkan_Init(&init_info);

        vkResetCommandBuffer(m_commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

        ImGui_ImplVulkan_CreateFontsTexture();

        vkEndCommandBuffer(m_commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffer;
        vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_context->getGraphicsQueue());

        ImGui_ImplVulkan_DestroyFontsTexture();

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 1.0f;
        if (vkCreateSampler(m_context->getDevice(), &samplerInfo, nullptr, &m_viewportSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create viewport sampler!");
        }

        m_viewportDescriptor = ImGui_ImplVulkan_AddTexture(m_viewportSampler, m_colorImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void VulkanRenderer::renderImGui(VkCommandBuffer cmd, VkImageView targetView, ecs::Registry& registry, core::AssetManager* assetManager, core::Engine* engine) {
        (void)assetManager;
        (void)registry;
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* mainVp = ImGui::GetMainViewport();
        float screenW = mainVp->Size.x;
        float screenH = mainVp->Size.y;

        if (m_standaloneGameMode) {
            ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(320, 100), ImGuiCond_Always);
            ImGuiWindowFlags hudFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings;
            ImGui::Begin("GameHUD", nullptr, hudFlags);
            int hp = engine ? engine->getHealth() : 100;
            int score = engine ? engine->getScore() : 0;
            std::string st = engine ? engine->getGameStatus() : "PLAYING";

            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.06f, 0.73f, 0.51f, 1.0f));
            ImGui::ProgressBar(hp / 100.0f, ImVec2(300, 20), ("HP: " + std::to_string(hp) + "%").c_str());
            ImGui::PopStyleColor();
            ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.08f, 1.0f), "SCORE: %d", score);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "  |  %s", st.c_str());
            ImGui::End();
        } else {
            // Sleek, minimal, non-intrusive 3D Viewport HUD
            ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Always);
            ImGuiWindowFlags pillFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.07f, 0.10f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.55f, 0.95f, 0.40f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));

            if (ImGui::Begin("ViewportHUD", nullptr, pillFlags)) {
                ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "LUCID ENGINE 5.8");
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.5f, 1.0f), "Vulkan 1.3 HDR");
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%.0f FPS", ImGui::GetIO().Framerate);

                ImGui::Separator();
                if (ImGui::Button("Otworz Studio UI (1:1 Web/Desktop)")) {
#ifdef _WIN32
                    std::system("start http://127.0.0.1:9000");
#endif
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset Sceny")) {
                    if (engine) engine->resetScene(engine->getCurrentPreset());
                }
                ImGui::SameLine();
                if (ImGui::Button("Kompiluj .exe")) {
                    if (engine) engine->packageStandaloneGame("Build/Windows/MyLucidGame", "MyLucidGame");
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);

            // Bottom-left navigation hint
            ImGui::SetNextWindowPos(ImVec2(16, screenH - 42), ImGuiCond_Always);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.07f, 0.10f, 0.75f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.25f, 0.3f, 0.3f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 5));
            if (ImGui::Begin("NavHints", nullptr, pillFlags)) {
                ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.8f, 1.0f), "PPM+WASD: Kamera • F1: Studio Shell • F11: Pelny Ekran • Port: 9000");
            }
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }

        ImGui::Render();

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = targetView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea.extent = m_context->getSwapchainExtent();
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(cmd, &renderInfo);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        vkCmdEndRendering(cmd);
    }

#endif
}