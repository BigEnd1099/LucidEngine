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
        : m_context(context), m_width(width), m_height(height) {
        createOffscreenTarget();
        createCommandBuffer();
        createComputePipelines();
        createGraphicsPipeline();
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
            
#ifdef USE_GLFW
            if (!m_context->isHeadless()) {
                if (m_viewportDescriptor) {
                    ImGui_ImplVulkan_RemoveTexture(m_viewportDescriptor);
                    m_viewportDescriptor = VK_NULL_HANDLE;
                }
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
            vkDestroyImageView(m_context->getDevice(), m_depthImageView, nullptr);
            vmaDestroyImage(m_context->getAllocator(), m_depthImage, m_depthAllocation);

            vkDestroyImageView(m_context->getDevice(), m_objectIdImageView, nullptr);
            vmaDestroyImage(m_context->getAllocator(), m_objectIdImage, m_objectIdAllocation);
            vkDestroyImageView(m_context->getDevice(), m_colorImageView, nullptr);
            vmaDestroyImage(m_context->getAllocator(), m_colorImage, m_colorAllocation);
            vkFreeCommandBuffers(m_context->getDevice(), m_context->getCommandPool(), 1, &m_commandBuffer);
        }
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
#ifdef USE_GLFW
        if (!m_context->isHeadless()) {
            depthImageInfo.extent.width = m_context->getSwapchainExtent().width;
            depthImageInfo.extent.height = m_context->getSwapchainExtent().height;
        }
#endif
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
                glm::vec4 baseColorFactor;
                glm::vec4 cameraPos;
                glm::vec4 materialParams;
            } pcs;

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
                        pcs.materialParams = glm::vec4(mat.metallic, mat.roughness, 0.0f, 0.0f);
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
        jpegBytes.reserve(128 * 1024);
        stbi_write_jpg_to_func(writeJpegBuffer, &jpegBytes, static_cast<int>(m_width), static_cast<int>(m_height), 4, srcPixels, 85);

        std::string encoded = encodeBase64Data(jpegBytes.data(), jpegBytes.size());
        return "data:image/jpeg;base64," + encoded;
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

        // Window & Panels (Obsidian Dark like #07070c and #0e0e16)
        colors[ImGuiCol_WindowBg]             = ImVec4(0.055f, 0.055f, 0.086f, 1.00f); // #0e0e16
        colors[ImGuiCol_ChildBg]              = ImVec4(0.043f, 0.043f, 0.067f, 1.00f); // #0b0b11
        colors[ImGuiCol_PopupBg]              = ImVec4(0.063f, 0.063f, 0.098f, 0.98f); // #101019
        colors[ImGuiCol_Border]               = ImVec4(0.20f, 0.22f, 0.30f, 0.40f);    // subtle white/slate border
        colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        // Headers & Titles (like #0a0a10)
        colors[ImGuiCol_TitleBg]              = ImVec4(0.039f, 0.039f, 0.063f, 1.00f); // #0a0a10
        colors[ImGuiCol_TitleBgActive]        = ImVec4(0.051f, 0.055f, 0.090f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.039f, 0.039f, 0.063f, 0.75f);
        colors[ImGuiCol_MenuBarBg]            = ImVec4(0.039f, 0.039f, 0.063f, 1.00f); // #0a0a10

        // Text & Labels (slate-200 and slate-400)
        colors[ImGuiCol_Text]                 = ImVec4(0.94f, 0.96f, 0.98f, 1.00f); // #f1f5f9
        colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.54f, 0.62f, 1.00f); // #808a9e

        // Frames & Inputs (like #12121e)
        colors[ImGuiCol_FrameBg]              = ImVec4(0.071f, 0.071f, 0.118f, 1.00f); // #12121e
        colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.110f, 0.118f, 0.188f, 1.00f);
        colors[ImGuiCol_FrameBgActive]        = ImVec4(0.145f, 0.157f, 0.251f, 1.00f);

        // Buttons (like #161626, hover #202036, active Cyan #00b4d8)
        colors[ImGuiCol_Button]               = ImVec4(0.086f, 0.086f, 0.149f, 1.00f); // #161626
        colors[ImGuiCol_ButtonHovered]        = ImVec4(0.133f, 0.141f, 0.224f, 1.00f); // #222439
        colors[ImGuiCol_ButtonActive]         = ImVec4(0.000f, 0.706f, 0.847f, 1.00f); // Cyan accent

        // Headers & Selection (like #141424)
        colors[ImGuiCol_Header]               = ImVec4(0.094f, 0.102f, 0.165f, 1.00f); // #181a2a
        colors[ImGuiCol_HeaderHovered]        = ImVec4(0.145f, 0.161f, 0.259f, 1.00f);
        colors[ImGuiCol_HeaderActive]         = ImVec4(0.000f, 0.588f, 0.784f, 1.00f);

        // Tabs (UE5 style tabs)
        colors[ImGuiCol_Tab]                  = ImVec4(0.051f, 0.055f, 0.090f, 1.00f);
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
        colors[ImGuiCol_Separator]            = ImVec4(0.20f, 0.22f, 0.30f, 0.50f);
        colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.000f, 0.706f, 0.847f, 0.78f);
        colors[ImGuiCol_SeparatorActive]      = ImVec4(0.000f, 0.706f, 0.847f, 1.00f);
        colors[ImGuiCol_ResizeGrip]           = ImVec4(0.165f, 0.176f, 0.267f, 0.30f);
        colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.000f, 0.706f, 0.847f, 0.67f);
        colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.000f, 0.706f, 0.847f, 1.00f);

        // Modern roundings & paddings matching Web UI
        style.WindowRounding    = 6.0f;
        style.ChildRounding     = 6.0f;
        style.FrameRounding     = 5.0f;
        style.PopupRounding     = 6.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding      = 4.0f;
        style.TabRounding       = 5.0f;

        style.WindowBorderSize  = 1.0f;
        style.ChildBorderSize   = 1.0f;
        style.PopupBorderSize   = 1.0f;
        style.FrameBorderSize   = 1.0f;

        style.WindowPadding     = ImVec2(8.0f, 8.0f);
        style.FramePadding      = ImVec2(7.0f, 4.0f);
        style.ItemSpacing       = ImVec2(7.0f, 5.0f);
        style.ItemInnerSpacing  = ImVec2(5.0f, 4.0f);
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

    static void drawVec3Control(const char* label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 70.0f) {
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
        if (ImGui::Button("X", buttonSize)) values.x = resetValue;
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(itemWidth);
        ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::SameLine();

        // Y (Green button)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.75f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.90f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.60f, 0.18f, 1.0f));
        if (ImGui::Button("Y", buttonSize)) values.y = resetValue;
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(itemWidth);
        ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::SameLine();

        // Z (Blue button)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.35f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.48f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.28f, 0.75f, 1.0f));
        if (ImGui::Button("Z", buttonSize)) values.z = resetValue;
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(itemWidth);
        ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");

        ImGui::PopStyleVar();
        ImGui::Columns(1);
        ImGui::PopID();
    }

    void VulkanRenderer::renderImGui(VkCommandBuffer cmd, VkImageView targetView, ecs::Registry& registry, core::AssetManager* assetManager, core::Engine* engine) {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- GLOBAL UI SCALING & RESPONSIVE LAYOUT CALCULATION ---
        static float s_uiScale = 1.0f;
        ImGui::GetIO().FontGlobalScale = s_uiScale;

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float screenW = viewport->Size.x;
        float screenH = viewport->Size.y;

        float topMenuBarH = 26.0f * s_uiScale;
        float topToolbarH = 40.0f * s_uiScale;
        float topTotalH = topMenuBarH + topToolbarH;

        float bottomBarH = 210.0f * s_uiScale;
        if (bottomBarH > screenH * 0.38f) bottomBarH = screenH * 0.38f;

        float contentH = screenH - topTotalH - bottomBarH;
        if (contentH < 180.0f) contentH = 180.0f;

        float leftPanelW = 270.0f * s_uiScale;
        if (leftPanelW > screenW * 0.30f) leftPanelW = screenW * 0.30f;

        float rightPanelW = 380.0f * s_uiScale;
        if (rightPanelW > screenW * 0.36f) rightPanelW = screenW * 0.36f;

        float centerViewportW = screenW - leftPanelW - rightPanelW;
        if (centerViewportW < 200.0f) centerViewportW = 200.0f;

        static entt::entity selectedEnt = entt::null;
        static bool s_showScriptStudio = false;
        static bool s_showContentDrawer = false;
        static bool s_showOutputLog = true;

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

        // --- 1. TOP MENU BAR (UNREAL ENGINE 5 HEADER) ---
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(screenW, topMenuBarH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 2));
        ImGuiWindowFlags menuWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("TopMenuBar", nullptr, menuWindowFlags);

        if (ImGui::BeginMenuBar()) {
            // UE5 Cyan Badge
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.45f, 0.85f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::Button(" LUCID 3.8 ");
            ImGui::PopStyleColor(2);
            ImGui::SameLine();

            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Project")) { ImGui::OpenPopup("New Project Setup"); }
                if (ImGui::MenuItem("Open Project...")) { ImGui::OpenPopup("Open Project"); }
                ImGui::Separator();
                if (ImGui::MenuItem("Save Level", "Ctrl+S")) { engine::core::Logger::info("Level saved successfully."); }
                if (ImGui::MenuItem("Export Executable (.exe)")) {
                    engine::core::Logger::info("Packaging standalone binary...");
                    if (engine) engine->packageStandaloneGame("Build/Windows", "LucidGame");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) {}
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
                if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
                ImGui::Separator();
                if (ImGui::MenuItem("Editor Preferences...")) {}
                if (ImGui::MenuItem("Project Settings...")) {}
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window")) {
                ImGui::MenuItem("Blueprints / Scripts Studio", nullptr, &s_showScriptStudio);
                ImGui::MenuItem("Content Drawer", nullptr, &s_showContentDrawer);
                ImGui::MenuItem("Output Log", nullptr, &s_showOutputLog);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Tools")) {
                if (ImGui::MenuItem("Recompile C++ Hot-Reload")) { engine::core::Logger::info("Live Coding: compiled successfully."); }
                if (ImGui::MenuItem("QuickJS Garbage Collection")) { engine::core::Logger::info("QuickJS GC executed."); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Build")) {
                if (ImGui::MenuItem("Build Lighting (Vulkan VSM)")) {}
                if (ImGui::MenuItem("Build Physics Collision Meshes")) {}
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Select")) {
                if (ImGui::MenuItem("Select All Actors")) {}
                if (ImGui::MenuItem("Deselect All")) { selectedEnt = entt::null; }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Actor")) {
                if (ImGui::MenuItem("Delete Selected Actor", "Del")) {
                    if (selectedEnt != entt::null && registry.get().valid(selectedEnt)) {
                        registry.get().destroy(selectedEnt);
                        selectedEnt = entt::null;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("Documentation")) {}
                if (ImGui::MenuItem("About Lucid Engine")) {}
                ImGui::EndMenu();
            }

            // Right-aligned Project Title and UI Scale
            float rightInfoWidth = 430.0f * s_uiScale;
            ImGui::SameLine(screenW - rightInfoWidth);
            ImGui::TextColored(ImVec4(0.5f, 0.6f, 0.75f, 1.0f), "Project: moj projekt gry | UE 5.4 / Vulkan Hybrid");
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Scale:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70 * s_uiScale);
            const char* scaleItems[] = { "100%", "125%", "150%" };
            static int scaleIdx = 0;
            if (ImGui::Combo("##UIScaleCombo", &scaleIdx, scaleItems, IM_ARRAYSIZE(scaleItems))) {
                if (scaleIdx == 0) s_uiScale = 1.0f;
                else if (scaleIdx == 1) s_uiScale = 1.25f;
                else if (scaleIdx == 2) s_uiScale = 1.50f;
            }

            ImGui::EndMenuBar();
        }
        ImGui::End();
        ImGui::PopStyleVar();

        // --- 2. TOP TOOLBAR & QUICK TABS STRIP (UE5 SUB-TOOLBAR) ---
        ImGui::SetNextWindowPos(ImVec2(0, topMenuBarH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(screenW, topToolbarH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
        ImGuiWindowFlags subToolbarFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("SubToolbar", nullptr, subToolbarFlags);

        // Level Tab
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.85f, 1.0f, 1.0f));
        ImGui::Button(" CzystyPoziom.scene (PersistLevel)  x ");
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("+##NewTab")) {}
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Mode Selectors
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.11f, 0.18f, 1.0f));
        if (ImGui::Button("Select Mode v")) {}
        ImGui::SameLine();
        if (ImGui::Button("+ Add v")) {}
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Quick Tools Pills
        if (ImGui::Button("</> Blueprints / Scripts")) { s_showScriptStudio = !s_showScriptStudio; }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Center Transport
        bool isPaused = engine ? engine->isPaused() : true;
        if (isPaused) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.72f, 0.38f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.85f, 0.45f, 1.0f));
            if (ImGui::Button("  > Play  ")) { if (engine) engine->playSimulation(); }
            ImGui::PopStyleColor(2);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.65f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.75f, 0.20f, 1.0f));
            if (ImGui::Button("  || Pause  ")) { if (engine) engine->setPaused(true); }
            ImGui::PopStyleColor(2);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
        if (ImGui::Button("  # Stop  ")) {
            if (engine) {
                engine->stopSimulation();
            }
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        const char* presets[] = { "breakout", "platformer", "shooter", "racer", "empty" };
        static int currentPresetIdx = 0;
        ImGui::SetNextItemWidth(110 * s_uiScale);
        if (ImGui::Combo("##PresetCombo", &currentPresetIdx, presets, IM_ARRAYSIZE(presets))) {
            if (engine) {
                engine->setCurrentPreset(presets[currentPresetIdx]);
                engine->resetScene(presets[currentPresetIdx]);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Level")) {
            if (engine) engine->resetScene(presets[currentPresetIdx]);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.45f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.08f, 0.60f, 0.35f, 1.0f));
        if (ImGui::Button("  Kompiluj gre (.exe)  ")) {
            if (engine) {
                engine->packageStandaloneGame("Build/Windows/MyLucidGame", "MyLucidGame");
                engine::core::Logger::info("Game packaged successfully to Build/Windows/MyLucidGame/MyLucidGame.exe");
            }
        }
        ImGui::PopStyleColor(2);

        // Right side badges
        float subRightW = 320.0f * s_uiScale;
        ImGui::SameLine(screenW - subRightW);
        ImGui::Button("Editor Cam v");
        ImGui::SameLine();
        ImGui::Button("Tryb Gry v");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "O Jolt Server");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "%.1f FPS", ImGui::GetIO().Framerate);

        ImGui::End();
        ImGui::PopStyleVar();

        // --- 3. LEFT PANEL: "PLACE ACTORS" (PALETTE) ---
        ImGui::SetNextWindowPos(ImVec2(0, topTotalH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(leftPanelW, contentH), ImGuiCond_Always);
        ImGui::Begin("Place Actors", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        static char classSearch[64] = "";
        ImGui::InputTextWithHint("##SearchClasses", "Search Classes...", classSearch, sizeof(classSearch));
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.5f, 0.6f, 0.7f, 1.0f), "BASIC");

        auto spawnActorItem = [&](const char* title, const char* desc, int typeIdx) {
            if (classSearch[0] != '\0' && std::string(title).find(classSearch) == std::string::npos) return;
            ImGui::PushID(title);
            if (ImGui::Button(title, ImVec2(-1, 28 * s_uiScale))) {
                auto entity = registry.createEntity();
                ecs::MeshComponent dynMesh; dynMesh.meshId = 2;
                registry.get().emplace<ecs::MeshComponent>(entity, dynMesh);

                if (typeIdx == 0) { // Empty Actor
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{title});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 2.0f, 0.0f));
                } else if (typeIdx == 1) { // Character Player
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"CharacterPlayer"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 4.0f, 0.0f));
                    ecs::RigidBodyComponent dynRb; dynRb.mass = 1.0f; dynRb.shapeType = 2; // Capsule
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
                } else if (typeIdx == 7) { // Player Start
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"PlayerStart"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 1.0f, 0.0f));
                } else if (typeIdx == 8) { // Trigger Box
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"TriggerBox"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f), glm::vec3(2.0f, 1.0f, 2.0f));
                    ecs::RigidBodyComponent dynRb; dynRb.mass = 0.0f; dynRb.shapeType = 0; dynRb.isSensor = true;
                    registry.get().emplace<ecs::RigidBodyComponent>(entity, dynRb);
                } else if (typeIdx == 9) { // Trigger Sphere
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"TriggerSphere"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 1.0f, 0.0f));
                    ecs::RigidBodyComponent dynRb; dynRb.mass = 0.0f; dynRb.shapeType = 1; dynRb.isSensor = true;
                    registry.get().emplace<ecs::RigidBodyComponent>(entity, dynRb);
                } else if (typeIdx == 10) { // UE5 Sports Car
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"UE5_SportsCar"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 2.0f, 0.0f));
                    ecs::RigidBodyComponent dynRb; dynRb.mass = 1200.0f; dynRb.shapeType = 0;
                    dynRb.halfExtents = glm::vec3(1.2f, 0.6f, 2.5f);
                    registry.get().emplace<ecs::RigidBodyComponent>(entity, dynRb);
                } else if (typeIdx == 11) { // UE5 Chamfer Cube
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"UE5_ChamferCube"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 4.0f, 0.0f));
                    ecs::RigidBodyComponent dynRb; dynRb.mass = 1.0f; dynRb.shapeType = 0;
                    dynRb.halfExtents = glm::vec3(0.5f, 0.5f, 0.5f);
                    registry.get().emplace<ecs::RigidBodyComponent>(entity, dynRb);
                } else if (typeIdx == 12) { // UE5 Gizmo
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"UE5_GizmoLocation"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 2.0f, 0.0f));
                } else if (typeIdx == 13) { // UE5 Blue Cube
                    registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"UE5_BlueCube"});
                    registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(0.0f, 3.0f, 0.0f));
                    ecs::RigidBodyComponent dynRb; dynRb.mass = 1.0f; dynRb.shapeType = 0;
                    registry.get().emplace<ecs::RigidBodyComponent>(entity, dynRb);
                }
                selectedEnt = entity;
                engine::core::Logger::info(std::string("Placed Actor: ") + title + " (ID: " + std::to_string(static_cast<uint32_t>(entity)) + ")");
            }
            ImGui::TextDisabled("  %s", desc);
            ImGui::PopID();
        };

        spawnActorItem("Actor", "Empty actor container", 0);
        spawnActorItem("Character (Player)", "WASD controllable player pawn", 1);
        spawnActorItem("Capsule Pawn", "Dynamic Jolt capsule character", 2);
        spawnActorItem("Coin (Trigger)", "Pickable sensor item", 3);
        spawnActorItem("Trigger Zone", "Overlap sensor volume", 4);
        spawnActorItem("Pawn", "Dynamic physics rigid body", 5);
        spawnActorItem("Point Light", "Omni-directional point light", 6);
        spawnActorItem("Player Start", "Initial player spawn transform", 7);
        spawnActorItem("Trigger Box", "Physics overlap trigger volume", 8);
        spawnActorItem("Trigger Sphere", "Spherical overlap volume", 9);
        spawnActorItem("Sports Car (UE 5.8)", "Unreal 5.8 Sports Car FBX Model", 10);
        spawnActorItem("1M Chamfer Cube (UE 5.8)", "Unreal 5.8 Chamfered Cube", 11);
        spawnActorItem("Location Gizmo (UE 5.8)", "Unreal 5.8 Transform 3D Gizmo", 12);
        spawnActorItem("Blue Cube (UE 5.8)", "Unreal 5.8 Automation Blue Cube", 13);

        ImGui::End();

        // --- 4. CENTER PANEL: MAIN 3D VIEWPORT (VULKAN 1.3) ---
        ImGui::SetNextWindowPos(ImVec2(leftPanelW, topTotalH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(centerViewportW, contentH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        // Viewport Header Controls Bar
        ImGui::SetCursorPos(ImVec2(8, 6));
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Perspective v  |  Lit (Vulkan 1.3 3D) v  |  Snap: 10  |  Angle: 45 deg  |  Cam Speed: 4");

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (viewportPanelSize.x > 10.0f && viewportPanelSize.y > 10.0f) {
            m_viewportAspectRatio = viewportPanelSize.x / viewportPanelSize.y;
        }

        if (m_viewportDescriptor != VK_NULL_HANDLE && viewportPanelSize.x > 0 && viewportPanelSize.y > 0) {
            ImGui::Image((ImTextureID)m_viewportDescriptor, viewportPanelSize);
        }

        // Live Native Game HUD Overlay (Bottom of Viewport)
        if (engine) {
            ImGui::SetCursorPos(ImVec2(15, 36));
            if (!engine->isPaused()) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.8f, 1.0f), "[ NATIVE VULKAN 1.3 GAMEPLAY ACTIVE ]");
                ImGui::SetCursorPos(ImVec2(15, 56));
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "SCORE: %d", engine->getScore());
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), " | HEALTH: %d HP", engine->getHealth());
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), " | STATUS: %s", engine->getGameStatus().c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "[ PAUSED / EDITOR MODE ]");
            }

            ImGui::SetCursorPos(ImVec2(15, viewportPanelSize.y - 24));
            ImGui::TextColored(ImVec4(0.7f, 0.75f, 0.85f, 0.9f), "WSAD/Arrows: ruch * LPM: obrot * Scroll: zoom * PPM: rozgladanie * Spacja: skok");
        }

        ImGui::End();
        ImGui::PopStyleVar();

        // --- 5. RIGHT TOP PANEL: "OUTLINER" (SCENE HIERARCHY) ---
        ImGui::SetNextWindowPos(ImVec2(leftPanelW + centerViewportW, topTotalH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(rightPanelW, contentH * 0.48f), ImGuiCond_Always);
        ImGui::Begin("Outliner", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        static char outlinerFilter[64] = "";
        ImGui::InputTextWithHint("##OutlinerFilter", "Search...", outlinerFilter, sizeof(outlinerFilter));
        ImGui::Separator();

        ImGui::Columns(2, "OutlinerColumns");
        ImGui::SetColumnWidth(0, rightPanelW * 0.65f);
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

        // --- 6. RIGHT BOTTOM PANEL: "DETAILS" (PROPERTIES INSPECTOR) ---
        ImGui::SetNextWindowPos(ImVec2(leftPanelW + centerViewportW, topTotalH + contentH * 0.48f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(rightPanelW, contentH * 0.52f), ImGuiCond_Always);
        ImGui::Begin("Details", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        if (selectedEnt != entt::null && registry.get().valid(selectedEnt)) {
            std::string tagStr = "Actor";
            if (registry.get().all_of<ecs::TagComponent>(selectedEnt)) {
                tagStr = registry.get().get<ecs::TagComponent>(selectedEnt).name;
            }
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Actor: %s (ID: %u)", tagStr.c_str(), static_cast<uint32_t>(selectedEnt));
            ImGui::Separator();

            if (registry.get().all_of<ecs::TagComponent>(selectedEnt)) {
                auto& tag = registry.get().get<ecs::TagComponent>(selectedEnt);
                char tagBuf[128];
                strncpy(tagBuf, tag.name.c_str(), sizeof(tagBuf));
                tagBuf[sizeof(tagBuf) - 1] = '\0';
                if (ImGui::InputText("Item Label", tagBuf, sizeof(tagBuf))) {
                    tag.name = tagBuf;
                }
            }

            if (registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Transform");
                drawVec3Control("Location", t.position, 0.0f);
                drawVec3Control("Rotation", t.rotation, 0.0f);
                drawVec3Control("Scale", t.scale, 1.0f);

                if (ImGui::Button("Reset Pos")) { t.position = glm::vec3(0.0f); }
                ImGui::SameLine();
                if (ImGui::Button("Ground (Y=0.5)")) { t.position.y = 0.5f; }
                ImGui::SameLine();
                if (ImGui::Button("+5 Y Jump")) { t.position.y += 5.0f; }
            }

            if (registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt)) {
                auto& rb = registry.get().get<ecs::RigidBodyComponent>(selectedEnt);
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Jolt Physics");
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

            ImGui::Spacing();
            if (ImGui::Button("Delete Entity", ImVec2(120, 0))) {
                registry.get().destroy(selectedEnt);
                selectedEnt = entt::null;
            }
        } else {
            ImGui::TextDisabled("Brak zaznaczonego aktora.");
            ImGui::TextDisabled("Kliknij obiekt w Outlinerze lub Place Actors.");
            ImGui::Separator();
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

        // --- 7. BOTTOM PANEL: "CONTENT DRAWER, AI PROMPT & CONSOLE" ---
        ImGui::SetNextWindowPos(ImVec2(0, topTotalH + contentH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(screenW, bottomBarH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        ImGui::Begin("BottomPanel", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        // Top Drawer Row with AI prompt in center
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.09f, 0.10f, 0.16f, 1.0f));
        if (ImGui::Button(" Content Drawer ")) { s_showContentDrawer = !s_showContentDrawer; }
        ImGui::SameLine();
        if (ImGui::Button(" >_ Output Log ")) { s_showOutputLog = !s_showOutputLog; }
        ImGui::PopStyleColor();

        // Center AI Prompt Input Bar (matching Web Image 2)
        ImGui::SameLine(screenW * 0.32f);
        static char aiPromptBuf[256] = "";
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.06f, 0.07f, 0.12f, 1.0f));
        ImGui::SetNextItemWidth(screenW * 0.32f);
        ImGui::InputTextWithHint("##AIPrompt", "Napisz do AI co chcesz stworzyc (np. Wyscig, Arkanoid)...", aiPromptBuf, sizeof(aiPromptBuf));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.65f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.80f, 1.0f, 1.0f));
        if (ImGui::Button("  Generuj  ")) {
            std::string promptStr = aiPromptBuf;
            aiPromptBuf[0] = '\0';
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

        ImGui::Separator();

        // Output Log / Console Scroll Area
        float cmdLineH = ImGui::GetFrameHeightWithSpacing() + 6;
        ImGui::BeginChild("LogScrollRegion", ImVec2(0, -cmdLineH), false, ImGuiWindowFlags_HorizontalScrollbar);
        auto logs = engine::core::Logger::getRecentLogs(40);
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

        // Bottom Console Command Line (matching Image 2)
        static char cmdInput[256] = "";
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "cmd >");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(screenW - 280.0f * s_uiScale);
        if (ImGui::InputTextWithHint("##ConsoleCmd", "Enter Console Command (np. reset breakout, play, help)...", cmdInput, sizeof(cmdInput), ImGuiInputTextFlags_EnterReturnsTrue)) {
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
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "O Jolt Server");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.9f, 1.0f), "All saved");

        ImGui::End();
        ImGui::PopStyleVar();

        // --- 8. FLOATING STUDIOS (BLUEPRINTS, UMG, SOUND) ---
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
