#include "renderer/VulkanRenderer.hpp"
#include "renderer/VulkanRendererInternal.hpp"
#include "renderer/Vertex.hpp"
#include "core/Logger.hpp"
#include "utils/FileIO.hpp"
#include <stdexcept>
#include <iostream>
#include <array>

namespace engine::renderer {

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

    void VulkanRenderer::createGraphicsPipeline() {
        VkDevice dev = m_context->getDevice();
        VkShaderModule vertShaderModule = createShaderModule(utils::readFile("pbr.vert.spv"));
        VkShaderModule fragShaderModule = createShaderModule(utils::readFile("pbr.frag.spv"));
        VkShaderModule rtFragShaderModule = VK_NULL_HANDLE;
        if (m_context->isRayTracingSupported()) {
            rtFragShaderModule = createShaderModule(utils::readFile("pbr_rt.frag.spv"));
        }

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
        depthStencil.depthWriteEnable = VK_FALSE; // Zapis glębokości odbywa się w Depth Prepass (zero overdraw w PBR!)
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
        if (rtFragShaderModule) {
            shaderStages[1].module = rtFragShaderModule;
            if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_rtGraphicsPipeline) != VK_SUCCESS) {
                throw std::runtime_error("failed to create ray-query PBR graphics pipeline!");
            }
            shaderStages[1].module = fragShaderModule;
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

        // Thin G-Buffer Pipeline (gbuffer.vert + gbuffer.frag, 3 color attachments, EQUAL depth test)
        VkShaderModule gbufferVertModule = createShaderModule(utils::readFile("gbuffer.vert.spv"));
        VkShaderModule gbufferFragModule = createShaderModule(utils::readFile("gbuffer.frag.spv"));
        if (gbufferVertModule != VK_NULL_HANDLE && gbufferFragModule != VK_NULL_HANDLE) {
            VkPipelineShaderStageCreateInfo gbufferStages[2] = { shaderStages[0], shaderStages[1] };
            gbufferStages[0].module = gbufferVertModule;
            gbufferStages[1].module = gbufferFragModule;

            VkPipelineColorBlendAttachmentState gbufferBlendAttachments[3]{};
            for (int i = 0; i < 3; ++i) {
                gbufferBlendAttachments[i].colorWriteMask =
                    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                gbufferBlendAttachments[i].blendEnable = VK_FALSE;
            }
            VkPipelineColorBlendStateCreateInfo gbufferBlend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
            gbufferBlend.attachmentCount = 3;
            gbufferBlend.pAttachments = gbufferBlendAttachments;

            const VkFormat gbufferFormats[3] = { VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16_SFLOAT };
            VkPipelineRenderingCreateInfo gbufferRendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
            gbufferRendering.colorAttachmentCount = 3;
            gbufferRendering.pColorAttachmentFormats = gbufferFormats;
            gbufferRendering.depthAttachmentFormat = depthFormat;

            VkPipelineDepthStencilStateCreateInfo gbufferDepth = depthStencil;
            gbufferDepth.depthTestEnable = VK_TRUE;
            gbufferDepth.depthWriteEnable = VK_FALSE;
            gbufferDepth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

            VkGraphicsPipelineCreateInfo gbufferInfo = pipelineInfo;
            gbufferInfo.pNext = &gbufferRendering;
            gbufferInfo.stageCount = 2;
            gbufferInfo.pStages = gbufferStages;
            gbufferInfo.pColorBlendState = &gbufferBlend;
            gbufferInfo.pDepthStencilState = &gbufferDepth;

            if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gbufferInfo, nullptr, &m_gBufferPipeline) != VK_SUCCESS) {
                throw std::runtime_error("failed to create G-Buffer graphics pipeline!");
            }
            vkDestroyShaderModule(dev, gbufferFragModule, nullptr);
            vkDestroyShaderModule(dev, gbufferVertModule, nullptr);
        }

        if (rtFragShaderModule) vkDestroyShaderModule(dev, rtFragShaderModule, nullptr);
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

}
