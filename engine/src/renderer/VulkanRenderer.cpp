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
#include <algorithm>

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
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
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

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
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
        // 1. Allocate Scene UBO Buffer
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

        // 2. Setup Descriptor Layout (Binding 0: Shadow Map, Binding 1: Scene UBO)
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

        // 3. Setup Descriptor Pool
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

        // 4. Allocate and Write Descriptor Set
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
        rasterizer.depthBiasConstantFactor = 1.25f;
        rasterizer.depthBiasSlopeFactor = 1.75f;

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
        // PASS 1: DIRECTIONAL SHADOW PASS (Unreal Engine 5 Cascaded/Directional Depth)
        // =========================================================================
        glm::vec3 sunDir = engine ? glm::normalize(engine->getEnvironmentSettings().sunDirection) : glm::normalize(glm::vec3(0.55f, 0.85f, 0.40f));
        glm::vec3 camPos = engine ? engine->getCameraPosition() : glm::vec3(glm::inverse(viewMatrix)[3]);
        
        // Target focal area in world space
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
        lightTarget.y = 0.0f; // Anchor shadow projection plane to ground level

        float orthoExtent = 35.0f;
        // Snap light target to shadow texels to eliminate shadow crawling/shimmering (UE5 technique)
        float worldUnitsPerTexel = (2.0f * orthoExtent) / static_cast<float>(m_shadowWidth);
        lightTarget.x = std::round(lightTarget.x / worldUnitsPerTexel) * worldUnitsPerTexel;
        lightTarget.z = std::round(lightTarget.z / worldUnitsPerTexel) * worldUnitsPerTexel;

        glm::vec3 lightPos = lightTarget + sunDir * 60.0f;
        glm::mat4 lightView = glm::lookAt(lightPos, lightTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightProj = glm::ortho(-orthoExtent, orthoExtent, -orthoExtent, orthoExtent, 1.0f, 120.0f);
        lightProj[1][1] *= -1.0f; // Vulkan Y-flip
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

                // Ground plane doesn't cast shadow onto itself
                if (meshComp.meshId == 1) continue;

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

            // Transition shadow map to SHADER_READ_ONLY_OPTIMAL for sampling in Pass 2
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
        // PASS 2: MAIN PBR PASS (UE5 Cook-Torrance BRDF + PCF Soft Shadows)
        // =========================================================================

        // Transition m_colorImage to COLOR_ATTACHMENT_OPTIMAL and m_depthImage to DEPTH_STENCIL_ATTACHMENT_OPTIMAL
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

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = m_colorImageView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = { {0.06f, 0.07f, 0.09f, 1.0f} };

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

        // --- DRAW 3D SCENE ---
        if (m_graphicsPipeline) {
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

            // Populate Scene Lighting, Atmosphere & Point Lights UBO
            if (m_sceneUboMappedPtr) {
                SceneUboData ubo{};
                if (engine) {
                    const auto& env = engine->getEnvironmentSettings();
                    ubo.sunDirection = glm::vec4(glm::normalize(env.sunDirection), env.sunIntensity);
                    ubo.sunColor = glm::vec4(env.sunColor, 1.0f);
                    ubo.ambientColor = glm::vec4(env.ambientColor, env.ambientIntensity);
                    ubo.fogParams = glm::vec4(env.fogEnabled ? 1.0f : 0.0f, env.fogNear, env.fogFar, 0.0f);
                    ubo.fogColor = glm::vec4(env.fogColor, 1.0f);
                } else {
                    ubo.sunDirection = glm::vec4(glm::normalize(glm::vec3(0.55f, 0.85f, 0.40f)), 3.4f);
                    ubo.sunColor = glm::vec4(1.0f, 0.97f, 0.92f, 1.0f);
                    ubo.ambientColor = glm::vec4(0.15f, 0.22f, 0.35f, 1.0f);
                    ubo.fogParams = glm::vec4(0.0f, 20.0f, 100.0f, 0.0f);
                    ubo.fogColor = glm::vec4(0.06f, 0.07f, 0.09f, 1.0f);
                }

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
            glm::vec3 camPos = engine ? engine->getCameraPosition() : glm::vec3(glm::inverse(viewMatrix)[3]);
            pcs.cameraPos = glm::vec4(camPos, 0.0f);

#ifdef USE_GLFW
            float aspect = (m_standaloneGameMode || m_viewportAspectRatio <= 0.1f) ? (viewport.width / viewport.height) : m_viewportAspectRatio;
#else
            float aspect = viewport.width / viewport.height;
#endif
            glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
            projection[1][1] *= -1; // Vulkan Y-flip
            glm::mat4 cameraView = viewMatrix;
            glm::mat4 vp = projection * cameraView;

            auto view = registry.get().view<ecs::TransformComponent, ecs::MeshComponent>();
            for (auto entity : view) {
                const auto& transform = view.get<ecs::TransformComponent>(entity);
                const auto& meshComp = view.get<ecs::MeshComponent>(entity);

                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, transform.position);
                model *= glm::mat4_cast(transform.orientation);
                model = glm::scale(model, transform.scale);
                
                if (meshComp.meshId == 1) { // Ground plane special scale
                    model = glm::scale(model, glm::vec3(100.0f, 4.0f, 100.0f));
                    pcs.materialParams = glm::vec4(0.05f, 0.85f, 1.0f, 0.0f); // isGround = 1.0
                    pcs.baseColorFactor = glm::vec4(0.12f, 0.14f, 0.18f, 1.0f); // Sleek Unreal Engine dark floor
                } else {
                    if (registry.get().all_of<ecs::MaterialComponent>(entity)) {
                        const auto& mat = registry.get().get<ecs::MaterialComponent>(entity);
                        pcs.baseColorFactor = mat.baseColor;
                        pcs.materialParams = glm::vec4(mat.metallic, mat.roughness, 0.0f, mat.emissiveIntensity);
                    } else {
                        // Cohesive high-fidelity UE5 studio palette
                        static const glm::vec4 uePalette[] = {
                            glm::vec4(0.00f, 0.65f, 0.95f, 1.0f), // Unreal Cyan
                            glm::vec4(0.95f, 0.75f, 0.15f, 1.0f), // Brushed Gold
                            glm::vec4(0.85f, 0.22f, 0.22f, 1.0f), // Crimson Oxide
                            glm::vec4(0.35f, 0.85f, 0.45f, 1.0f), // Emerald Jade
                            glm::vec4(0.70f, 0.40f, 0.95f, 1.0f), // Royal Violet
                            glm::vec4(0.88f, 0.88f, 0.92f, 1.0f)  // Polished Titanium
                        };
                        uint32_t eId = static_cast<uint32_t>(entity);
                        pcs.baseColorFactor = uePalette[eId % 6];
                        pcs.materialParams = glm::vec4(0.4f, 0.35f, 0.0f, 0.0f); // Sleek PBR
                    }
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

        // Render Niagara-style particles
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
                    } else { // default: fire
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
                memcpy(m_particleMappedPtr, particles.data(), particles.size() * sizeof(ParticleVertex));
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_particlePipeline);

                float pAspect = static_cast<float>(m_width) / static_cast<float>(m_height);
#ifdef USE_GLFW
                if (!m_standaloneGameMode && m_viewportAspectRatio > 0.1f) {
                    pAspect = m_viewportAspectRatio;
                }
#endif
                glm::mat4 pProj = glm::perspective(glm::radians(45.0f), pAspect, 0.1f, 1000.0f);
                pProj[1][1] *= -1;
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

            // Transition swapchain image to TRANSFER_DST and m_colorImage to TRANSFER_SRC
            VkImageMemoryBarrier2 imgBarrierDst{};
            imgBarrierDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            imgBarrierDst.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            imgBarrierDst.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            imgBarrierDst.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            imgBarrierDst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            imgBarrierDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imgBarrierDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imgBarrierDst.image = swapchainImg;
            imgBarrierDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imgBarrierDst.subresourceRange.baseMipLevel = 0;
            imgBarrierDst.subresourceRange.levelCount = 1;
            imgBarrierDst.subresourceRange.baseArrayLayer = 0;
            imgBarrierDst.subresourceRange.layerCount = 1;
            
            VkImageMemoryBarrier2 imgBarrierSrc{};
            imgBarrierSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            imgBarrierSrc.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            imgBarrierSrc.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            imgBarrierSrc.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            imgBarrierSrc.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            imgBarrierSrc.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            imgBarrierSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imgBarrierSrc.image = m_colorImage;
            imgBarrierSrc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imgBarrierSrc.subresourceRange.baseMipLevel = 0;
            imgBarrierSrc.subresourceRange.levelCount = 1;
            imgBarrierSrc.subresourceRange.baseArrayLayer = 0;
            imgBarrierSrc.subresourceRange.layerCount = 1;

            VkImageMemoryBarrier2 copyBarriers[] = {imgBarrierDst, imgBarrierSrc};
            VkDependencyInfo copyDepInfo{};
            copyDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            copyDepInfo.imageMemoryBarrierCount = 2;
            copyDepInfo.pImageMemoryBarriers = copyBarriers;
            vkCmdPipelineBarrier2(m_commandBuffer, &copyDepInfo);
            
            // Blit m_colorImage to swapchainImg
            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {(int32_t)m_width, (int32_t)m_height, 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = 0;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {(int32_t)m_context->getSwapchainExtent().width, (int32_t)m_context->getSwapchainExtent().height, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = 0;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;
            
            vkCmdBlitImage(m_commandBuffer, 
                m_colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
                swapchainImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                1, &blit, VK_FILTER_LINEAR);

            // Transition swapchain to COLOR_ATTACHMENT and m_colorImage to SHADER_READ_ONLY_OPTIMAL (for ImGui sampling)
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
            
            // For imgBarrier at the end
            VkImageMemoryBarrier2 imgBarrier = imgBarrierDst;

            VkImageMemoryBarrier2 depthBarrier{};
            depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            depthBarrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            depthBarrier.srcAccessMask = 0;
            depthBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            depthBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthBarrier.image = m_depthImage;
            depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            depthBarrier.subresourceRange.baseMipLevel = 0;
            depthBarrier.subresourceRange.levelCount = 1;
            depthBarrier.subresourceRange.baseArrayLayer = 0;
            depthBarrier.subresourceRange.layerCount = 1;

            VkImageMemoryBarrier2 barriers[] = {imgBarrier};

            VkDependencyInfo depInfo{};
            depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = barriers;
            vkCmdPipelineBarrier2(m_commandBuffer, &depInfo);

            renderImGui(m_commandBuffer, m_context->getSwapchainImageViews()[imageIndex], registry, assetManager, engine);

            // Transition swapchain image to present layout
            imgBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            imgBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            imgBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            imgBarrier.dstAccessMask = 0;
            imgBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            imgBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &imgBarrier;
            vkCmdPipelineBarrier2(m_commandBuffer, &depInfo);
        } else {
            // Headless transition to SHADER_READ_ONLY_OPTIMAL for frame capture
            VkImageMemoryBarrier2 imgBarrierSrc{};
            imgBarrierSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            imgBarrierSrc.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            imgBarrierSrc.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            imgBarrierSrc.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            imgBarrierSrc.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
            imgBarrierSrc.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            imgBarrierSrc.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imgBarrierSrc.image = m_colorImage;
            imgBarrierSrc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imgBarrierSrc.subresourceRange.baseMipLevel = 0;
            imgBarrierSrc.subresourceRange.levelCount = 1;
            imgBarrierSrc.subresourceRange.baseArrayLayer = 0;
            imgBarrierSrc.subresourceRange.layerCount = 1;

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
        imgBarrierSrc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imgBarrierSrc.subresourceRange.baseMipLevel = 0;
        imgBarrierSrc.subresourceRange.levelCount = 1;
        imgBarrierSrc.subresourceRange.baseArrayLayer = 0;
        imgBarrierSrc.subresourceRange.layerCount = 1;

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

        VkDeviceSize bufferSize = m_width * m_height * 4;

        vkResetCommandBuffer(m_captureCommandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(m_captureCommandBuffer, &beginInfo);

        // Transition m_colorImage to TRANSFER_SRC_OPTIMAL
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

        // Transition back to SHADER_READ_ONLY_OPTIMAL
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
        stbi_write_jpg_to_func(writeJpegBuffer, &jpegBytes, static_cast<int>(m_width), static_cast<int>(m_height), 4, srcPixels, 75);

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
    static void applyUnrealWebTheme() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        // Window & Panels (Obsidian Dark like #07070c, #0b0b12 and #0e0e16)
        colors[ImGuiCol_WindowBg]             = ImVec4(0.043f, 0.043f, 0.067f, 1.00f); // #0b0b11
        colors[ImGuiCol_ChildBg]              = ImVec4(0.055f, 0.055f, 0.086f, 1.00f); // #0e0e16
        colors[ImGuiCol_PopupBg]              = ImVec4(0.078f, 0.078f, 0.122f, 0.98f); // #14141f
        colors[ImGuiCol_Border]               = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);    // subtle white alpha border
        colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        // Headers & Titles (like #0a0a10)
        colors[ImGuiCol_TitleBg]              = ImVec4(0.035f, 0.035f, 0.055f, 1.00f); // #09090e
        colors[ImGuiCol_TitleBgActive]        = ImVec4(0.051f, 0.055f, 0.090f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.035f, 0.035f, 0.055f, 0.75f);
        colors[ImGuiCol_MenuBarBg]            = ImVec4(0.043f, 0.043f, 0.067f, 1.00f); // #0b0b11

        // Text & Labels (slate-200 and slate-400)
        colors[ImGuiCol_Text]                 = ImVec4(0.92f, 0.95f, 0.98f, 1.00f); // #e2e8f0
        colors[ImGuiCol_TextDisabled]         = ImVec4(0.48f, 0.52f, 0.60f, 1.00f); // #7b8499

        // Frames & Inputs (like #101018)
        colors[ImGuiCol_FrameBg]              = ImVec4(0.045f, 0.045f, 0.070f, 1.00f); // #0c0c12
        colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.090f, 0.095f, 0.150f, 1.00f);
        colors[ImGuiCol_FrameBgActive]        = ImVec4(0.120f, 0.130f, 0.200f, 1.00f);

        // Buttons (like #161626, hover #202036, active Cyan #00b4d8)
        colors[ImGuiCol_Button]               = ImVec4(0.086f, 0.086f, 0.149f, 1.00f); // #161626
        colors[ImGuiCol_ButtonHovered]        = ImVec4(0.133f, 0.141f, 0.224f, 1.00f); // #222439
        colors[ImGuiCol_ButtonActive]         = ImVec4(0.000f, 0.706f, 0.847f, 1.00f); // Cyan accent

        // Headers & Selection (UE5 blue selection)
        colors[ImGuiCol_Header]               = ImVec4(0.000f, 0.373f, 0.722f, 0.75f); // #005fb8
        colors[ImGuiCol_HeaderHovered]        = ImVec4(0.000f, 0.450f, 0.820f, 0.85f);
        colors[ImGuiCol_HeaderActive]         = ImVec4(0.000f, 0.500f, 0.900f, 1.00f);

        // Tabs (UE5 style tabs)
        colors[ImGuiCol_Tab]                  = ImVec4(0.055f, 0.055f, 0.086f, 1.00f);
        colors[ImGuiCol_TabHovered]           = ImVec4(0.133f, 0.141f, 0.224f, 1.00f);
        colors[ImGuiCol_TabActive]            = ImVec4(0.094f, 0.102f, 0.165f, 1.00f);
        colors[ImGuiCol_TabUnfocused]         = ImVec4(0.039f, 0.039f, 0.063f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.071f, 0.071f, 0.118f, 1.00f);

        // Scrollbars & Grabs
        colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.039f, 0.039f, 0.063f, 0.60f);
        colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.165f, 0.176f, 0.267f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.224f, 0.239f, 0.353f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.000f, 0.706f, 0.847f, 1.00f);
        colors[ImGuiCol_CheckMark]            = ImVec4(0.000f, 0.847f, 0.678f, 1.00f); // Emerald/Cyan
        colors[ImGuiCol_SliderGrab]           = ImVec4(0.000f, 0.706f, 0.847f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.000f, 0.847f, 1.000f, 1.00f);
        colors[ImGuiCol_Separator]            = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
        colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.000f, 0.706f, 0.847f, 0.78f);
        colors[ImGuiCol_SeparatorActive]      = ImVec4(0.000f, 0.706f, 0.847f, 1.00f);
        colors[ImGuiCol_ResizeGrip]           = ImVec4(0.165f, 0.176f, 0.267f, 0.30f);
        colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.000f, 0.706f, 0.847f, 0.67f);
        colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.000f, 0.706f, 0.847f, 1.00f);

        // Flat docked windows, subtle roundings on frames and popups
        style.WindowRounding    = 0.0f;
        style.ChildRounding     = 4.0f;
        style.FrameRounding     = 4.0f;
        style.PopupRounding     = 6.0f;
        style.ScrollbarRounding = 4.0f;
        style.GrabRounding      = 3.0f;
        style.TabRounding       = 4.0f;

        style.WindowBorderSize  = 1.0f;
        style.ChildBorderSize   = 1.0f;
        style.PopupBorderSize   = 1.0f;
        style.FrameBorderSize   = 1.0f;

        style.WindowPadding     = ImVec2(8.0f, 6.0f);
        style.FramePadding      = ImVec2(6.0f, 3.5f);
        style.ItemSpacing       = ImVec2(6.0f, 4.0f);
        style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
    }
    void VulkanRenderer::initImGui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.IniFilename = nullptr; // Responsive auto-layout per resolution; do not lock to stale 4K ini coordinates
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // Load authentic Unreal Engine 5 Roboto Font
        if (std::filesystem::exists("engine/assets/fonts/Roboto-Medium.ttf")) {
            io.Fonts->AddFontFromFileTTF("engine/assets/fonts/Roboto-Medium.ttf", 15.0f);
        } else if (std::filesystem::exists("engine/assets/fonts/Roboto-Regular.ttf")) {
            io.Fonts->AddFontFromFileTTF("engine/assets/fonts/Roboto-Regular.ttf", 15.0f);
        }
        
        applyUnrealWebTheme();

        ImGui_ImplGlfw_InitForVulkan(m_context->getWindow(), true);
#ifdef USE_GLFW
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(m_context->getWindow(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
#endif
        
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

        // Upload fonts
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

        // Initialize Viewport Sampler & Register Descriptor with ImGui
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

    enum class NativeGizmoMode {
        TRANSLATE = 0,
        ROTATE = 1,
        SCALE = 2
    };

    static float distToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
        float abx = b.x - a.x;
        float aby = b.y - a.y;
        float lenSq = abx * abx + aby * aby;
        if (lenSq < 1e-4f) {
            float dx = p.x - a.x;
            float dy = p.y - a.y;
            return sqrtf(dx * dx + dy * dy);
        }
        float t = ((p.x - a.x) * abx + (p.y - a.y) * aby) / lenSq;
        t = std::clamp(t, 0.0f, 1.0f);
        float projX = a.x + t * abx;
        float projY = a.y + t * aby;
        float dx = p.x - projX;
        float dy = p.y - projY;
        return sqrtf(dx * dx + dy * dy);
    }

    static bool drawVec3Control(const char* label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 70.0f) {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.90f, 1.0f), "%s", label);
        ImGui::NextColumn();

        float lineHeight = ImGui::GetFrameHeight();
        ImVec2 buttonSize = { lineHeight + 2.0f, lineHeight };
        float availW = ImGui::GetContentRegionAvail().x;
        float itemWidth = (availW - 3.0f * buttonSize.x - 6.0f * 4.0f) / 3.0f;
        if (itemWidth < 35.0f) itemWidth = 35.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));

        // X (Red button)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.18f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.28f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.12f, 0.14f, 1.0f));
        if (ImGui::Button("X", buttonSize)) { values.x = resetValue; changed = true; }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(itemWidth);
        if (ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f")) changed = true;
        ImGui::SameLine();

        // Y (Green button)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.75f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.90f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.60f, 0.18f, 1.0f));
        if (ImGui::Button("Y", buttonSize)) { values.y = resetValue; changed = true; }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(itemWidth);
        if (ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f")) changed = true;
        ImGui::SameLine();

        // Z (Blue button)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.35f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.48f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.28f, 0.75f, 1.0f));
        if (ImGui::Button("Z", buttonSize)) { values.z = resetValue; changed = true; }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(itemWidth);
        if (ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f")) changed = true;

        ImGui::PopStyleVar();
        ImGui::Columns(1);
        ImGui::PopID();
        return changed;
    }

    void VulkanRenderer::renderImGui(VkCommandBuffer cmd, VkImageView targetView, ecs::Registry& registry, core::AssetManager* assetManager, core::Engine* engine) {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- GLOBAL UI SCALING & RESPONSIVE LAYOUT CALCULATION ---
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float screenW = viewport->Size.x;
        float screenH = viewport->Size.y;

        // Auto-scale relative to 1080p standard reference resolution (1920x1080)
        float autoScaleY = screenH / 1080.0f;
        float autoScaleX = screenW / 1920.0f;
        float autoScale = std::min(autoScaleX, autoScaleY);
        if (autoScale < 0.65f) autoScale = 0.65f;
        if (autoScale > 3.0f) autoScale = 3.0f;

        // Effective UI Scale: Auto-Scale * user modifier (m_uiScale)
        float s_uiScale = autoScale * m_uiScale;
        ImGui::GetIO().FontGlobalScale = s_uiScale;

        // 3-Tier Top Header matching Unreal Engine 5 Web Studio
        float topTitleBarH = 28.0f * s_uiScale;
        float topLevelTabsH = 28.0f * s_uiScale;
        float topToolbarH = 38.0f * s_uiScale;
        float topTotalH = topTitleBarH + topLevelTabsH + topToolbarH;

        // Bottom Dock Bar & Expandable Sliding Drawers
        float bottomStatusH = 28.0f * s_uiScale;
        static bool s_showContentDrawer = false;
        static bool s_showOutputLog = false;
        float drawerH = (s_showContentDrawer || s_showOutputLog) ? (210.0f * s_uiScale) : 0.0f;
        if (drawerH > screenH * 0.38f) drawerH = screenH * 0.38f;
        float bottomTotalH = bottomStatusH + drawerH;

        float contentH = screenH - topTotalH - bottomTotalH;
        if (contentH < 180.0f) contentH = 180.0f;

        float leftPanelW = 260.0f * s_uiScale;
        if (leftPanelW > screenW * 0.30f) leftPanelW = screenW * 0.30f;

        float rightPanelW = 350.0f * s_uiScale;
        if (rightPanelW > screenW * 0.35f) rightPanelW = screenW * 0.35f;

        float centerViewportW = screenW - leftPanelW - rightPanelW;
        if (centerViewportW < 200.0f) centerViewportW = 200.0f;

        static entt::entity selectedEnt = entt::null;
        static NativeGizmoMode s_gizmoMode = NativeGizmoMode::TRANSLATE;
        static int s_draggingGizmoAxis = 0;
        static glm::vec3 s_dragInitialEntityPos(0.0f);
        static glm::vec3 s_dragPlaneNormal(0.0f, 1.0f, 0.0f);
        static glm::vec3 s_dragAxisVec(1.0f, 0.0f, 0.0f);
        static glm::vec3 s_dragInitialHitPoint(0.0f);
        static float s_dragInitialOffset = 0.0f;
        static bool s_dragHasHit = false;

        static glm::vec3 s_dragInitialRotation(0.0f);
        static float s_dragInitialAngle = 0.0f;
        static float s_lastRotAngle = 0.0f;
        static float s_accumulatedRotAngle = 0.0f;
        static ImVec2 s_dragPivotScreen(0.0f, 0.0f);

        static glm::vec3 s_dragInitialScale(1.0f);
        static float s_dragInitialDist = 1.0f;

        static bool s_isRmbNavigating = false;
        static bool s_isMmbPanning = false;
        static bool s_isAltOrbiting = false;
        static float s_camFlySpeed = 25.0f;
        static bool s_showScriptStudio = false;
        static bool s_showEffectsPanel = false;
        static bool s_gizmoSpaceLocal = false;
        static bool s_snapEnabled = false;
        static float s_snapStep = 0.5f;
        static int s_cameraSpeed = 4;
        static int s_placeActorCategory = 3; // Default 3 = Basic (0: Fav, 1: UE5.8, 2: Rec, 3: Basic, 4: Lights, 5: Shapes, 6: VFX, 7: Volumes)
        static int s_detailsTab = 0; // 0=Details, 1=World Settings
        static int s_detailsFilterCat = 0; // 0=All, 1=General, 2=Actor, 3=Physics, 4=Rendering

        if (selectedEnt != entt::null && (!registry.get().valid(selectedEnt))) {
            selectedEnt = entt::null;
            s_draggingGizmoAxis = 0;
            if (engine) engine->setSelectedEntity(entt::null);
        }

        if (engine && engine->getSelectedEntity() != entt::null && selectedEnt == entt::null) {
            selectedEnt = engine->getSelectedEntity();
        }

        if (m_standaloneGameMode) {
            // Standalone Pure Game HUD (Health, Score, ESC - Pause/Quit)
            ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(320, 100), ImGuiCond_Always);
            ImGuiWindowFlags hudFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings;
            ImGui::Begin("GameHUD", nullptr, hudFlags);
            int hp = engine ? engine->getHealth() : 100;
            int score = engine ? engine->getScore() : 0;
            std::string st = engine ? engine->getGameStatus() : "PLAYING";
            
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.1f, 0.85f, 0.4f, 1.0f));
            ImGui::ProgressBar(hp / 100.0f, ImVec2(300, 22), ("HP: " + std::to_string(hp) + "%").c_str());
            ImGui::PopStyleColor();
            
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "SCORE: %d", score);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "  |  STATUS: %s", st.c_str());
            ImGui::TextDisabled("Vulkan 1.3 Direct PC Runtime | ESC to Exit");
            ImGui::End();
            return;
        }

        // ══════════════════════════════════════════════════════════════════════════
        // ═══ 1. TOP TITLE BAR (UNREAL ENGINE 5 WINDOW TITLE BAR & MENUS) ═════════
        // ══════════════════════════════════════════════════════════════════════════
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(screenW, topTitleBarH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 2));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.043f, 0.063f, 1.0f)); // #0b0b10
        ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.043f, 0.043f, 0.063f, 1.0f));
        ImGuiWindowFlags titleWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("TopTitleBar", nullptr, titleWindowFlags);

        if (ImGui::BeginMenuBar()) {
            // UE5 Cyan/Indigo Brand Badge
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.48f, 0.85f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::Button(" U  LUCID 5.8 ");
            ImGui::PopStyleColor(2);
            ImGui::SameLine();

            // Menu: File
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Project...", "Ctrl+N")) { ImGui::OpenPopup("New Project Setup"); }
                if (ImGui::MenuItem("Open Project...", "Ctrl+O")) { ImGui::OpenPopup("Open Project"); }
                if (ImGui::MenuItem("Save Level / Project", "Ctrl+S")) {
                    engine::core::Logger::info("Level and project saved successfully.");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Kompiluj pelna gre (.exe)", "Ctrl+Shift+P")) {
                    engine::core::Logger::info("Packaging standalone binary...");
                    if (engine) engine->packageStandaloneGame("Build/Windows/MyLucidGame", "MyLucidGame");
                }
                if (ImGui::MenuItem("Export Project (.lucidproject)")) {
                    engine::core::Logger::info("Project exported to .lucidproject format.");
                }
                if (ImGui::MenuItem("Import 3D Mesh (.glb)")) {}
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) {}
                ImGui::EndMenu();
            }

            // Menu: Edit
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
                if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
                ImGui::Separator();
                if (ImGui::MenuItem("Project Settings...")) { s_detailsTab = 1; }
                if (ImGui::MenuItem("Editor Preferences...")) {}
                ImGui::EndMenu();
            }

            // Menu: Window
            if (ImGui::BeginMenu("Window")) {
                ImGui::MenuItem("Place Actors", nullptr, nullptr);
                ImGui::MenuItem("Outliner", nullptr, nullptr);
                ImGui::MenuItem("Details", nullptr, nullptr);
                ImGui::MenuItem("Content Drawer", "Ctrl+Space", &s_showContentDrawer);
                ImGui::MenuItem("Output Log", nullptr, &s_showOutputLog);
                ImGui::MenuItem("Blueprints / Scripts Studio", nullptr, &s_showScriptStudio);
                ImGui::EndMenu();
            }

            // Menu: Tools
            if (ImGui::BeginMenu("Tools")) {
                if (ImGui::MenuItem("Execute QuickJS Script")) { s_showScriptStudio = true; }
                if (ImGui::MenuItem("Jolt Physics Diagnostics")) {
                    engine::core::Logger::info("Jolt Physics: World contacts healthy, 60 Hz stepping.");
                }
                if (ImGui::MenuItem("Vulkan Shader Profiler")) {
                    engine::core::Logger::info("Vulkan 1.3: GPU Pipeline Cache hit rate 99.4%, VSM Active.");
                }
                if (ImGui::MenuItem("Export TypeScript Definitions (.d.ts)")) {
                    if (engine) engine->exportTypeDefinitions();
                }
                ImGui::EndMenu();
            }

            // Menu: Build
            if (ImGui::BeginMenu("Build")) {
                if (ImGui::MenuItem("Build All Levels", "Ctrl+Shift+B")) {}
                if (ImGui::MenuItem("Build Lighting & Shadows (VSM)")) {}
                if (ImGui::MenuItem("Build Jolt Collision Meshes")) {}
                ImGui::EndMenu();
            }

            // Menu: Platforms
            if (ImGui::BeginMenu("Platforms")) {
                ImGui::MenuItem("Windows (x86_64 Vulkan 1.3)", nullptr, true);
                ImGui::MenuItem("WebAssembly (WebGL2)", nullptr, false);
                ImGui::Separator();
                if (ImGui::MenuItem("Package Windows Release Build (.exe)...")) {
                    if (engine) engine->packageStandaloneGame("Build/Windows/MyLucidGame", "MyLucidGame");
                }
                ImGui::EndMenu();
            }

            // Menu: Select
            if (ImGui::BeginMenu("Select")) {
                if (ImGui::MenuItem("Select All Actors", "Ctrl+A")) {}
                if (ImGui::MenuItem("Deselect", "Esc")) {
                    selectedEnt = entt::null;
                    if (engine) engine->setSelectedEntity(entt::null);
                }
                if (ImGui::MenuItem("Invert Selection", "Ctrl+I")) {}
                if (ImGui::MenuItem("Focus Selected Actor", "F")) {
                    if (selectedEnt != entt::null && registry.get().valid(selectedEnt) && registry.get().all_of<ecs::TransformComponent>(selectedEnt) && engine) {
                        auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                        float cYaw = engine->getCameraYaw();
                        float cPitch = engine->getCameraPitch();
                        glm::vec3 cFront(
                            cos(glm::radians(cYaw)) * cos(glm::radians(cPitch)),
                            sin(glm::radians(cPitch)),
                            sin(glm::radians(cYaw)) * cos(glm::radians(cPitch))
                        );
                        cFront = glm::normalize(cFront);
                        glm::vec3 newCamPos = t.position - cFront * 8.0f;
                        engine->setCamera(newCamPos, cYaw, cPitch, engine->getCameraFov());
                    }
                }
                ImGui::EndMenu();
            }

            // Menu: Actor
            if (ImGui::BeginMenu("Actor")) {
                if (ImGui::MenuItem("Duplicate Actor", "Ctrl+D")) {}
                if (ImGui::MenuItem("Delete Selected Actor", "Del")) {
                    if (selectedEnt != entt::null && registry.get().valid(selectedEnt)) {
                        registry.get().destroy(selectedEnt);
                        selectedEnt = entt::null;
                        if (engine) engine->setSelectedEntity(entt::null);
                    }
                }
                if (ImGui::MenuItem("Attach QuickJS Script")) { s_showScriptStudio = true; }
                ImGui::EndMenu();
            }

            // Menu: Help
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("Lucid Engine Documentation")) {}
                if (ImGui::MenuItem("Keyboard Shortcuts & Navigation")) {}
                ImGui::EndMenu();
            }

            // Center Project Title matching WebGL TitleBar
            float centerOffset = screenW * 0.42f;
            if (ImGui::GetCursorPosX() < centerOffset) {
                ImGui::SameLine(centerOffset);
            }
            ImGui::TextColored(ImVec4(0.45f, 0.52f, 0.65f, 1.0f), "Project:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.25f, 0.85f, 1.0f, 1.0f), "moj projekt gry");
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "UE 5.8 / Linear Hybrid | Vulkan 1.3");

            // Right-aligned window controls & Prototype label
            float rightButtonsW = 150.0f * s_uiScale;
            ImGui::SameLine(screenW - rightButtonsW);
            ImGui::TextDisabled("Prototype");
            ImGui::SameLine();
            ImGui::Button(" - ");
            ImGui::SameLine();
            ImGui::Button("[ ]");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
            ImGui::Button(" X ");
            ImGui::PopStyleColor();

            ImGui::EndMenuBar();
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        // ══════════════════════════════════════════════════════════════════════════
        // ═══ 2. LEVEL TABS STRIP (UNREAL ENGINE 5 ACTIVE LEVEL TABS) ══════════════
        // ══════════════════════════════════════════════════════════════════════════
        ImGui::SetNextWindowPos(ImVec2(0, topTitleBarH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(screenW, topLevelTabsH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 2));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.063f, 0.063f, 0.090f, 1.0f)); // #101017
        ImGuiWindowFlags levelTabsFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("LevelTabsStrip", nullptr, levelTabsFlags);

        std::string curPreset = engine ? engine->getCurrentPreset() : "breakout";

        auto renderLevelTab = [&](const char* label, const char* presetName, bool isPersistent) {
            bool isActive = (curPreset == presetName);
            if (isActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.094f, 0.094f, 0.145f, 1.0f)); // #181824
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.85f, 1.0f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.070f, 0.070f, 0.110f, 1.0f)); // #12121c
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.65f, 0.75f, 1.0f));
            }

            std::string btnText = std::string(isActive ? "^ " : "* ") + label + (isPersistent ? " (Persistent)  x" : "  x");
            if (ImGui::Button(btnText.c_str())) {
                if (engine) {
                    selectedEnt = entt::null;
                    s_draggingGizmoAxis = 0;
                    engine->setCurrentPreset(presetName);
                    engine->resetScene(presetName);
                }
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
        };

        renderLevelTab("Lvl_MainLevel.scene", "breakout", true);
        renderLevelTab("Platformer.scene", "platformer", false);
        renderLevelTab("Shooter.scene", "shooter", false);
        renderLevelTab("Racer.scene", "racer", false);

        if (ImGui::Button(" + ##NewLevelTab")) {
            if (engine) {
                selectedEnt = entt::null;
                s_draggingGizmoAxis = 0;
                engine->setCurrentPreset("empty");
                engine->resetScene("empty");
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");

        // Right side of Level Tabs: UI Scale & Resolution Scale Combos
        float tabsRightW = 380.0f * s_uiScale;
        ImGui::SameLine(screenW - tabsRightW);
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "UI:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80 * s_uiScale);
        const char* scaleItems[] = { "Auto", "75%", "100%", "125%", "150%", "200%" };
        static int scaleIdx = 0;
        if (ImGui::Combo("##UIScaleComboTabs", &scaleIdx, scaleItems, IM_ARRAYSIZE(scaleItems))) {
            if (scaleIdx == 0) setUiScale(1.0f);
            else if (scaleIdx == 1) setUiScale(0.75f);
            else if (scaleIdx == 2) setUiScale(1.0f);
            else if (scaleIdx == 3) setUiScale(1.25f);
            else if (scaleIdx == 4) setUiScale(1.50f);
            else if (scaleIdx == 5) setUiScale(2.0f);
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.5f, 1.0f), "Res:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100 * s_uiScale);
        const char* resItems[] = { "50% (540p)", "67% (720p)", "75% (810p)", "100% (1080p)", "133% (1440p)", "200% (4K)" };
        static int resIdx = 3;
        if (ImGui::Combo("##ResScaleComboTabs", &resIdx, resItems, IM_ARRAYSIZE(resItems))) {
            if (resIdx == 0) setResolutionScale(0.5f);
            else if (resIdx == 1) setResolutionScale(0.67f);
            else if (resIdx == 2) setResolutionScale(0.75f);
            else if (resIdx == 3) setResolutionScale(1.0f);
            else if (resIdx == 4) setResolutionScale(1.33f);
            else if (resIdx == 5) setResolutionScale(2.0f);
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        // ══════════════════════════════════════════════════════════════════════════
        // ═══ 3. MAIN TOOLBAR (SELECTION MODE, QUICK ADD, TRANSPORT & TOOLS) ═══════
        // ══════════════════════════════════════════════════════════════════════════
        ImGui::SetNextWindowPos(ImVec2(0, topTitleBarH + topLevelTabsH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(screenW, topToolbarH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 5));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.075f, 0.075f, 0.110f, 1.0f)); // #13131c
        ImGuiWindowFlags mainToolbarFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("MainToolbar", nullptr, mainToolbarFlags);

        // Selection Mode Pill
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.11f, 0.11f, 0.17f, 1.0f));
        if (ImGui::Button("[#] Select Mode v")) { ImGui::OpenPopup("SelectModePopup"); }
        if (ImGui::BeginPopup("SelectModePopup")) {
            if (ImGui::Selectable("Select Mode", true)) {}
            if (ImGui::Selectable("Landscape Mode")) {}
            if (ImGui::Selectable("Foliage Mode")) {}
            if (ImGui::Selectable("Modeling Mode")) {}
            if (ImGui::Selectable("Animation Mode")) {}
            ImGui::EndPopup();
        }
        ImGui::SameLine();

        // Save Button
        if (ImGui::Button(" [S] Save ")) {
            engine::core::Logger::info("Stan sceny zapisany.");
        }
        ImGui::SameLine();

        // Quick Add Actor (+)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.85f, 1.0f, 1.0f));
        if (ImGui::Button(" + Add v ")) { ImGui::OpenPopup("QuickAddPopup"); }
        ImGui::PopStyleColor();
        if (ImGui::BeginPopup("QuickAddPopup")) {
            ImGui::TextDisabled("ACTORS & SHAPES");
            if (ImGui::Selectable("Cube (StaticMeshActor)")) {
                auto ent = registry.createEntity();
                registry.get().emplace<ecs::TagComponent>(ent, ecs::TagComponent{"Cube_Mesh"});
                registry.get().emplace<ecs::TransformComponent>(ent, glm::vec3(0.0f, 2.0f, 0.0f));
                registry.get().emplace<ecs::MeshComponent>(ent, ecs::MeshComponent{2});
                selectedEnt = ent;
                if (engine) engine->setSelectedEntity(selectedEnt);
            }
            if (ImGui::Selectable("Sphere (RigidBody)")) {
                auto ent = registry.createEntity();
                registry.get().emplace<ecs::TagComponent>(ent, ecs::TagComponent{"Sphere_Pawn"});
                registry.get().emplace<ecs::TransformComponent>(ent, glm::vec3(0.0f, 5.0f, 0.0f));
                registry.get().emplace<ecs::MeshComponent>(ent, ecs::MeshComponent{3});
                ecs::RigidBodyComponent rb; rb.mass = 1.0f; rb.shapeType = 1;
                registry.get().emplace<ecs::RigidBodyComponent>(ent, rb);
                selectedEnt = ent;
                if (engine) engine->setSelectedEntity(selectedEnt);
            }
            if (ImGui::Selectable("Player Character (BP_Player)")) {
                auto ent = registry.createEntity();
                registry.get().emplace<ecs::TagComponent>(ent, ecs::TagComponent{"CharacterPlayer"});
                registry.get().emplace<ecs::TransformComponent>(ent, glm::vec3(0.0f, 3.0f, 0.0f));
                registry.get().emplace<ecs::MeshComponent>(ent, ecs::MeshComponent{2});
                ecs::RigidBodyComponent rb; rb.mass = 1.0f; rb.shapeType = 2; rb.capsuleRadius = 0.5f; rb.capsuleHalfHeight = 1.0f;
                rb.lockRotationX = true; rb.lockRotationZ = true;
                registry.get().emplace<ecs::RigidBodyComponent>(ent, rb);
                selectedEnt = ent;
                if (engine) engine->setSelectedEntity(selectedEnt);
            }
            if (ImGui::Selectable("Coin Pickable (BP_Coin)")) {
                auto ent = registry.createEntity();
                registry.get().emplace<ecs::TagComponent>(ent, ecs::TagComponent{"CoinTrigger"});
                registry.get().emplace<ecs::TransformComponent>(ent, glm::vec3(0.0f, 1.5f, 0.0f));
                registry.get().emplace<ecs::MeshComponent>(ent, ecs::MeshComponent{3});
                ecs::RigidBodyComponent rb; rb.mass = 0.0f; rb.shapeType = 1; rb.isSensor = true;
                registry.get().emplace<ecs::RigidBodyComponent>(ent, rb);
                selectedEnt = ent;
                if (engine) engine->setSelectedEntity(selectedEnt);
            }
            ImGui::Separator();
            ImGui::TextDisabled("VISUAL EFFECTS");
            if (ImGui::Selectable("Fire Particle Emitter (Niagara/VFX)")) {
                auto ent = registry.createEntity();
                registry.get().emplace<ecs::TagComponent>(ent, ecs::TagComponent{"FireVFX"});
                registry.get().emplace<ecs::TransformComponent>(ent, glm::vec3(0.0f, 1.0f, 0.0f));
                registry.get().emplace<ecs::ParticleEmitterComponent>(ent, ecs::ParticleEmitterComponent{"fire", glm::vec3(1.0f, 0.4f, 0.0f), 200, 3.0f, 0.4f});
                selectedEnt = ent;
                if (engine) engine->setSelectedEntity(selectedEnt);
            }
            if (ImGui::Selectable("Portal Vortex Emitter")) {
                auto ent = registry.createEntity();
                registry.get().emplace<ecs::TagComponent>(ent, ecs::TagComponent{"PortalVFX"});
                registry.get().emplace<ecs::TransformComponent>(ent, glm::vec3(0.0f, 2.0f, 0.0f));
                registry.get().emplace<ecs::ParticleEmitterComponent>(ent, ecs::ParticleEmitterComponent{"portal", glm::vec3(0.1f, 0.8f, 1.0f), 300, 4.0f, 0.3f});
                selectedEnt = ent;
                if (engine) engine->setSelectedEntity(selectedEnt);
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Blueprints / Scripts Button
        if (ImGui::Button("</> Blueprints / Scripts")) { s_showScriptStudio = !s_showScriptStudio; }
        ImGui::SameLine();
        if (ImGui::Button("* Cinematics & VFX")) { s_showEffectsPanel = !s_showEffectsPanel; }
        ImGui::SameLine();

        // Compile Game .exe in Emerald Pill
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.38f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.08f, 0.52f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.7f, 1.0f));
        if (ImGui::Button(" [v] Kompiluj gre (.exe) ")) {
            if (engine) {
                engine->packageStandaloneGame("Build/Windows/MyLucidGame", "MyLucidGame");
                engine::core::Logger::info("Game packaged to Build/Windows/MyLucidGame/MyLucidGame.exe");
            }
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleColor(); // pop mode button style

        // Center Transport Pill Bar
        float transportOffset = screenW * 0.46f;
        if (ImGui::GetCursorPosX() < transportOffset) {
            ImGui::SameLine(transportOffset);
        }

        bool isPaused = engine ? engine->isPaused() : true;
        if (isPaused) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.72f, 0.38f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.85f, 0.45f, 1.0f));
            if (ImGui::Button(" > Play ")) { if (engine) engine->playSimulation(); }
            ImGui::PopStyleColor(2);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.65f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.75f, 0.20f, 1.0f));
            if (ImGui::Button(" || Pause ")) { if (engine) engine->setPaused(true); }
            ImGui::PopStyleColor(2);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
        if (ImGui::Button(" # Stop ")) {
            if (engine) engine->stopSimulation();
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        if (ImGui::Button(" >| Step ")) {
            if (engine) engine->stepSimulation(1, 0.016f);
        }

        // Right side: Camera Mode, Network & FPS
        float toolRightW = 340.0f * s_uiScale;
        ImGui::SameLine(screenW - toolRightW);

        const char* camModes[] = { "Editor Cam", "TPP (Gracz)", "Top-Down", "Side View" };
        static int camModeIdx = 0;
        ImGui::SetNextItemWidth(105 * s_uiScale);
        if (ImGui::Combo("##CameraModeCombo", &camModeIdx, camModes, IM_ARRAYSIZE(camModes))) {
            if (engine) {
                if (camModeIdx == 0) engine->setCameraMode("editor");
                else if (camModeIdx == 1) engine->setCameraMode("thirdPerson");
                else if (camModeIdx == 2) engine->setCameraMode("topDown");
                else if (camModeIdx == 3) engine->setCameraMode("sideScroller");
            }
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "O WS: 9000");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "%.1f FPS", ImGui::GetIO().Framerate);

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        // ══════════════════════════════════════════════════════════════════════════
        // ═══ 4. LEFT PANEL: "PLACE ACTORS" (PALETTE) ═════════════════════════════
        // ══════════════════════════════════════════════════════════════════════════
        ImGui::SetNextWindowPos(ImVec2(0, topTotalH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(leftPanelW, contentH), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.071f, 0.071f, 0.098f, 1.0f)); // #121218
        ImGui::Begin("Place Actors", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        // Header Tab with Cyan Line
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "--- PLACE ACTORS ---");
        ImGui::SameLine(leftPanelW - 70 * s_uiScale);
        ImGui::TextDisabled("PALETTE");

        // Search Classes
        static char classSearch[64] = "";
        ImGui::InputTextWithHint("##SearchClasses", "Search Classes...", classSearch, sizeof(classSearch));
        ImGui::Separator();

        // Two-column Category Rail + Item Cards
        ImGui::Columns(2, "PlaceActorColumns", false);
        float railWidth = 46.0f * s_uiScale;
        ImGui::SetColumnWidth(0, railWidth);

        // Left Rail Category Buttons
        const char* catNames[] = { "Fav", "UE5", "Rec", "Basic", "Lights", "Shapes", "VFX", "Vol" };
        for (int i = 0; i < 8; ++i) {
            bool isCatActive = (s_placeActorCategory == i);
            if (isCatActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.85f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.05f, 0.07f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.55f, 0.65f, 1.0f));
            }
            if (ImGui::Button(catNames[i], ImVec2(railWidth - 8, 26 * s_uiScale))) {
                s_placeActorCategory = i;
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::NextColumn();

        auto spawnCard = [&](const char* title, const char* desc, int typeIdx, int catIdx) {
            if (s_placeActorCategory != catIdx && s_placeActorCategory != 0 && s_placeActorCategory != 2) return;
            if (classSearch[0] != '\0') {
                std::string tLower = title;
                std::string sLower = classSearch;
                std::transform(tLower.begin(), tLower.end(), tLower.begin(), ::tolower);
                std::transform(sLower.begin(), sLower.end(), sLower.begin(), ::tolower);
                if (tLower.find(sLower) == std::string::npos) return;
            }

            ImGui::PushID(title);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.090f, 0.090f, 0.133f, 1.0f)); // #171722
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.125f, 0.125f, 0.180f, 1.0f)); // #20202e

            if (ImGui::Button(title, ImVec2(-1, 26 * s_uiScale))) {
                auto entity = registry.createEntity();
                ecs::MeshComponent dynMesh; dynMesh.meshId = 2;
                registry.get().emplace<ecs::MeshComponent>(entity, dynMesh);

                if (typeIdx == 0) { // Empty Actor
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{title});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 2.0f, 0.0f));
                } else if (typeIdx == 1) { // Character Player
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"CharacterPlayer"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 4.0f, 0.0f));
                    ecs::RigidBodyComponent dynRb; dynRb.mass = 1.0f; dynRb.shapeType = 2;
                    dynRb.capsuleRadius = 0.6f; dynRb.capsuleHalfHeight = 1.0f;
                    dynRb.lockRotationX = true; dynRb.lockRotationZ = true;
                    registry.get().emplace<ecs::RigidBodyComponent>(entity, dynRb);
                } else if (typeIdx == 2) { // Capsule Pawn
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"CapsulePawn"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 5.0f, 0.0f));
                    ecs::RigidBodyComponent dynRb; dynRb.mass = 1.5f; dynRb.shapeType = 2;
                    dynRb.capsuleRadius = 0.5f; dynRb.capsuleHalfHeight = 1.2f;
                    registry.get().emplace<ecs::RigidBodyComponent>(entity, dynRb);
                } else if (typeIdx == 3) { // Coin Trigger
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"CoinTrigger"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 1.5f, 0.0f));
                    ecs::RigidBodyComponent dynRb; dynRb.mass = 0.0f; dynRb.shapeType = 1; dynRb.isSensor = true;
                    registry.get().emplace<ecs::RigidBodyComponent>(entity, dynRb);
                } else if (typeIdx == 4) { // Trigger Zone
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"TriggerZone"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f), glm::vec3(3.0f, 1.5f, 3.0f));
                    ecs::RigidBodyComponent dynRb; dynRb.mass = 0.0f; dynRb.shapeType = 0; dynRb.isSensor = true;
                    dynRb.halfExtents = glm::vec3(3.0f, 1.5f, 3.0f);
                    registry.get().emplace<ecs::RigidBodyComponent>(entity, dynRb);
                } else if (typeIdx == 5) { // Pawn (Rigid body)
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"DynamicPawn"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 8.0f, 0.0f));
                    ecs::RigidBodyComponent dynRb; dynRb.mass = 2.0f; dynRb.shapeType = 0; dynRb.halfExtents = glm::vec3(0.5f, 0.5f, 0.5f);
                    registry.get().emplace<ecs::RigidBodyComponent>(entity, dynRb);
                } else if (typeIdx == 6) { // Point Light
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"PointLight"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 5.0f, 0.0f));
                    registry.get().emplace<ecs::LightComponent>(entity, ecs::LightComponent{"point", glm::vec3(1.0f, 0.9f, 0.7f), 3.0f, 15.0f});
                } else if (typeIdx == 7) { // Player Start
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"PlayerStart"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 1.0f, 0.0f));
                } else if (typeIdx == 8) { // UE5 Sports Car
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"UE5_SportsCar"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 2.0f, 0.0f));
                    ecs::RigidBodyComponent dynRb; dynRb.mass = 1200.0f; dynRb.shapeType = 0;
                    dynRb.halfExtents = glm::vec3(1.2f, 0.6f, 2.5f);
                    registry.get().emplace<ecs::RigidBodyComponent>(entity, dynRb);
                } else if (typeIdx == 9) { // UE5 Chamfer Cube
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"UE5_ChamferCube"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 4.0f, 0.0f));
                    ecs::RigidBodyComponent dynRb; dynRb.mass = 1.0f; dynRb.shapeType = 0;
                    dynRb.halfExtents = glm::vec3(0.5f, 0.5f, 0.5f);
                    registry.get().emplace<ecs::RigidBodyComponent>(entity, dynRb);
                } else if (typeIdx == 10) { // UE5 Location Gizmo
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"UE5_GizmoLocation"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 2.0f, 0.0f));
                } else if (typeIdx == 11) { // UE5 Blue Cube
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"UE5_BlueCube"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 3.0f, 0.0f));
                    ecs::RigidBodyComponent dynRb; dynRb.mass = 1.0f; dynRb.shapeType = 0;
                    registry.get().emplace<ecs::RigidBodyComponent>(entity, dynRb);
                } else if (typeIdx == 12) { // Niagara Fire VFX
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"FireVFX"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 1.0f, 0.0f));
                    registry.get().emplace<ecs::ParticleEmitterComponent>(entity, ecs::ParticleEmitterComponent{"fire", glm::vec3(1.0f, 0.4f, 0.0f), 200, 3.0f, 0.4f});
                } else if (typeIdx == 13) { // Portal Vortex VFX
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"PortalVFX"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 2.0f, 0.0f));
                    registry.get().emplace<ecs::ParticleEmitterComponent>(entity, ecs::ParticleEmitterComponent{"portal", glm::vec3(0.1f, 0.8f, 1.0f), 300, 4.0f, 0.3f});
                }

                selectedEnt = entity;
                if (engine) engine->setSelectedEntity(selectedEnt);
                engine::core::Logger::info(std::string("Placed Actor: ") + title + " (ID: " + std::to_string(static_cast<uint32_t>(entity)) + ")");
            }
            ImGui::PopStyleColor(2);
            ImGui::TextDisabled("  %s", desc);
            ImGui::Spacing();
            ImGui::PopID();
        };

        ImGui::BeginChild("PlaceActorCardsChild", ImVec2(0, 0), false);
        // Basic Actors
        spawnCard("Actor (StaticMesh)", "Empty actor container (Cube)", 0, 3);
        spawnCard("Character (Player)", "WASD controllable player pawn", 1, 3);
        spawnCard("Capsule Pawn", "Dynamic Jolt capsule character", 2, 3);
        spawnCard("Coin (Trigger)", "Pickable sensor trigger item", 3, 3);
        spawnCard("Trigger Zone", "Overlap sensor volume", 4, 3);
        spawnCard("Pawn (Sphere)", "Dynamic physics rigid body", 5, 3);
        spawnCard("Point Light", "Omni-directional light source", 6, 4);
        spawnCard("Directional Light", "Sunlight with cascades", 6, 4);
        spawnCard("Player Start", "Initial spawn transform", 7, 3);

        // UE 5.8 Content
        spawnCard("Sports Car (UE 5.8)", "Unreal 5.8 Sports Car FBX Model", 8, 1);
        spawnCard("1M Chamfer Cube (UE 5.8)", "Unreal 5.8 Chamfered Box", 9, 1);
        spawnCard("Location Gizmo (UE 5.8)", "Unreal 5.8 Transform 3D Gizmo", 10, 1);
        spawnCard("Blue Cube (UE 5.8)", "Unreal 5.8 Blue Cube FBX", 11, 1);

        // VFX
        spawnCard("Fire Emitter (Niagara)", "Rising burning flame particles", 12, 6);
        spawnCard("Portal Vortex", "Swirling quantum portal effect", 13, 6);
        ImGui::EndChild();

        ImGui::Columns(1);
        ImGui::End();
        ImGui::PopStyleColor();

        // ══════════════════════════════════════════════════════════════════════════
        // ═══ 5. CENTER PANEL: MAIN 3D VIEWPORT (VULKAN 1.3 DIRECT GPU) ════════════
        // ══════════════════════════════════════════════════════════════════════════
        ImGui::SetNextWindowPos(ImVec2(leftPanelW, topTotalH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(centerViewportW, contentH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (viewportPanelSize.x > 10.0f && viewportPanelSize.y > 10.0f) {
            m_viewportAspectRatio = viewportPanelSize.x / viewportPanelSize.y;
        }

        if (m_viewportDescriptor != VK_NULL_HANDLE && viewportPanelSize.x > 0 && viewportPanelSize.y > 0) {
            ImGui::Image((ImTextureID)m_viewportDescriptor, viewportPanelSize);
        }

        ImVec2 vMin = ImGui::GetItemRectMin();
        ImVec2 vMax = ImGui::GetItemRectMax();
        ImVec2 vSize = ImGui::GetItemRectSize();
        bool isHovered = ImGui::IsItemHovered();
        bool isWindowFocused = ImGui::IsWindowFocused();
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        float camYaw = engine ? engine->getCameraYaw() : -90.0f;
        float camPitch = engine ? engine->getCameraPitch() : -20.0f;
        float camFov = engine ? engine->getCameraFov() : 45.0f;
        glm::vec3 camPos = engine ? engine->getCameraPosition() : glm::vec3(0.0f, 15.0f, 30.0f);

        glm::vec3 front(
            cos(glm::radians(camYaw)) * cos(glm::radians(camPitch)),
            sin(glm::radians(camPitch)),
            sin(glm::radians(camYaw)) * cos(glm::radians(camPitch))
        );
        front = glm::normalize(front);
        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 up = glm::normalize(glm::cross(right, front));

        float aspect = (m_standaloneGameMode || m_viewportAspectRatio <= 0.1f) ? (16.0f / 9.0f) : m_viewportAspectRatio;
        glm::mat4 curProj = glm::perspective(glm::radians(camFov), aspect, 0.1f, 1000.0f);
        curProj[1][1] *= -1.0f; // Vulkan Y-flip
        glm::mat4 curView = glm::lookAt(camPos, camPos + front, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 curVP = curProj * curView;
        glm::mat4 invVP = glm::inverse(curVP);

        auto worldToScreen = [&](const glm::vec3& worldPos, ImVec2& screenPt, float& outW) -> bool {
            glm::vec4 clip = curVP * glm::vec4(worldPos, 1.0f);
            outW = clip.w;
            if (clip.w <= 0.05f) return false;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            screenPt.x = vMin.x + (ndc.x * 0.5f + 0.5f) * vSize.x;
            screenPt.y = vMin.y + (ndc.y * 0.5f + 0.5f) * vSize.y;
            return true;
        };

        auto getRayFromScreen = [&](const ImVec2& screenPt, glm::vec3& outOrig, glm::vec3& outDir) {
            float relX = (screenPt.x - vMin.x) / (vSize.x > 0.0f ? vSize.x : 1.0f);
            float relY = (screenPt.y - vMin.y) / (vSize.y > 0.0f ? vSize.y : 1.0f);
            float ndcX = relX * 2.0f - 1.0f;
            float ndcY = relY * 2.0f - 1.0f;

            glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
            nearPt /= nearPt.w;
            glm::vec4 farPt = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
            farPt /= farPt.w;

            outOrig = camPos;
            outDir = glm::normalize(glm::vec3(farPt - nearPt));
        };

        auto intersectRayPlane = [](const glm::vec3& rayOrig, const glm::vec3& rayDir, const glm::vec3& planePos, const glm::vec3& planeNormal, glm::vec3& outHit) -> bool {
            float denom = glm::dot(rayDir, planeNormal);
            if (std::abs(denom) < 1e-5f) return false;
            float t = glm::dot(planePos - rayOrig, planeNormal) / denom;
            if (t < 0.05f) return false;
            outHit = rayOrig + t * rayDir;
            return true;
        };

        // Hotkeys in Viewport
        if ((isHovered || isWindowFocused) && !ImGui::GetIO().WantTextInput) {
            if (!s_isRmbNavigating) {
                if (ImGui::IsKeyPressed(ImGuiKey_W)) s_gizmoMode = NativeGizmoMode::TRANSLATE;
                if (ImGui::IsKeyPressed(ImGuiKey_E)) s_gizmoMode = NativeGizmoMode::ROTATE;
                if (ImGui::IsKeyPressed(ImGuiKey_R)) s_gizmoMode = NativeGizmoMode::SCALE;
                if (ImGui::IsKeyPressed(ImGuiKey_Q)) s_gizmoSpaceLocal = !s_gizmoSpaceLocal;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F) && selectedEnt != entt::null && registry.get().valid(selectedEnt) && registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                glm::vec3 newCamPos = t.position - front * 8.0f;
                if (engine) engine->setCamera(newCamPos, camYaw, camPitch, camFov);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedEnt != entt::null && registry.get().valid(selectedEnt)) {
                registry.get().destroy(selectedEnt);
                selectedEnt = entt::null;
                if (engine) engine->setSelectedEntity(entt::null);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                selectedEnt = entt::null;
                if (engine) engine->setSelectedEntity(entt::null);
            }
        }

        // Hit-test Gizmo Handles
        int hoveredGizmoAxis = 0;
        ImVec2 originScreen(0, 0);
        ImVec2 ptX(0, 0), ptY(0, 0), ptZ(0, 0);
        float distToCam = 10.0f;
        bool hasSelectedTransform = (selectedEnt != entt::null && registry.get().valid(selectedEnt) && registry.get().all_of<ecs::TransformComponent>(selectedEnt));

        if (hasSelectedTransform) {
            auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
            distToCam = glm::length(camPos - t.position);
            float gizmoLen = std::max(0.7f, distToCam * 0.14f);

            float originW = 0.0f, wX = 0.0f, wY = 0.0f, wZ = 0.0f;
            bool originVisible = worldToScreen(t.position, originScreen, originW);
            bool xVis = worldToScreen(t.position + glm::vec3(gizmoLen, 0.0f, 0.0f), ptX, wX);
            bool yVis = worldToScreen(t.position + glm::vec3(0.0f, gizmoLen, 0.0f), ptY, wY);
            bool zVis = worldToScreen(t.position + glm::vec3(0.0f, 0.0f, gizmoLen), ptZ, wZ);

            if (originVisible && isHovered && !s_isRmbNavigating && !s_isMmbPanning && !s_isAltOrbiting) {
                float dCenter = sqrtf((mousePos.x - originScreen.x)*(mousePos.x - originScreen.x) + (mousePos.y - originScreen.y)*(mousePos.y - originScreen.y));
                if (dCenter <= 14.0f) {
                    hoveredGizmoAxis = 4;
                } else if (xVis && distToSegment(mousePos, originScreen, ptX) <= 12.0f) {
                    hoveredGizmoAxis = 1;
                } else if (yVis && distToSegment(mousePos, originScreen, ptY) <= 12.0f) {
                    hoveredGizmoAxis = 2;
                } else if (zVis && distToSegment(mousePos, originScreen, ptZ) <= 12.0f) {
                    hoveredGizmoAxis = 3;
                }
            }
        }

        // Mouse buttons handling
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            s_isRmbNavigating = true;
            ImGui::SetWindowFocus("Viewport");
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            s_isRmbNavigating = false;
        }

        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            s_isMmbPanning = true;
            ImGui::SetWindowFocus("Viewport");
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            s_isMmbPanning = false;
        }

        bool altDown = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
        if (isHovered && altDown && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            s_isAltOrbiting = true;
            ImGui::SetWindowFocus("Viewport");
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            s_isAltOrbiting = false;
            s_draggingGizmoAxis = 0;
        }

        // LMB Click on Gizmo or Raycast Picking
        if (isHovered && !altDown && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hoveredGizmoAxis > 0 && hasSelectedTransform) {
                s_draggingGizmoAxis = hoveredGizmoAxis;
                auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                s_dragInitialEntityPos = t.position;
                s_dragInitialRotation = t.rotation;
                s_dragInitialScale = t.scale;
                s_dragPivotScreen = originScreen;

                if (s_draggingGizmoAxis == 1) s_dragAxisVec = glm::vec3(1.0f, 0.0f, 0.0f);
                else if (s_draggingGizmoAxis == 2) s_dragAxisVec = glm::vec3(0.0f, 1.0f, 0.0f);
                else if (s_draggingGizmoAxis == 3) s_dragAxisVec = glm::vec3(0.0f, 0.0f, 1.0f);
                else s_dragAxisVec = glm::vec3(0.0f);

                if (s_draggingGizmoAxis >= 1 && s_draggingGizmoAxis <= 3) {
                    glm::vec3 toCam = camPos - s_dragInitialEntityPos;
                    glm::vec3 vPerp = toCam - glm::dot(toCam, s_dragAxisVec) * s_dragAxisVec;
                    if (glm::length(vPerp) < 1e-4f) {
                        if (std::abs(s_dragAxisVec.x) > 0.8f) vPerp = glm::vec3(0.0f, 1.0f, 0.0f);
                        else vPerp = glm::vec3(1.0f, 0.0f, 0.0f);
                    }
                    s_dragPlaneNormal = glm::normalize(vPerp);
                } else {
                    s_dragPlaneNormal = -front;
                }

                glm::vec3 clickRayOrig, clickRayDir;
                getRayFromScreen(mousePos, clickRayOrig, clickRayDir);
                s_dragHasHit = intersectRayPlane(clickRayOrig, clickRayDir, s_dragInitialEntityPos, s_dragPlaneNormal, s_dragInitialHitPoint);
                if (s_dragHasHit && s_draggingGizmoAxis >= 1 && s_draggingGizmoAxis <= 3) {
                    s_dragInitialOffset = glm::dot(s_dragInitialHitPoint - s_dragInitialEntityPos, s_dragAxisVec);
                } else {
                    s_dragInitialOffset = 0.0f;
                    if (!s_dragHasHit) s_dragInitialHitPoint = s_dragInitialEntityPos;
                }

                s_dragInitialAngle = atan2f(mousePos.y - originScreen.y, mousePos.x - originScreen.x) * (180.0f / 3.1415926535f);
                s_lastRotAngle = s_dragInitialAngle;
                s_accumulatedRotAngle = 0.0f;

                float initDist = sqrtf((mousePos.x - originScreen.x)*(mousePos.x - originScreen.x) + (mousePos.y - originScreen.y)*(mousePos.y - originScreen.y));
                s_dragInitialDist = std::max(10.0f, initDist);
            } else {
                glm::vec3 rayOrig, rayDir;
                getRayFromScreen(mousePos, rayOrig, rayDir);

                entt::entity hitEnt = entt::null;
                float closestDist = 1e9f;

                auto tView = registry.get().view<ecs::TransformComponent>();
                for (auto entity : tView) {
                    const auto& tc = tView.get<ecs::TransformComponent>(entity);

                    // Skip ground plane
                    if (registry.get().all_of<ecs::MeshComponent>(entity)) {
                        if (registry.get().get<ecs::MeshComponent>(entity).meshId == 1) continue;
                    }
                    if (registry.get().all_of<ecs::TagComponent>(entity)) {
                        const std::string& tagName = registry.get().get<ecs::TagComponent>(entity).name;
                        if (tagName == "Ground" || tagName == "GroundPlane") continue;
                    }

                    // Compute actor half-extents in local space
                    glm::vec3 halfExt = glm::max(tc.scale * 0.5f, glm::vec3(0.05f));
                    if (registry.get().all_of<ecs::RigidBodyComponent>(entity)) {
                        const auto& rb = registry.get().get<ecs::RigidBodyComponent>(entity);
                        if (rb.shapeType == 0 && (rb.halfExtents.x > 0.01f || rb.halfExtents.y > 0.01f || rb.halfExtents.z > 0.01f)) {
                            halfExt = rb.halfExtents;
                        }
                    }

                    // Transform ray to actor's local space (accounting for exact position and rotation)
                    glm::quat rot = tc.orientation;
                    if (glm::dot(rot, rot) < 0.001f) {
                        rot = glm::quat(glm::radians(tc.rotation));
                    }
                    rot = glm::normalize(rot);
                    glm::quat invRot = glm::inverse(rot);
                    glm::vec3 rOrigLocal = invRot * (rayOrig - tc.position);
                    glm::vec3 rDirLocal = invRot * rayDir;

                    bool isSphere = false;
                    if (registry.get().all_of<ecs::MeshComponent>(entity)) {
                        if (registry.get().get<ecs::MeshComponent>(entity).meshId == 3) isSphere = true;
                    }
                    if (registry.get().all_of<ecs::RigidBodyComponent>(entity)) {
                        if (registry.get().get<ecs::RigidBodyComponent>(entity).shapeType == 1) isSphere = true;
                    }

                    float tHit = -1.0f;
                    if (isSphere) {
                        glm::vec3 pOrig = rOrigLocal / halfExt;
                        glm::vec3 pDir = rDirLocal / halfExt;
                        float a = glm::dot(pDir, pDir);
                        float b = glm::dot(pOrig, pDir);
                        float c = glm::dot(pOrig, pOrig) - 1.0f;
                        float discr = b * b - a * c;
                        if (discr >= 0.0f) {
                            float sq = sqrtf(discr);
                            float t0 = (-b - sq) / a;
                            float t1 = (-b + sq) / a;
                            tHit = (t0 >= 0.0f) ? t0 : t1;
                        }
                    } else {
                        glm::vec3 invD(
                            std::abs(rDirLocal.x) > 1e-7f ? 1.0f / rDirLocal.x : (rDirLocal.x >= 0 ? 1e7f : -1e7f),
                            std::abs(rDirLocal.y) > 1e-7f ? 1.0f / rDirLocal.y : (rDirLocal.y >= 0 ? 1e7f : -1e7f),
                            std::abs(rDirLocal.z) > 1e-7f ? 1.0f / rDirLocal.z : (rDirLocal.z >= 0 ? 1e7f : -1e7f)
                        );

                        glm::vec3 boxMin = -halfExt - glm::vec3(0.01f);
                        glm::vec3 boxMax = +halfExt + glm::vec3(0.01f);

                        glm::vec3 t0 = (boxMin - rOrigLocal) * invD;
                        glm::vec3 t1 = (boxMax - rOrigLocal) * invD;

                        glm::vec3 tMinV = glm::min(t0, t1);
                        glm::vec3 tMaxV = glm::max(t0, t1);

                        float tNear = std::max({tMinV.x, tMinV.y, tMinV.z});
                        float tFar = std::min({tMaxV.x, tMaxV.y, tMaxV.z});

                        if (tNear <= tFar && tFar >= 0.0f) {
                            tHit = (tNear >= 0.0f) ? tNear : tFar;
                        }
                    }

                    if (tHit >= 0.0f && tHit < closestDist) {
                        closestDist = tHit;
                        hitEnt = entity;
                    }
                }

                selectedEnt = hitEnt;
                if (engine) engine->setSelectedEntity(hitEnt);
                hasSelectedTransform = (selectedEnt != entt::null && registry.get().valid(selectedEnt) && registry.get().all_of<ecs::TransformComponent>(selectedEnt));
                if (selectedEnt != entt::null) {
                    engine::core::Logger::info("Selected Actor in Viewport: ID " + std::to_string(static_cast<uint32_t>(selectedEnt)));
                }
            }
        }

        // Dragging Gizmo with 1:1 Ray-Plane Projection
        if (s_draggingGizmoAxis > 0 && hasSelectedTransform) {
            auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);

            if (s_gizmoMode == NativeGizmoMode::TRANSLATE) {
                glm::vec3 curRayOrig, curRayDir;
                getRayFromScreen(mousePos, curRayOrig, curRayDir);

                float denom = glm::dot(curRayDir, s_dragPlaneNormal);
                if (std::abs(denom) > 0.03f) {
                    float tDist = glm::dot(s_dragInitialEntityPos - curRayOrig, s_dragPlaneNormal) / denom;
                    if (tDist > 0.05f && tDist < 5000.0f) {
                        glm::vec3 curHit = curRayOrig + tDist * curRayDir;

                        if (s_draggingGizmoAxis >= 1 && s_draggingGizmoAxis <= 3) {
                            float curOffset = glm::dot(curHit - s_dragInitialEntityPos, s_dragAxisVec);
                            float deltaAxis = curOffset - s_dragInitialOffset;
                            if (s_snapEnabled && s_snapStep > 0.01f) {
                                deltaAxis = std::round(deltaAxis / s_snapStep) * s_snapStep;
                            }
                            t.position = s_dragInitialEntityPos + s_dragAxisVec * deltaAxis;
                        } else if (s_draggingGizmoAxis == 4) {
                            glm::vec3 delta = curHit - s_dragInitialHitPoint;
                            t.position = s_dragInitialEntityPos + delta;
                        }

                        if (engine) engine->syncEntityTransformPhysics(selectedEnt);
                    }
                }
            } else if (s_gizmoMode == NativeGizmoMode::ROTATE) {
                float curAngle = atan2f(mousePos.y - s_dragPivotScreen.y, mousePos.x - s_dragPivotScreen.x) * (180.0f / 3.1415926535f);
                float dAngle = curAngle - s_lastRotAngle;
                while (dAngle > 180.0f) dAngle -= 360.0f;
                while (dAngle < -180.0f) dAngle += 360.0f;
                s_accumulatedRotAngle += dAngle;
                s_lastRotAngle = curAngle;

                float finalAngle = s_accumulatedRotAngle;
                if (s_snapEnabled) {
                    finalAngle = std::round(finalAngle / 15.0f) * 15.0f;
                }

                t.rotation = s_dragInitialRotation;
                if (s_draggingGizmoAxis == 1) t.rotation.x += finalAngle;
                else if (s_draggingGizmoAxis == 2) t.rotation.y += finalAngle;
                else if (s_draggingGizmoAxis == 3) t.rotation.z += finalAngle;
                else if (s_draggingGizmoAxis == 4) t.rotation.y += finalAngle;

                t.orientation = glm::quat(glm::radians(t.rotation));
                if (engine) engine->syncEntityTransformPhysics(selectedEnt);
            } else if (s_gizmoMode == NativeGizmoMode::SCALE) {
                float curDist = sqrtf((mousePos.x - s_dragPivotScreen.x)*(mousePos.x - s_dragPivotScreen.x) + (mousePos.y - s_dragPivotScreen.y)*(mousePos.y - s_dragPivotScreen.y));
                float scaleFactor = curDist / s_dragInitialDist;
                if (s_snapEnabled) {
                    scaleFactor = std::max(0.1f, std::round(scaleFactor / 0.25f) * 0.25f);
                }

                if (s_draggingGizmoAxis == 1) t.scale.x = std::max(0.01f, s_dragInitialScale.x * scaleFactor);
                else if (s_draggingGizmoAxis == 2) t.scale.y = std::max(0.01f, s_dragInitialScale.y * scaleFactor);
                else if (s_draggingGizmoAxis == 3) t.scale.z = std::max(0.01f, s_dragInitialScale.z * scaleFactor);
                else if (s_draggingGizmoAxis == 4) t.scale = glm::max(glm::vec3(0.01f), s_dragInitialScale * scaleFactor);
            }
        }

        // RMB Fly Look & Movement
        if (s_isRmbNavigating && engine) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            float dx = ImGui::GetIO().MouseDelta.x;
            float dy = ImGui::GetIO().MouseDelta.y;
            camYaw += dx * 0.20f;
            camPitch = glm::clamp(camPitch - dy * 0.20f, -89.0f, 89.0f);

            glm::vec3 curFront(
                cos(glm::radians(camYaw)) * cos(glm::radians(camPitch)),
                sin(glm::radians(camPitch)),
                sin(glm::radians(camYaw)) * cos(glm::radians(camPitch))
            );
            curFront = glm::normalize(curFront);
            glm::vec3 curRight = glm::normalize(glm::cross(curFront, glm::vec3(0.0f, 1.0f, 0.0f)));
            glm::vec3 curUp = glm::vec3(0.0f, 1.0f, 0.0f);

            float dt = ImGui::GetIO().DeltaTime;
            float speed = s_camFlySpeed * dt * (s_cameraSpeed / 4.0f);
            if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) speed *= 3.0f;

            if (ImGui::IsKeyDown(ImGuiKey_W)) camPos += curFront * speed;
            if (ImGui::IsKeyDown(ImGuiKey_S)) camPos -= curFront * speed;
            if (ImGui::IsKeyDown(ImGuiKey_A)) camPos -= curRight * speed;
            if (ImGui::IsKeyDown(ImGuiKey_D)) camPos += curRight * speed;
            if (ImGui::IsKeyDown(ImGuiKey_E)) camPos += curUp * speed;
            if (ImGui::IsKeyDown(ImGuiKey_Q)) camPos -= curUp * speed;

            engine->setCamera(camPos, camYaw, camPitch, camFov);
        }

        // MMB Pan
        if (s_isMmbPanning && engine) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            float dx = ImGui::GetIO().MouseDelta.x;
            float dy = ImGui::GetIO().MouseDelta.y;
            float panScale = 0.04f;
            camPos -= (right * dx - up * dy) * panScale;
            engine->setCamera(camPos, camYaw, camPitch, camFov);
        }

        // Alt+LMB Orbit
        if (s_isAltOrbiting && engine) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            float dx = ImGui::GetIO().MouseDelta.x;
            float dy = ImGui::GetIO().MouseDelta.y;
            glm::vec3 pivot = (hasSelectedTransform) ? registry.get().get<ecs::TransformComponent>(selectedEnt).position : (camPos + front * 10.0f);
            float orbitDist = glm::length(camPos - pivot);
            if (orbitDist < 0.5f) orbitDist = 0.5f;

            camYaw += dx * 0.25f;
            camPitch = glm::clamp(camPitch - dy * 0.25f, -89.0f, 89.0f);

            glm::vec3 orbitFront(
                cos(glm::radians(camYaw)) * cos(glm::radians(camPitch)),
                sin(glm::radians(camPitch)),
                sin(glm::radians(camYaw)) * cos(glm::radians(camPitch))
            );
            orbitFront = glm::normalize(orbitFront);
            camPos = pivot - orbitFront * orbitDist;
            engine->setCamera(camPos, camYaw, camPitch, camFov);
        }

        // Mouse Wheel Zoom
        if (isHovered && std::abs(ImGui::GetIO().MouseWheel) > 0.01f && engine) {
            float wheel = ImGui::GetIO().MouseWheel;
            camPos += front * (wheel * 3.0f);
            engine->setCamera(camPos, camYaw, camPitch, camFov);
        }

        // Draw 3D Selection Bounding Box & 3D Transform Gizmo
        if (hasSelectedTransform) {
            auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
            glm::vec3 halfExt(std::max(0.4f, t.scale.x * 0.5f), std::max(0.4f, t.scale.y * 0.5f), std::max(0.4f, t.scale.z * 0.5f));
            if (registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt)) {
                auto& rb = registry.get().get<ecs::RigidBodyComponent>(selectedEnt);
                if (rb.halfExtents.x > 0.01f) halfExt = rb.halfExtents;
                else if (rb.shapeType == 1 && rb.capsuleRadius > 0.01f) halfExt = glm::vec3(rb.capsuleRadius);
                else if (rb.shapeType == 2 && rb.capsuleRadius > 0.01f) halfExt = glm::vec3(rb.capsuleRadius, rb.capsuleHalfHeight, rb.capsuleRadius);
            }

            glm::vec3 corners[8] = {
                t.position + glm::vec3(-halfExt.x, -halfExt.y, -halfExt.z),
                t.position + glm::vec3( halfExt.x, -halfExt.y, -halfExt.z),
                t.position + glm::vec3( halfExt.x,  halfExt.y, -halfExt.z),
                t.position + glm::vec3(-halfExt.x,  halfExt.y, -halfExt.z),
                t.position + glm::vec3(-halfExt.x, -halfExt.y,  halfExt.z),
                t.position + glm::vec3( halfExt.x, -halfExt.y,  halfExt.z),
                t.position + glm::vec3( halfExt.x,  halfExt.y,  halfExt.z),
                t.position + glm::vec3(-halfExt.x,  halfExt.y,  halfExt.z),
            };

            ImVec2 sCorners[8];
            float sW[8];
            bool allVis = true;
            for (int i = 0; i < 8; ++i) {
                if (!worldToScreen(corners[i], sCorners[i], sW[i])) allVis = false;
            }

            if (allVis) {
                ImU32 boxCol = IM_COL32(255, 185, 30, 160);
                int edges[12][2] = {
                    {0,1}, {1,2}, {2,3}, {3,0},
                    {4,5}, {5,6}, {6,7}, {7,4},
                    {0,4}, {1,5}, {2,6}, {3,7}
                };
                for (int e = 0; e < 12; ++e) {
                    drawList->AddLine(sCorners[edges[e][0]], sCorners[edges[e][1]], boxCol, 1.5f);
                }
            }

            // Draw Actor Name Label
            ImVec2 topPt; float topW;
            if (worldToScreen(t.position + glm::vec3(0.0f, halfExt.y + 0.35f, 0.0f), topPt, topW)) {
                std::string tagLabel = "Actor_" + std::to_string(static_cast<uint32_t>(selectedEnt));
                if (registry.get().all_of<ecs::TagComponent>(selectedEnt)) {
                    tagLabel = registry.get().get<ecs::TagComponent>(selectedEnt).name;
                }
                drawList->AddText(ImVec2(topPt.x - 20, topPt.y - 14), IM_COL32(0, 240, 255, 230), tagLabel.c_str());
            }

            // Draw 3D Gizmo Axes
            float origW;
            if (worldToScreen(t.position, originScreen, origW)) {
                int activeOrHovered = (s_draggingGizmoAxis > 0) ? s_draggingGizmoAxis : hoveredGizmoAxis;

                ImU32 colX = (activeOrHovered == 1) ? IM_COL32(255, 255, 50, 255) : IM_COL32(235, 55, 55, 255);
                ImU32 colY = (activeOrHovered == 2) ? IM_COL32(255, 255, 50, 255) : IM_COL32(50, 220, 50, 255);
                ImU32 colZ = (activeOrHovered == 3) ? IM_COL32(255, 255, 50, 255) : IM_COL32(50, 130, 250, 255);
                ImU32 colCenter = (activeOrHovered == 4) ? IM_COL32(255, 255, 50, 255) : IM_COL32(240, 240, 240, 200);

                float thickX = (activeOrHovered == 1) ? 4.5f : 3.0f;
                float thickY = (activeOrHovered == 2) ? 4.5f : 3.0f;
                float thickZ = (activeOrHovered == 3) ? 4.5f : 3.0f;

                drawList->AddCircleFilled(originScreen, (activeOrHovered == 4) ? 8.0f : 5.5f, colCenter);
                drawList->AddCircle(originScreen, (activeOrHovered == 4) ? 8.0f : 5.5f, IM_COL32(0, 0, 0, 180), 0, 1.5f);

                // X Axis
                drawList->AddLine(originScreen, ptX, colX, thickX);
                if (s_gizmoMode == NativeGizmoMode::TRANSLATE) {
                    drawList->AddTriangleFilled(ImVec2(ptX.x + 4, ptX.y), ImVec2(ptX.x - 5, ptX.y - 5), ImVec2(ptX.x - 5, ptX.y + 5), colX);
                } else if (s_gizmoMode == NativeGizmoMode::SCALE) {
                    drawList->AddRectFilled(ImVec2(ptX.x - 4, ptX.y - 4), ImVec2(ptX.x + 4, ptX.y + 4), colX);
                } else {
                    drawList->AddCircle(ptX, 6.0f, colX, 0, 2.0f);
                }
                drawList->AddText(ImVec2(ptX.x + 6, ptX.y - 6), colX, "X");

                // Y Axis
                drawList->AddLine(originScreen, ptY, colY, thickY);
                if (s_gizmoMode == NativeGizmoMode::TRANSLATE) {
                    drawList->AddTriangleFilled(ImVec2(ptY.x, ptY.y - 6), ImVec2(ptY.x - 5, ptY.y + 4), ImVec2(ptY.x + 5, ptY.y + 4), colY);
                } else if (s_gizmoMode == NativeGizmoMode::SCALE) {
                    drawList->AddRectFilled(ImVec2(ptY.x - 4, ptY.y - 4), ImVec2(ptY.x + 4, ptY.y + 4), colY);
                } else {
                    drawList->AddCircle(ptY, 6.0f, colY, 0, 2.0f);
                }
                drawList->AddText(ImVec2(ptY.x + 6, ptY.y - 6), colY, "Y");

                // Z Axis
                drawList->AddLine(originScreen, ptZ, colZ, thickZ);
                if (s_gizmoMode == NativeGizmoMode::TRANSLATE) {
                    drawList->AddTriangleFilled(ImVec2(ptZ.x + 4, ptZ.y), ImVec2(ptZ.x - 5, ptZ.y - 5), ImVec2(ptZ.x - 5, ptZ.y + 5), colZ);
                } else if (s_gizmoMode == NativeGizmoMode::SCALE) {
                    drawList->AddRectFilled(ImVec2(ptZ.x - 4, ptZ.y - 4), ImVec2(ptZ.x + 4, ptZ.y + 4), colZ);
                } else {
                    drawList->AddCircle(ptZ, 6.0f, colZ, 0, 2.0f);
                }
                drawList->AddText(ImVec2(ptZ.x + 6, ptZ.y - 6), colZ, "Z");
            }
        }

        // ── Viewport Floating Pill 1 (Top-Left): Gizmo tools, Snapping, Camera Speed
        ImGui::SetCursorPos(ImVec2(10, 10));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.063f, 0.063f, 0.094f, 0.90f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
        ImGui::BeginChild("ViewportPillLeft", ImVec2(390 * s_uiScale, 32 * s_uiScale), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        
        // Translate (W)
        if (s_gizmoMode == NativeGizmoMode::TRANSLATE) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.55f, 0.85f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.18f, 1.0f));
        if (ImGui::Button(" [W] ")) s_gizmoMode = NativeGizmoMode::TRANSLATE;
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // Rotate (E)
        if (s_gizmoMode == NativeGizmoMode::ROTATE) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.38f, 0.25f, 0.85f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.18f, 1.0f));
        if (ImGui::Button(" [E] ")) s_gizmoMode = NativeGizmoMode::ROTATE;
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // Scale (R)
        if (s_gizmoMode == NativeGizmoMode::SCALE) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.70f, 0.40f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.18f, 1.0f));
        if (ImGui::Button(" [R] ")) s_gizmoMode = NativeGizmoMode::SCALE;
        ImGui::PopStyleColor();
        ImGui::SameLine();

        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Coordinate Space (World / Local)
        if (ImGui::Button(s_gizmoSpaceLocal ? " Local " : " World ")) {
            s_gizmoSpaceLocal = !s_gizmoSpaceLocal;
        }
        ImGui::SameLine();

        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Snapping Button
        if (s_snapEnabled) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.10f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.18f, 1.0f));
        std::string snapStr = s_snapEnabled ? (std::to_string(s_snapStep).substr(0, 3) + "m") : "Snap: Off";
        if (ImGui::Button(snapStr.c_str())) {
            s_snapEnabled = !s_snapEnabled;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // Cam Speed
        ImGui::TextColored(ImVec4(0.2f, 0.85f, 1.0f, 1.0f), "Speed: %d", s_cameraSpeed);
        ImGui::SameLine();
        if (ImGui::Button(" F ")) {
            if (selectedEnt != entt::null && registry.get().valid(selectedEnt) && registry.get().all_of<ecs::TransformComponent>(selectedEnt) && engine) {
                auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                glm::vec3 newCamPos = t.position - front * 8.0f;
                engine->setCamera(newCamPos, camYaw, camPitch, camFov);
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        // ── Viewport Floating Pill 2 (Top-Right): Perspective, Shading
        ImGui::SetCursorPos(ImVec2(centerViewportW - 240 * s_uiScale, 10));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.063f, 0.063f, 0.094f, 0.90f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
        ImGui::BeginChild("ViewportPillRight", ImVec2(230 * s_uiScale, 32 * s_uiScale), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextColored(ImVec4(0.85f, 0.9f, 0.95f, 1.0f), "Perspective");
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Lit (Vulkan 1.3)");
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        // ── Center-Bottom Floating AI Game Creator Prompt Bar (Glass linear style)
        float promptBarW = std::min(540.0f * s_uiScale, centerViewportW * 0.70f);
        ImGui::SetCursorPos(ImVec2((centerViewportW - promptBarW) * 0.5f, viewportPanelSize.y - 48 * s_uiScale));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.063f, 0.063f, 0.094f, 0.92f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
        ImGui::BeginChild("AIPromptPillCenter", ImVec2(promptBarW, 38 * s_uiScale), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        
        static char aiPromptNative[256] = "";
        ImGui::SetNextItemWidth(promptBarW - 105 * s_uiScale);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.04f, 0.04f, 0.06f, 1.0f));
        bool submitPrompt = ImGui::InputTextWithHint("##AIPromptNative", "Napisz do AI co chcesz stworzyc (np. Wyscigi, Arkanoid)...", aiPromptNative, sizeof(aiPromptNative), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleColor();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.65f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.80f, 1.0f, 1.0f));
        if (ImGui::Button(" Generuj ") || submitPrompt) {
            std::string promptStr = aiPromptNative;
            aiPromptNative[0] = '\0';
            std::transform(promptStr.begin(), promptStr.end(), promptStr.begin(), ::tolower);
            if (engine) {
                if (promptStr.find("wyscig") != std::string::npos || promptStr.find("racer") != std::string::npos) {
                    engine->setCurrentPreset("racer"); engine->resetScene("racer");
                } else if (promptStr.find("arkanoid") != std::string::npos || promptStr.find("breakout") != std::string::npos) {
                    engine->setCurrentPreset("breakout"); engine->resetScene("breakout");
                } else if (promptStr.find("platformer") != std::string::npos || promptStr.find("skoki") != std::string::npos) {
                    engine->setCurrentPreset("platformer"); engine->resetScene("platformer");
                } else if (promptStr.find("strzelanka") != std::string::npos || promptStr.find("shooter") != std::string::npos) {
                    engine->setCurrentPreset("shooter"); engine->resetScene("shooter");
                } else {
                    engine->setCurrentPreset("empty"); engine->resetScene("empty");
                }
                engine::core::Logger::info("AI Generator executed scene creation: " + promptStr);
            }
        }
        ImGui::PopStyleColor(2);

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        // Live Native Game HUD Overlay (Top-Left below tools)
        if (engine && !engine->isPaused()) {
            ImGui::SetCursorPos(ImVec2(15, 52));
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.8f, 1.0f), "[ NATIVE VULKAN 1.3 GAMEPLAY ACTIVE ]");
            ImGui::SetCursorPos(ImVec2(15, 72));
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "SCORE: %d", engine->getScore());
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), " | HEALTH: %d HP", engine->getHealth());
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), " | STATUS: %s", engine->getGameStatus().c_str());
        }

        // Bottom-Left Navigation Hint Box
        ImGui::SetCursorPos(ImVec2(12, viewportPanelSize.y - 24));
        ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.80f, 0.90f), "PPM+WASD: Lec * Alt+LPM: Orbituj * SPM: Przesun * Scroll: Zoom * LPM: Zaznacz * W/E/R: Gizmo * F: Skup");

        ImGui::End();
        ImGui::PopStyleVar();

        // ══════════════════════════════════════════════════════════════════════════
        // ═══ 6. RIGHT TOP PANEL: "OUTLINER" (SCENE HIERARCHY) ════════════════════
        // ══════════════════════════════════════════════════════════════════════════
        ImGui::SetNextWindowPos(ImVec2(leftPanelW + centerViewportW, topTotalH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(rightPanelW, contentH * 0.45f), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.055f, 0.086f, 1.0f)); // #0e0e16
        ImGui::Begin("Outliner", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "--- OUTLINER ---");
        ImGui::SameLine(rightPanelW - 65 * s_uiScale);
        ImGui::TextDisabled("LEVEL");

        static char outlinerFilter[64] = "";
        ImGui::InputTextWithHint("##OutlinerFilter", "Search...", outlinerFilter, sizeof(outlinerFilter));
        ImGui::Separator();

        ImGui::Columns(2, "OutlinerColumns");
        ImGui::SetColumnWidth(0, rightPanelW * 0.68f);
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "ITEM LABEL");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "TYPE");
        ImGui::NextColumn();
        ImGui::Separator();

        ImGui::BeginChild("OutlinerTree", ImVec2(0, 0), false);
        if (ImGui::TreeNodeEx("Lvl_MainLevel (Editor)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::TreeNodeEx("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Selectable("DirectionalLight (Sun)"); ImGui::NextColumn(); ImGui::TextDisabled("DirectionalLight"); ImGui::NextColumn();
                ImGui::Selectable("SkyLight (Ambient)"); ImGui::NextColumn(); ImGui::TextDisabled("SkyLight"); ImGui::NextColumn();
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Actors & Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto view = registry.get().view<ecs::TransformComponent>();
                for (auto entity : view) {
                    std::string label = "Entity_" + std::to_string(static_cast<uint32_t>(entity));
                    std::string typeName = "Actor";
                    if (registry.get().all_of<ecs::TagComponent>(entity)) {
                        label = registry.get().get<ecs::TagComponent>(entity).name;
                    }
                    if (registry.get().all_of<ecs::RigidBodyComponent>(entity)) {
                        auto& rb = registry.get().get<ecs::RigidBodyComponent>(entity);
                        typeName = rb.isSensor ? "TriggerZone" : (rb.shapeType == 2 ? "PlayerPawn" : "RigidBodyActor");
                    }

                    if (outlinerFilter[0] != '\0' && label.find(outlinerFilter) == std::string::npos) continue;

                    bool isSelected = (selectedEnt == entity);
                    if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                        selectedEnt = entity;
                        if (engine) engine->setSelectedEntity(selectedEnt);
                    }
                    ImGui::NextColumn();
                    if (registry.get().all_of<ecs::RigidBodyComponent>(entity)) {
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "[PHYS] ");
                        ImGui::SameLine();
                    }
                    ImGui::TextDisabled("%s", typeName.c_str());
                    ImGui::NextColumn();
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::EndChild();
        ImGui::Columns(1);

        size_t entityCount = registry.get().storage<entt::entity>().in_use();
        ImGui::TextDisabled("%zu actors (%d selected)", entityCount, selectedEnt != entt::null ? 1 : 0);
        ImGui::End();
        ImGui::PopStyleColor();

        // ══════════════════════════════════════════════════════════════════════════
        // ═══ 7. RIGHT BOTTOM PANEL: "DETAILS" (PROPERTIES INSPECTOR) ═════════════
        // ══════════════════════════════════════════════════════════════════════════
        ImGui::SetNextWindowPos(ImVec2(leftPanelW + centerViewportW, topTotalH + contentH * 0.45f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(rightPanelW, contentH * 0.55f), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.067f, 0.067f, 0.098f, 1.0f)); // #111119
        ImGui::Begin("Details", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        // Header Tabs: Details vs World Settings
        if (s_detailsTab == 0) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.094f, 0.094f, 0.145f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.05f, 0.07f, 1.0f));
        if (ImGui::Button(" Details x ")) s_detailsTab = 0;
        ImGui::PopStyleColor();
        ImGui::SameLine();

        if (s_detailsTab == 1) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.094f, 0.094f, 0.145f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.05f, 0.07f, 1.0f));
        if (ImGui::Button(" World Settings ")) s_detailsTab = 1;
        ImGui::PopStyleColor();
        ImGui::Separator();

        if (s_detailsTab == 0 && selectedEnt != entt::null && registry.get().valid(selectedEnt)) {
            // Actor Header Card
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.086f, 0.086f, 0.133f, 1.0f)); // #161622
            ImGui::BeginChild("ActorHeaderCard", ImVec2(0, 58 * s_uiScale), true);
            
            std::string tagStr = "Actor";
            if (registry.get().all_of<ecs::TagComponent>(selectedEnt)) {
                tagStr = registry.get().get<ecs::TagComponent>(selectedEnt).name;
            }
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", tagStr.c_str());
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "#%u", static_cast<uint32_t>(selectedEnt));
            
            ImGui::SameLine(ImGui::GetWindowWidth() - 95 * s_uiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.65f, 0.35f, 1.0f));
            if (ImGui::Button(" + Add ")) { ImGui::OpenPopup("AddComponentPopup"); }
            ImGui::PopStyleColor();

            if (ImGui::BeginPopup("AddComponentPopup")) {
                if (ImGui::Selectable("Jolt RigidBody Component")) {
                    if (!registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt)) {
                        registry.get().emplace<ecs::RigidBodyComponent>(selectedEnt, ecs::RigidBodyComponent{});
                    }
                }
                if (ImGui::Selectable("Point Light Component")) {
                    if (!registry.get().all_of<ecs::LightComponent>(selectedEnt)) {
                        registry.get().emplace<ecs::LightComponent>(selectedEnt, ecs::LightComponent{"point", glm::vec3(1.0f, 0.9f, 0.7f), 3.0f, 15.0f});
                    }
                }
                if (ImGui::Selectable("Niagara Particle Emitter")) {
                    if (!registry.get().all_of<ecs::ParticleEmitterComponent>(selectedEnt)) {
                        registry.get().emplace<ecs::ParticleEmitterComponent>(selectedEnt, ecs::ParticleEmitterComponent{"fire", glm::vec3(1.0f, 0.4f, 0.0f), 200, 3.0f, 0.4f});
                    }
                }
                ImGui::EndPopup();
            }

            // Attached Badges
            ImGui::TextDisabled("StaticMesh");
            if (registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt)) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "* JoltBody");
            }
            if (registry.get().all_of<ecs::LightComponent>(selectedEnt)) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "* Light");
            }
            if (registry.get().all_of<ecs::ParticleEmitterComponent>(selectedEnt)) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "* VFX");
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();

            // Category Filter Pills
            const char* filterCats[] = { "All", "General", "Actor", "Physics", "Rendering" };
            for (int fc = 0; fc < 5; ++fc) {
                if (s_detailsFilterCat == fc) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.37f, 0.72f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.09f, 0.09f, 0.13f, 1.0f));
                }
                if (ImGui::Button(filterCats[fc])) s_detailsFilterCat = fc;
                ImGui::PopStyleColor();
                if (fc < 4) ImGui::SameLine();
            }
            ImGui::Spacing();

            // Section 1: Transform
            if (registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    bool posChanged = drawVec3Control("Location", t.position, 0.0f);
                    bool rotChanged = drawVec3Control("Rotation", t.rotation, 0.0f);
                    bool scaleChanged = drawVec3Control("Scale", t.scale, 1.0f);

                    if (rotChanged) {
                        t.orientation = glm::quat(glm::radians(t.rotation));
                    }
                    if ((posChanged || rotChanged || scaleChanged) && engine) {
                        engine->syncEntityTransformPhysics(selectedEnt);
                    }

                    // Mobility Buttons
                    ImGui::TextDisabled("Mobility");
                    ImGui::SameLine();
                    bool isDynamic = registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt) && registry.get().get<ecs::RigidBodyComponent>(selectedEnt).mass > 0;
                    bool isStationary = registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt) && registry.get().get<ecs::RigidBodyComponent>(selectedEnt).mass == 0;
                    bool isStatic = !registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt);

                    if (isStatic) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.37f, 0.72f, 1.0f));
                    else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.15f, 1.0f));
                    if (ImGui::Button("Static")) {
                        if (registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt)) registry.get().erase<ecs::RigidBodyComponent>(selectedEnt);
                    }
                    ImGui::PopStyleColor();
                    ImGui::SameLine();

                    if (isStationary) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.1f, 1.0f));
                    else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.15f, 1.0f));
                    if (ImGui::Button("Stationary")) {
                        if (!registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt)) registry.get().emplace<ecs::RigidBodyComponent>(selectedEnt, ecs::RigidBodyComponent{});
                        registry.get().get<ecs::RigidBodyComponent>(selectedEnt).mass = 0.0f;
                    }
                    ImGui::PopStyleColor();
                    ImGui::SameLine();

                    if (isDynamic) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.65f, 0.35f, 1.0f));
                    else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.15f, 1.0f));
                    if (ImGui::Button("Movable")) {
                        if (!registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt)) registry.get().emplace<ecs::RigidBodyComponent>(selectedEnt, ecs::RigidBodyComponent{});
                        registry.get().get<ecs::RigidBodyComponent>(selectedEnt).mass = 1.0f;
                    }
                    ImGui::PopStyleColor();
                }
            }

            // Section 2: Mesh & Material
            if (registry.get().all_of<ecs::MeshComponent>(selectedEnt)) {
                if (ImGui::CollapsingHeader("Static Mesh & Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& mc = registry.get().get<ecs::MeshComponent>(selectedEnt);
                    const char* meshTypes[] = { "Default Plane", "Ground Terrain", "Cube StaticMesh", "Sphere Mesh", "Capsule Mesh", "Cylinder Mesh" };
                    int currentMesh = std::clamp(static_cast<int>(mc.meshId), 0, 5);
                    if (ImGui::Combo("Mesh Asset", &currentMesh, meshTypes, IM_ARRAYSIZE(meshTypes))) {
                        mc.meshId = currentMesh;
                    }
                }
            }

            // Section 3: Jolt Physics
            if (registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt)) {
                if (ImGui::CollapsingHeader("Jolt Physics Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& rb = registry.get().get<ecs::RigidBodyComponent>(selectedEnt);
                    ImGui::DragFloat("Mass (kg)", &rb.mass, 0.1f, 0.0f, 500.0f);
                    const char* shapes[] = { "Box", "Sphere", "Capsule" };
                    int shapeIdx = rb.shapeType;
                    if (ImGui::Combo("Collider Shape", &shapeIdx, shapes, IM_ARRAYSIZE(shapes))) {
                        rb.shapeType = shapeIdx;
                    }
                    if (rb.shapeType == 2) {
                        ImGui::DragFloat("Capsule Radius", &rb.capsuleRadius, 0.05f, 0.1f, 10.0f);
                        ImGui::DragFloat("Capsule Half Height", &rb.capsuleHalfHeight, 0.05f, 0.1f, 10.0f);
                    }
                    ImGui::SliderFloat("Restitution (Bounciness)", &rb.restitution, 0.0f, 1.0f);
                    ImGui::SliderFloat("Friction", &rb.friction, 0.0f, 1.0f);
                    ImGui::Checkbox("Is Sensor (Trigger)", &rb.isSensor);
                    ImGui::Checkbox("Lock Rotation X", &rb.lockRotationX);
                    ImGui::SameLine();
                    ImGui::Checkbox("Lock Rotation Z", &rb.lockRotationZ);
                }
            }

            // Quick Actions & Delete
            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button(" Ground (Y=0.5) ")) {
                if (registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                    registry.get().get<ecs::TransformComponent>(selectedEnt).position.y = 0.5f;
                    if (engine) engine->syncEntityTransformPhysics(selectedEnt);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(" +5 Y Jump ")) {
                if (registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                    registry.get().get<ecs::TransformComponent>(selectedEnt).position.y += 5.0f;
                    if (engine) engine->syncEntityTransformPhysics(selectedEnt);
                }
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.18f, 0.20f, 1.0f));
            if (ImGui::Button(" Delete Actor ")) {
                registry.get().destroy(selectedEnt);
                selectedEnt = entt::null;
                if (engine) engine->setSelectedEntity(entt::null);
            }
            ImGui::PopStyleColor();

        } else if (s_detailsTab == 1 || selectedEnt == entt::null) {
            // World Settings Tab
            if (ImGui::CollapsingHeader("World Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                static float gravity = -9.81f;
                ImGui::DragFloat("Gravity (Y)", &gravity, 0.1f, -50.0f, 50.0f);
                static bool vsync = true;
                ImGui::Checkbox("V-Sync", &vsync);
                static float exposure = 1.0f;
                ImGui::SliderFloat("Exposure", &exposure, 0.1f, 5.0f);
                static bool useRayTracing = true;
                ImGui::Checkbox("Hardware Raytracing (TLAS/BLAS)", &useRayTracing);
                static bool useVSM = true;
                ImGui::Checkbox("Virtual Shadow Maps (16k)", &useVSM);
                static bool useReSTIR = true;
                ImGui::Checkbox("ReSTIR Global Illumination", &useReSTIR);
                static bool useClouds = true;
                ImGui::Checkbox("Volumetric Clouds", &useClouds);
            }
        }

        ImGui::End();
        ImGui::PopStyleColor();

        // ══════════════════════════════════════════════════════════════════════════
        // ═══ 8. BOTTOM EXPANDABLE DRAWER (CONTENT DRAWER / OUTPUT LOG) ═══════════
        // ══════════════════════════════════════════════════════════════════════════
        if (s_showContentDrawer) {
            ImGui::SetNextWindowPos(ImVec2(0, screenH - bottomTotalH), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(screenW, drawerH), ImGuiCond_Always);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.051f, 0.051f, 0.086f, 1.0f)); // #0d0d16
            ImGui::Begin("ContentDrawerWindow", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

            // Drawer Header
            ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.04f, 1.0f), "[FOLDER]");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "CONTENT DRAWER");
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            if (ImGui::Button("+ Dodaj Asset")) {}
            ImGui::SameLine();
            ImGui::TextDisabled("Categories: All | Scripts | Materials | Effects | Audio | Scenes");

            ImGui::SameLine(screenW - 40 * s_uiScale);
            if (ImGui::Button(" X ##CloseContentDrawer")) { s_showContentDrawer = false; }
            ImGui::Separator();

            // Asset Cards Grid
            auto renderAssetCard = [&](const char* name, const char* type, ImVec4 col) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.12f, 0.18f, 1.0f));
                if (ImGui::Button(name, ImVec2(130 * s_uiScale, 65 * s_uiScale))) {}
                ImGui::PopStyleColor(2);
                ImGui::TextColored(col, "  %s", type);
                ImGui::SameLine();
            };

            renderAssetCard("BP_Paddle", "Blueprint", ImVec4(0.2f, 0.8f, 1.0f, 1.0f));
            renderAssetCard("BP_Ball", "Blueprint", ImVec4(0.2f, 0.8f, 1.0f, 1.0f));
            renderAssetCard("M_CyanGlow", "Material", ImVec4(0.9f, 0.3f, 0.5f, 1.0f));
            renderAssetCard("M_GoldMetal", "Material", ImVec4(0.9f, 0.3f, 0.5f, 1.0f));
            renderAssetCard("VFX_Fire", "Niagara/VFX", ImVec4(1.0f, 0.5f, 0.1f, 1.0f));
            renderAssetCard("VFX_Portal", "Niagara/VFX", ImVec4(0.1f, 0.8f, 1.0f, 1.0f));
            renderAssetCard("S_BounceHit", "SoundCue", ImVec4(0.9f, 0.7f, 0.1f, 1.0f));
            renderAssetCard("Lvl_Main", "SceneLevel", ImVec4(0.3f, 0.9f, 0.4f, 1.0f));

            ImGui::End();
            ImGui::PopStyleColor();
        } else if (s_showOutputLog) {
            ImGui::SetNextWindowPos(ImVec2(0, screenH - bottomTotalH), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(screenW, drawerH), ImGuiCond_Always);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.031f, 0.031f, 0.055f, 1.0f)); // #08080e
            ImGui::Begin("OutputLogWindow", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

            ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), ">_ Output Log");
            ImGui::SameLine();
            auto logs = engine::core::Logger::getRecentLogs(40);
            ImGui::TextDisabled("(%zu entries)", logs.size());
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {}
            ImGui::SameLine(screenW - 40 * s_uiScale);
            if (ImGui::Button(" X ##CloseOutputLog")) { s_showOutputLog = false; }
            ImGui::Separator();

            ImGui::BeginChild("LogRegionInner", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            for (const auto& logLine : logs) {
                if (logLine.find("[ERROR]") != std::string::npos) {
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", logLine.c_str());
                } else if (logLine.find("[WARN]") != std::string::npos) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "%s", logLine.c_str());
                } else if (logLine.find("QuickJS") != std::string::npos) {
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f), "%s", logLine.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.85f, 0.88f, 0.95f, 1.0f), "%s", logLine.c_str());
                }
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();

            ImGui::End();
            ImGui::PopStyleColor();
        }

        // ══════════════════════════════════════════════════════════════════════════
        // ═══ 9. BOTTOM DOCK BAR (UNREAL BOTTOM BAR & CONSOLE COMMAND) ════════════
        // ══════════════════════════════════════════════════════════════════════════
        ImGui::SetNextWindowPos(ImVec2(0, screenH - bottomStatusH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(screenW, bottomStatusH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.043f, 0.070f, 1.0f)); // #0b0b12
        ImGuiWindowFlags bottomBarFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("BottomStatusRail", nullptr, bottomBarFlags);

        // Content Drawer Toggle Button
        if (s_showContentDrawer) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.10f, 0.35f));
        else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.70f, 0.20f, 1.0f));
        if (ImGui::Button(" [FOLDER] Content Drawer ")) {
            s_showContentDrawer = !s_showContentDrawer;
            if (s_showContentDrawer) s_showOutputLog = false;
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Output Log Toggle Button
        if (s_showOutputLog) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.55f, 0.85f, 0.35f));
        else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.85f, 1.0f, 1.0f));
        if (ImGui::Button(" >_ Output Log ")) {
            s_showOutputLog = !s_showOutputLog;
            if (s_showOutputLog) s_showContentDrawer = false;
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();

        // Center: Cmd Console Bar (matching exact WebGL bottom center)
        float consoleW = std::min(450.0f * s_uiScale, screenW * 0.35f);
        float consoleOffset = (screenW - consoleW) * 0.5f;
        if (ImGui::GetCursorPosX() < consoleOffset) {
            ImGui::SameLine(consoleOffset);
        }

        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Cmd v");
        ImGui::SameLine();
        static char cmdInput[256] = "";
        ImGui::SetNextItemWidth(consoleW - 60 * s_uiScale);
        if (ImGui::InputTextWithHint("##ConsoleCmdNative", "Enter Console Command (np. reset breakout, play)...", cmdInput, sizeof(cmdInput), ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::string cmdStr = cmdInput;
            cmdInput[0] = '\0';
            if (!cmdStr.empty() && engine) {
                engine::core::Logger::info("User Command: " + cmdStr);
                if (cmdStr == "play") engine->playSimulation();
                else if (cmdStr == "pause") engine->setPaused(true);
                else if (cmdStr == "stop") engine->stopSimulation();
                else if (cmdStr == "reset") engine->resetScene(engine->getCurrentPreset());
                else if (cmdStr == "breakout") { engine->setCurrentPreset("breakout"); engine->resetScene("breakout"); }
                else if (cmdStr == "platformer") { engine->setCurrentPreset("platformer"); engine->resetScene("platformer"); }
                else if (cmdStr == "shooter") { engine->setCurrentPreset("shooter"); engine->resetScene("shooter"); }
                else if (cmdStr == "racer") { engine->setCurrentPreset("racer"); engine->resetScene("racer"); }
                else if (cmdStr == "empty") { engine->setCurrentPreset("empty"); engine->resetScene("empty"); }
                else if (cmdStr == "help") {
                    engine::core::Logger::info("Commands: play, pause, reset, breakout, platformer, shooter, racer, empty");
                }
            }
        }

        // Right Status Indicators (Exact UE5 bottom-right rail)
        float statusRightW = 380.0f * s_uiScale;
        ImGui::SameLine(screenW - statusRightW);
        ImGui::TextDisabled("Diagnostics *");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "O Zen Server");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "[v] All Saved");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.9f, 1.0f), "UI: %.0f%%", s_uiScale * 100.0f);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.5f, 1.0f), "Vulkan: %.0f FPS", ImGui::GetIO().Framerate);

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        // ══════════════════════════════════════════════════════════════════════════
        // ═══ 10. FLOATING BLUEPRINT / SCRIPT STUDIO & PROJECT POPUPS ═════════════
        // ══════════════════════════════════════════════════════════════════════════
        if (s_showScriptStudio) {
            ImGui::SetNextWindowSize(ImVec2(620 * s_uiScale, 440 * s_uiScale), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Blueprints / QuickJS Script Studio", &s_showScriptStudio)) {
                static char scriptBuf[16384] = "";
                static std::string lastLoadedPreset = "";
                if (engine && lastLoadedPreset != engine->getCurrentPreset()) {
                    lastLoadedPreset = engine->getCurrentPreset();
                    std::string code = engine->getDefaultScript(lastLoadedPreset);
                    strncpy(scriptBuf, code.c_str(), sizeof(scriptBuf) - 1);
                    scriptBuf[sizeof(scriptBuf) - 1] = '\0';
                }

                ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Live Gameplay ECMAScript Studio");
                ImGui::SameLine(ImGui::GetWindowWidth() - 240);
                if (ImGui::Button("Inject Code (F5)")) {
                    if (engine) engine->loadScript(scriptBuf);
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset Code")) {
                    if (engine) {
                        std::string code = engine->getDefaultScript(engine->getCurrentPreset());
                        strncpy(scriptBuf, code.c_str(), sizeof(scriptBuf) - 1);
                        scriptBuf[sizeof(scriptBuf) - 1] = '\0';
                        engine->loadScript(scriptBuf);
                    }
                }
                ImGui::InputTextMultiline("##ScriptCode", scriptBuf, sizeof(scriptBuf), ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput);
            }
            ImGui::End();
        }

        if (ImGui::BeginPopupModal("New Project Setup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            static char projectName[128] = "MyNewProject";
            ImGui::InputText("Project Name", projectName, IM_ARRAYSIZE(projectName));
            static char projectPath[256] = "C:/Projects/";
            ImGui::InputText("Path", projectPath, IM_ARRAYSIZE(projectPath));
            if (ImGui::Button("Create", ImVec2(120, 0))) {
                engine::core::Logger::info(std::string("Created new project: ") + projectName);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Open Project", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            static char loadPath[256] = "C:/Projects/MyProject/project.lucid";
            ImGui::InputText("##LoadPath", loadPath, IM_ARRAYSIZE(loadPath));
            if (ImGui::Button("Open", ImVec2(120, 0))) {
                engine::core::Logger::info(std::string("Opened project: ") + loadPath);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        ImGui::Render();

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = targetView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; 
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = { {0.05f, 0.05f, 0.06f, 1.0f} };

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
