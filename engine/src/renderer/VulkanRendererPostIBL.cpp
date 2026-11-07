#include "renderer/VulkanRenderer.hpp"
#include "core/Logger.hpp"
#include "utils/FileIO.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#ifdef USE_GLFW
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#endif

#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <deque>
#include "renderer/stb_image.h"
#define TINYEXR_USE_MINIZ 1
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"  

namespace engine::renderer {

    namespace {
        std::vector<char> loadSpvBinary(const std::string& filename) {
            return utils::readFile(filename);
        }

        VkCommandBuffer beginOneShotCommands(VulkanContext* ctx) {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = ctx->getCommandPool();
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer cmd = VK_NULL_HANDLE;
            vkAllocateCommandBuffers(ctx->getDevice(), &allocInfo, &cmd);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &beginInfo);
            return cmd;
        }

        void endOneShotCommands(VulkanContext* ctx, VkCommandBuffer cmd) {
            vkEndCommandBuffer(cmd);
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmd;
            vkQueueSubmit(ctx->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(ctx->getGraphicsQueue());
            vkFreeCommandBuffers(ctx->getDevice(), ctx->getCommandPool(), 1, &cmd);
        }

        struct IblPushConstants {
            int faceIndex{0};
            float roughness{0.0f};
            float envResolution{512.0f};
            float pad{0.0f};
        };
    }

    VkShaderModule VulkanRenderer::createShaderModule(const std::vector<char>& code) const {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_context->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan shader module!");
        }
        return shaderModule;
    }

    void VulkanRenderer::transitionImage(
        VkCommandBuffer cmd, VkImage image,
        VkImageLayout oldLayout, VkImageLayout newLayout,
        VkImageAspectFlags aspectMask,
        uint32_t baseMip, uint32_t levelCount,
        uint32_t baseLayer, uint32_t layerCount,
        VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
        VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
    {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = srcStage;
        barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask = dstStage;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspectMask;
        barrier.subresourceRange.baseMipLevel = baseMip;
        barrier.subresourceRange.levelCount = levelCount;
        barrier.subresourceRange.baseArrayLayer = baseLayer;
        barrier.subresourceRange.layerCount = layerCount;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    void VulkanRenderer::createFullscreenPipeline(
        const std::string& fragShaderSpv,
        VkPipelineLayout layout,
        VkFormat targetFormat,
        VkPipeline* outPipeline,
        bool additiveBlend)
    {
        VkDevice device = m_context->getDevice();
        VkShaderModule vertModule = createShaderModule(loadSpvBinary("fullscreen.vert.spv"));
        VkShaderModule fragModule = createShaderModule(loadSpvBinary(fragShaderSpv));

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";

        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (additiveBlend) {
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &targetFormat;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = layout;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, outPipeline) != VK_SUCCESS) {
            vkDestroyShaderModule(device, fragModule, nullptr);
            vkDestroyShaderModule(device, vertModule, nullptr);
            throw std::runtime_error("Failed to create fullscreen pipeline for " + fragShaderSpv);
        }

        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
    }

    void VulkanRenderer::uploadTexture2D(
        VkImage& dstImage, VmaAllocation& dstAlloc, VkImageView& dstView,
        const void* pixels, uint32_t width, uint32_t height,
        VkFormat format, bool generateMips)
    {
        const uint32_t bytesPerPixel = (format == VK_FORMAT_R32G32B32A32_SFLOAT) ? 16u : 4u;
        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * bytesPerPixel;
        const uint32_t mipLevels = generateMips
            ? (static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1u)
            : 1u;

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingAlloc = VK_NULL_HANDLE;
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = imageSize;
        bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo allocCreateInfo{};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo stagingInfo{};
        vmaCreateBuffer(m_context->getAllocator(), &bufInfo, &allocCreateInfo, &stagingBuffer, &stagingAlloc, &stagingInfo);
        if (!stagingInfo.pMappedData) {
            void* mapped = nullptr;
            vmaMapMemory(m_context->getAllocator(), stagingAlloc, &mapped);
            std::memcpy(mapped, pixels, static_cast<size_t>(imageSize));
            vmaUnmapMemory(m_context->getAllocator(), stagingAlloc);
        } else {
            std::memcpy(stagingInfo.pMappedData, pixels, static_cast<size_t>(imageSize));
        }

        VkImageCreateInfo imgInfo{};
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.extent = { width, height, 1 };
        imgInfo.mipLevels = mipLevels;
        imgInfo.arrayLayers = 1;
        imgInfo.format = format;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        (generateMips ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0u);
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo imgAllocInfo{};
        imgAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        vmaCreateImage(m_context->getAllocator(), &imgInfo, &imgAllocInfo, &dstImage, &dstAlloc, nullptr);

        VkCommandBuffer cmd = beginOneShotCommands(m_context);
        transitionImage(cmd, dstImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = { width, height, 1 };
        vkCmdCopyBufferToImage(cmd, stagingBuffer, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        if (generateMips && mipLevels > 1) {
            int32_t mipW = static_cast<int32_t>(width);
            int32_t mipH = static_cast<int32_t>(height);
            for (uint32_t i = 1; i < mipLevels; ++i) {
                transitionImage(cmd, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

                VkImageBlit blit{};
                blit.srcOffsets[1] = { mipW, mipH, 1 };
                blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 };
                blit.dstOffsets[1] = { std::max(1, mipW / 2), std::max(1, mipH / 2), 1 };
                blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 };
                vkCmdBlitImage(cmd, dstImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

                transitionImage(cmd, dstImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                mipW = std::max(1, mipW / 2);
                mipH = std::max(1, mipH / 2);
            }
            transitionImage(cmd, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, mipLevels - 1, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        } else {
            transitionImage(cmd, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }

        endOneShotCommands(m_context, cmd);
        vmaDestroyBuffer(m_context->getAllocator(), stagingBuffer, stagingAlloc);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = dstImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };
        vkCreateImageView(m_context->getDevice(), &viewInfo, nullptr, &dstView);
    }

    void VulkanRenderer::createDefaultTextures() {
        const uint8_t whitePx[4]  = { 255, 255, 255, 255 };
        const uint8_t normalPx[4] = { 128, 128, 255, 255 };
        const uint8_t ormPx[4]    = { 255, 255, 255, 255 }; // glTF 2.0 default: R=AO(1.0), G=Rough(1.0), B=Metal(1.0)

        uploadTexture2D(m_defaultAlbedoImage, m_defaultAlbedoAlloc, m_defaultAlbedoView, whitePx, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, false);
        uploadTexture2D(m_defaultNormalImage, m_defaultNormalAlloc, m_defaultNormalView, normalPx, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, false);
        uploadTexture2D(m_defaultOrmImage, m_defaultOrmAlloc, m_defaultOrmView, ormPx, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, false);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = (m_context->getMaxSamplerAnisotropy() > 1.0f) ? VK_TRUE : VK_FALSE;
        samplerInfo.maxAnisotropy = m_context->getMaxSamplerAnisotropy();
        samplerInfo.maxLod = 16.0f;
        vkCreateSampler(m_context->getDevice(), &samplerInfo, nullptr, &m_defaultTextureSampler);
    }

    void VulkanRenderer::cleanupDefaultTextures() {
        VkDevice dev = m_context->getDevice();
        VmaAllocator alloc = m_context->getAllocator();
        if (m_defaultTextureSampler) { vkDestroySampler(dev, m_defaultTextureSampler, nullptr); m_defaultTextureSampler = VK_NULL_HANDLE; }
        auto destroyTex = [&](VkImage& img, VmaAllocation& mem, VkImageView& view) {
            if (view) { vkDestroyImageView(dev, view, nullptr); view = VK_NULL_HANDLE; }
            if (img)  { vmaDestroyImage(alloc, img, mem); img = VK_NULL_HANDLE; mem = VK_NULL_HANDLE; }
        };
        destroyTex(m_defaultAlbedoImage, m_defaultAlbedoAlloc, m_defaultAlbedoView);
        destroyTex(m_defaultNormalImage, m_defaultNormalAlloc, m_defaultNormalView);
        destroyTex(m_defaultOrmImage, m_defaultOrmAlloc, m_defaultOrmView);
    }

    void VulkanRenderer::createOffscreenTarget() {
        VkDevice dev = m_context->getDevice();
        VmaAllocator alloc = m_context->getAllocator();

        auto createImg2D = [&](uint32_t w, uint32_t h, uint32_t mips, VkFormat fmt, VkImageUsageFlags usage,
                               VkImageAspectFlags aspect, VkImage& img, VmaAllocation& mem, VkImageView& view) {
            VkImageCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            info.imageType = VK_IMAGE_TYPE_2D;
            info.extent = { std::max(1u, w), std::max(1u, h), 1u };
            info.mipLevels = mips;
            info.arrayLayers = 1;
            info.format = fmt;
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            info.usage = usage;
            info.samples = VK_SAMPLE_COUNT_1_BIT;

            VmaAllocationCreateInfo vmaInfo{};
            vmaInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            vmaCreateImage(alloc, &info, &vmaInfo, &img, &mem, nullptr);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = img;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = fmt;
            viewInfo.subresourceRange = { aspect, 0, mips, 0, 1 };
            vkCreateImageView(dev, &viewInfo, nullptr, &view);
        };

        createImg2D(m_width, m_height, 1, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, m_colorImage, m_colorAllocation, m_colorImageView);

        createImg2D(m_width, m_height, 1, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, m_ldrTempImage, m_ldrTempAllocation, m_ldrTempImageView);

        createImg2D(m_width, m_height, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, m_hdrImage, m_hdrAllocation, m_hdrImageView);

        // 5-poziomowa piramida Jimenez Bloom (start od 1/2 rozdzielczości)
        const uint32_t bloomBaseW = std::max(1u, m_width / 2u);
        const uint32_t bloomBaseH = std::max(1u, m_height / 2u);
        const uint32_t maxPossibleMips = static_cast<uint32_t>(std::floor(std::log2(std::max(bloomBaseW, bloomBaseH)))) + 1u;
        m_activeBloomMips = std::clamp(maxPossibleMips, 1u, BLOOM_MIP_COUNT);

        createImg2D(bloomBaseW, bloomBaseH, m_activeBloomMips, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, m_bloomImage, m_bloomAllocation, m_bloomFullView);

        uint32_t mipW = bloomBaseW;
        uint32_t mipH = bloomBaseH;
        for (uint32_t i = 0; i < BLOOM_MIP_COUNT; ++i) {
            const uint32_t clampedMip = std::min(i, m_activeBloomMips - 1u);
            m_bloomMipExtents[i] = { mipW, mipH };

            VkImageViewCreateInfo mv{};
            mv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            mv.image = m_bloomImage;
            mv.viewType = VK_IMAGE_VIEW_TYPE_2D;
            mv.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            mv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, clampedMip, 1, 0, 1 };
            vkCreateImageView(dev, &mv, nullptr, &m_bloomMipViews[i]);

            mipW = std::max(1u, mipW / 2u);
            mipH = std::max(1u, mipH / 2u);
        }

        createImg2D(m_width, m_height, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, m_aoImage, m_aoAllocation, m_aoImageView);

        createImg2D(m_width, m_height, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, m_aoBlurImage, m_aoBlurAllocation, m_aoBlurImageView);

        createImg2D(m_width, m_height, 1, VK_FORMAT_R32_UINT,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, m_objectIdImage, m_objectIdAllocation, m_objectIdImageView);

        createImg2D(m_width, m_height, 1, VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT, m_depthImage, m_depthAllocation, m_depthImageView);

        createImg2D(m_width, m_height, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, m_gBufferNormalRoughnessImage, m_gBufferNormalRoughnessAlloc, m_gBufferNormalRoughnessImageView);

        createImg2D(m_width, m_height, 1, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, m_gBufferAlbedoMetallicImage, m_gBufferAlbedoMetallicAlloc, m_gBufferAlbedoMetallicImageView);

        createImg2D(m_width, m_height, 1, VK_FORMAT_R16G16_SFLOAT,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, m_gBufferMotionImage, m_gBufferMotionAlloc, m_gBufferMotionImageView);

        if (!m_viewportSampler) {
            VkSamplerCreateInfo sInfo{};
            sInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sInfo.magFilter = VK_FILTER_LINEAR;
            sInfo.minFilter = VK_FILTER_LINEAR;
            sInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            sInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            vkCreateSampler(dev, &sInfo, nullptr, &m_viewportSampler);
        }
        if (!m_linearSampler) {
            VkSamplerCreateInfo sInfo{};
            sInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sInfo.magFilter = VK_FILTER_LINEAR;
            sInfo.minFilter = VK_FILTER_LINEAR;
            sInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            sInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sInfo.maxLod = 16.0f;
            vkCreateSampler(dev, &sInfo, nullptr, &m_linearSampler);
        }
        if (!m_depthSampler) {
            VkSamplerCreateInfo sInfo{};
            sInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sInfo.magFilter = VK_FILTER_NEAREST;
            sInfo.minFilter = VK_FILTER_NEAREST;
            sInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            vkCreateSampler(dev, &sInfo, nullptr, &m_depthSampler);
        }

        // Inicjalizacja czystego stanu layoutów (biała mapa AO = 1.0 przy wyłączonym GTAO)
        VkCommandBuffer cmd = beginOneShotCommands(m_context);
        transitionImage(cmd, m_colorImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        transitionImage(cmd, m_hdrImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        transitionImage(cmd, m_aoBlurImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        VkRenderingAttachmentInfo clearAo{};
        clearAo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        clearAo.imageView = m_aoBlurImageView;
        clearAo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        clearAo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        clearAo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        clearAo.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea = { {0, 0}, {m_width, m_height} };
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &clearAo;
        vkCmdBeginRendering(cmd, &ri);
        vkCmdEndRendering(cmd);

        transitionImage(cmd, m_aoBlurImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        endOneShotCommands(m_context, cmd);

#ifdef USE_GLFW
        if (!m_context->isHeadless() && ImGui::GetCurrentContext() != nullptr) {
            m_viewportDescriptor = ImGui_ImplVulkan_AddTexture(
                m_viewportSampler, m_colorImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
#endif
    }

    void VulkanRenderer::cleanupOffscreenTarget() {
        VkDevice dev = m_context->getDevice();
        VmaAllocator alloc = m_context->getAllocator();

#ifdef USE_GLFW
        if (!m_context->isHeadless() && m_viewportDescriptor != VK_NULL_HANDLE && ImGui::GetCurrentContext() != nullptr) {
            ImGui_ImplVulkan_RemoveTexture(m_viewportDescriptor);
            m_viewportDescriptor = VK_NULL_HANDLE;
        }
#endif
        for (auto& mv : m_bloomMipViews) {
            if (mv) { vkDestroyImageView(dev, mv, nullptr); mv = VK_NULL_HANDLE; }
        }
        auto destroyImg = [&](VkImage& img, VmaAllocation& mem, VkImageView& view) {
            if (view) { vkDestroyImageView(dev, view, nullptr); view = VK_NULL_HANDLE; }
            if (img)  { vmaDestroyImage(alloc, img, mem); img = VK_NULL_HANDLE; mem = VK_NULL_HANDLE; }
        };
        destroyImg(m_bloomImage, m_bloomAllocation, m_bloomFullView);
        destroyImg(m_hdrImage, m_hdrAllocation, m_hdrImageView);
        destroyImg(m_ldrTempImage, m_ldrTempAllocation, m_ldrTempImageView);
        destroyImg(m_colorImage, m_colorAllocation, m_colorImageView);
        destroyImg(m_aoImage, m_aoAllocation, m_aoImageView);
        destroyImg(m_aoBlurImage, m_aoBlurAllocation, m_aoBlurImageView);
        destroyImg(m_objectIdImage, m_objectIdAllocation, m_objectIdImageView);
        destroyImg(m_depthImage, m_depthAllocation, m_depthImageView);
        destroyImg(m_gBufferNormalRoughnessImage, m_gBufferNormalRoughnessAlloc, m_gBufferNormalRoughnessImageView);
        destroyImg(m_gBufferAlbedoMetallicImage, m_gBufferAlbedoMetallicAlloc, m_gBufferAlbedoMetallicImageView);
        destroyImg(m_gBufferMotionImage, m_gBufferMotionAlloc, m_gBufferMotionImageView);
    }

    bool VulkanRenderer::loadEnvironmentHdriFile(const std::string& path) {
        if (!std::filesystem::exists(path)) {
            core::Logger::warn("HDRI/EXR file not found: " + path);
            return false;
        }

        int width = 0, height = 0, channels = 0;
        float* data = nullptr;
        bool isExr = false;

        std::string ext = std::filesystem::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

        if (ext == ".exr") {
            isExr = true;
            const char* err = nullptr;
            int ret = LoadEXR(&data, &width, &height, path.c_str(), &err);
            if (ret != TINYEXR_SUCCESS) {
                std::string errStr = err ? err : ("code " + std::to_string(ret));
                core::Logger::warn("Failed to decode EXR file (" + errStr + "): " + path);
                if (err) FreeEXRErrorMessage(err);
                return false;
            }
        } else {
            data = stbi_loadf(path.c_str(), &width, &height, &channels, 4);
            if (!data) {
                core::Logger::warn("Failed to decode HDR file: " + path);
                return false;
            }
        }

        VkDevice dev = m_context->getDevice();
        VmaAllocator alloc = m_context->getAllocator();
        vkDeviceWaitIdle(dev);

        if (m_environmentImageView) { vkDestroyImageView(dev, m_environmentImageView, nullptr); m_environmentImageView = VK_NULL_HANDLE; }
        if (m_environmentImage)     { vmaDestroyImage(alloc, m_environmentImage, m_environmentAlloc); m_environmentImage = VK_NULL_HANDLE; }

        uploadTexture2D(m_environmentImage, m_environmentAlloc, m_environmentImageView,
                        data, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                        VK_FORMAT_R32G32B32A32_SFLOAT, false);

        if (isExr) {
            free(data);
        } else {
            stbi_image_free(data);
        }

        if (m_iblEquirectDescSet != VK_NULL_HANDLE) {
            VkDescriptorImageInfo eqInfo{ m_environmentSampler, m_environmentImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = m_iblEquirectDescSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &eqInfo;
            vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
            bakeIblMaps();
        }
        core::Logger::info("HDRI/EXR loaded and baked successfully (" + std::to_string(width) + "x" + std::to_string(height) + "): " + path);
        return true;
    }

    void VulkanRenderer::loadEnvironmentHdri(int index) {
        m_selectedHdriIndex = std::clamp(index, 0, 7);

        const char* presetNames[] = {
            "sunset", "studio", "city", "courtyard", "forest", "interior", "night", "sunrise"
        };

        if (m_selectedHdriIndex >= 0 && m_selectedHdriIndex < 8) {
            const std::string name = presetNames[m_selectedHdriIndex];
            const std::vector<std::string> candidatePaths = {
                "assets/textures/" + name + ".hdr",
                "engine/assets/textures/" + name + ".hdr",
                "assets/textures/" + name + ".exr",
                "engine/assets/textures/" + name + ".exr",
                "../../engine/assets/textures/" + name + ".hdr",
                "../../engine/assets/textures/" + name + ".exr"
            };

            for (const auto& p : candidatePaths) {
                if (std::filesystem::exists(p)) {
                    if (loadEnvironmentHdriFile(p)) {
                        return;
                    }
                }
            }
        }
        VkDevice dev = m_context->getDevice();
        VmaAllocator alloc = m_context->getAllocator();

        if (m_environmentImageView) { vkDestroyImageView(dev, m_environmentImageView, nullptr); m_environmentImageView = VK_NULL_HANDLE; }
        if (m_environmentImage)     { vmaDestroyImage(alloc, m_environmentImage, m_environmentAlloc); m_environmentImage = VK_NULL_HANDLE; }

        const uint32_t envW = 512;
        const uint32_t envH = 256;
        std::vector<glm::vec4> pixels(envW * envH);

        glm::vec3 zenithCol, horizonCol, groundCol, sunDir;
        float sunPower = 18.0f;
        glm::vec3 sunTint(1.0f, 0.95f, 0.88f);

        switch (m_selectedHdriIndex) {
            case 0: // Sunset (Zachod slonca)
                zenithCol  = glm::vec3(0.08f, 0.10f, 0.32f);
                horizonCol = glm::vec3(1.70f, 0.50f, 0.12f);
                groundCol  = glm::vec3(0.08f, 0.05f, 0.04f);
                sunDir     = glm::normalize(glm::vec3(0.75f, 0.12f, 0.45f));
                sunPower   = 30.0f;
                sunTint    = glm::vec3(1.0f, 0.65f, 0.30f);
                break;
            case 1: // Studio (Neutralne studio fotograficzne)
                zenithCol  = glm::vec3(0.40f, 0.42f, 0.45f);
                horizonCol = glm::vec3(0.28f, 0.30f, 0.32f);
                groundCol  = glm::vec3(0.12f, 0.12f, 0.13f);
                sunDir     = glm::normalize(glm::vec3(0.0f, 0.95f, 0.20f));
                sunPower   = 12.0f;
                sunTint    = glm::vec3(1.0f, 1.0f, 1.0f);
                break;
            case 2: // City (Panorama miejska / Urban skyline)
                zenithCol  = glm::vec3(0.15f, 0.38f, 0.85f);
                horizonCol = glm::vec3(0.70f, 0.76f, 0.88f);
                groundCol  = glm::vec3(0.08f, 0.09f, 0.11f);
                sunDir     = glm::normalize(glm::vec3(0.40f, 0.75f, -0.30f));
                sunPower   = 20.0f;
                sunTint    = glm::vec3(1.0f, 0.96f, 0.88f);
                break;
            case 3: // Courtyard (Dziedziniec / Cieply sloneczny dzien)
                zenithCol  = glm::vec3(0.12f, 0.35f, 0.92f);
                horizonCol = glm::vec3(0.82f, 0.88f, 0.98f);
                groundCol  = glm::vec3(0.24f, 0.20f, 0.16f);
                sunDir     = glm::normalize(glm::vec3(0.35f, 0.85f, 0.20f));
                sunPower   = 24.0f;
                sunTint    = glm::vec3(1.0f, 0.98f, 0.92f);
                break;
            case 4: // Forest (Otoczenie lesne)
                zenithCol  = glm::vec3(0.10f, 0.30f, 0.78f);
                horizonCol = glm::vec3(0.40f, 0.62f, 0.32f);
                groundCol  = glm::vec3(0.06f, 0.16f, 0.05f);
                sunDir     = glm::normalize(glm::vec3(-0.45f, 0.65f, 0.45f));
                sunPower   = 18.0f;
                sunTint    = glm::vec3(1.0f, 0.94f, 0.82f);
                break;
            case 5: // Interior (Wnetrze studyjno-przemyslowe)
                zenithCol  = glm::vec3(0.25f, 0.22f, 0.20f);
                horizonCol = glm::vec3(0.45f, 0.38f, 0.30f);
                groundCol  = glm::vec3(0.16f, 0.14f, 0.12f);
                sunDir     = glm::normalize(glm::vec3(0.20f, 0.92f, 0.20f));
                sunPower   = 14.0f;
                sunTint    = glm::vec3(1.0f, 0.92f, 0.78f);
                break;
            case 6: // Night (Ciemna noc z gwiazdami i ksiezycem)
                zenithCol  = glm::vec3(0.008f, 0.012f, 0.035f);
                horizonCol = glm::vec3(0.025f, 0.035f, 0.075f);
                groundCol  = glm::vec3(0.004f, 0.004f, 0.006f);
                sunDir     = glm::normalize(glm::vec3(-0.60f, 0.55f, -0.40f));
                sunPower   = 6.0f;
                sunTint    = glm::vec3(0.65f, 0.80f, 1.0f);
                break;
            case 7: // Sunrise (Swit / Wschod slonca)
            default:
                zenithCol  = glm::vec3(0.14f, 0.22f, 0.62f);
                horizonCol = glm::vec3(1.50f, 0.68f, 0.42f);
                groundCol  = glm::vec3(0.08f, 0.08f, 0.10f);
                sunDir     = glm::normalize(glm::vec3(-0.80f, 0.10f, 0.35f));
                sunPower   = 22.0f;
                sunTint    = glm::vec3(1.0f, 0.82f, 0.60f);
                break;
        }

        auto frac = [](float x) { return x - std::floor(x); };

        for (uint32_t y = 0; y < envH; ++y) {
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(envH);
            const float theta = (0.5f - v) * glm::pi<float>();
            const float cosTheta = std::cos(theta);
            const float sinTheta = std::sin(theta);

            for (uint32_t x = 0; x < envW; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(envW);
                const float phi = (u - 0.5f) * glm::two_pi<float>();
                const glm::vec3 dir(cosTheta * std::cos(phi), sinTheta, cosTheta * std::sin(phi));

                glm::vec3 col;
                if (dir.y >= 0.0f) {
                    const float t = std::pow(dir.y, 0.48f);
                    col = glm::mix(horizonCol, zenithCol, t);
                } else {
                    const float t = std::clamp(-dir.y * 3.0f, 0.0f, 1.0f);
                    col = glm::mix(horizonCol * 0.45f, groundCol, t);
                }

                // Bogate cechy specyficzne dla poszczegolnych presetow
                if (m_selectedHdriIndex == 1) { // Studio: jasne softboxy sufitowe i boczne
                    if (dir.y > 0.75f && std::abs(dir.x) < 0.45f && std::abs(dir.z) < 0.65f) {
                        col += glm::vec3(12.0f);
                    } else if (std::abs(dir.x) > 0.88f && std::abs(dir.y) < 0.35f) {
                        col += glm::vec3(7.0f);
                    }
                } else if (m_selectedHdriIndex == 2) { // City: sylwetka wiezowcow na horyzoncie
                    if (dir.y >= 0.0f && dir.y < 0.08f) {
                        float skyline = 0.02f + 0.05f * std::abs(std::sin(phi * 18.0f) * std::cos(phi * 42.0f));
                        if (dir.y < skyline) {
                            col = glm::vec3(0.04f, 0.05f, 0.07f) + glm::vec3(0.4f, 0.3f, 0.1f) * (0.08f - dir.y);
                        }
                    }
                } else if (m_selectedHdriIndex == 5) { // Interior: pasy lamp sufitowych
                    if (dir.y > 0.60f && std::abs(std::fmod(std::abs(phi) * 4.0f, 1.0f) - 0.5f) < 0.12f) {
                        col += glm::vec3(9.0f, 8.0f, 6.0f);
                    }
                } else if (m_selectedHdriIndex == 6) { // Night: pole gwiazd w gornym polkolu
                    if (dir.y > 0.05f) {
                        float starHash = frac(std::sin(u * 12345.67f + v * 76543.21f) * 43758.5453f);
                        if (starHash > 0.994f) {
                            float starBright = (starHash - 0.994f) / 0.006f;
                            col += glm::vec3(starBright * 8.0f);
                        }
                    }
                }

                const float sunCos = std::max(glm::dot(dir, sunDir), 0.0f);
                col += sunTint * (std::pow(sunCos, 96.0f) * sunPower + std::pow(sunCos, 8.0f) * 0.25f);
                pixels[y * envW + x] = glm::vec4(col, 1.0f);
            }
        }

        uploadTexture2D(m_environmentImage, m_environmentAlloc, m_environmentImageView,
                        pixels.data(), envW, envH, VK_FORMAT_R32G32B32A32_SFLOAT, false);

        if (m_iblEquirectDescSet != VK_NULL_HANDLE) {
            VkDescriptorImageInfo eqInfo{ m_environmentSampler, m_environmentImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = m_iblEquirectDescSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &eqInfo;
            vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
            bakeIblMaps();
        }
    }

    void VulkanRenderer::createEnvironmentResources() {
        VkDevice dev = m_context->getDevice();
        VmaAllocator alloc = m_context->getAllocator();

        VkSamplerCreateInfo sampInfo{};
        sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampInfo.magFilter = VK_FILTER_LINEAR;
        sampInfo.minFilter = VK_FILTER_LINEAR;
        sampInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampInfo.maxLod = 16.0f;
        vkCreateSampler(dev, &sampInfo, nullptr, &m_environmentSampler);

        auto createCubemap = [&](uint32_t size, uint32_t mips, VkImageUsageFlags extraUsage,
                                 VkImage& img, VmaAllocation& mem, VkImageView& cubeView) {
            VkImageCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            info.imageType = VK_IMAGE_TYPE_2D;
            info.extent = { size, size, 1 };
            info.mipLevels = mips;
            info.arrayLayers = 6;
            info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | extraUsage;
            info.samples = VK_SAMPLE_COUNT_1_BIT;

            VmaAllocationCreateInfo vmaInfo{};
            vmaInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            vmaCreateImage(alloc, &info, &vmaInfo, &img, &mem, nullptr);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = img;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 6 };
            vkCreateImageView(dev, &viewInfo, nullptr, &cubeView);
        };

        createCubemap(ENV_CUBE_SIZE, ENV_CUBE_MIPS, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                      m_envCubeImage, m_envCubeAlloc, m_envCubeView);
        createCubemap(IRR_CUBE_SIZE, 1, 0, m_irradianceCubeImage, m_irradianceCubeAlloc, m_irradianceCubeView);
        createCubemap(PREFILTER_CUBE_SIZE, PREFILTER_CUBE_MIPS, 0, m_prefilterCubeImage, m_prefilterCubeAlloc, m_prefilterCubeView);

        for (uint32_t f = 0; f < 6; ++f) {
            VkImageViewCreateInfo fv{};
            fv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            fv.viewType = VK_IMAGE_VIEW_TYPE_2D;
            fv.format = VK_FORMAT_R16G16B16A16_SFLOAT;

            fv.image = m_envCubeImage;
            fv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, f, 1 };
            vkCreateImageView(dev, &fv, nullptr, &m_envCubeFaceViews[f]);

            fv.image = m_irradianceCubeImage;
            vkCreateImageView(dev, &fv, nullptr, &m_irradianceFaceViews[f]);

            for (uint32_t m = 0; m < PREFILTER_CUBE_MIPS; ++m) {
                fv.image = m_prefilterCubeImage;
                fv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, m, 1, f, 1 };
                vkCreateImageView(dev, &fv, nullptr, &m_prefilterFaceMipViews[m][f]);
            }
        }

        // 2D Split-Sum DFG LUT (RG16F)
        VkImageCreateInfo lutInfo{};
        lutInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        lutInfo.imageType = VK_IMAGE_TYPE_2D;
        lutInfo.extent = { BRDF_LUT_SIZE, BRDF_LUT_SIZE, 1 };
        lutInfo.mipLevels = 1;
        lutInfo.arrayLayers = 1;
        lutInfo.format = VK_FORMAT_R16G16_SFLOAT;
        lutInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        lutInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        lutInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        lutInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        VmaAllocationCreateInfo vmaInfo{};
        vmaInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        vmaCreateImage(alloc, &lutInfo, &vmaInfo, &m_brdfLutImage, &m_brdfLutAlloc, nullptr);

        VkImageViewCreateInfo lutView{};
        lutView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        lutView.image = m_brdfLutImage;
        lutView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        lutView.format = VK_FORMAT_R16G16_SFLOAT;
        lutView.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(dev, &lutView, nullptr, &m_brdfLutView);

        if (!m_singleTextureDescLayout) {
            VkDescriptorSetLayoutBinding b0{};
            b0.binding = 0;
            b0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b0.descriptorCount = 1;
            b0.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutCreateInfo dl{};
            dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            dl.bindingCount = 1;
            dl.pBindings = &b0;
            vkCreateDescriptorSetLayout(dev, &dl, nullptr, &m_singleTextureDescLayout);
        }

        VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_iblDescPool);

        VkDescriptorSetLayout layouts[2] = { m_singleTextureDescLayout, m_singleTextureDescLayout };
        VkDescriptorSetAllocateInfo dsAlloc{};
        dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAlloc.descriptorPool = m_iblDescPool;
        dsAlloc.descriptorSetCount = 2;
        dsAlloc.pSetLayouts = layouts;
        VkDescriptorSet sets[2]{};
        vkAllocateDescriptorSets(dev, &dsAlloc, sets);
        m_iblEquirectDescSet = sets[0];
        m_iblCubemapDescSet  = sets[1];

        VkPushConstantRange iblPc{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(IblPushConstants) };
        VkPipelineLayoutCreateInfo plInfo{};
        plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_singleTextureDescLayout;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &iblPc;
        vkCreatePipelineLayout(dev, &plInfo, nullptr, &m_iblCubePipelineLayout);

        VkPipelineLayoutCreateInfo lutPlInfo{};
        lutPlInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        vkCreatePipelineLayout(dev, &lutPlInfo, nullptr, &m_brdfLutPipelineLayout);

        createFullscreenPipeline("ibl_equirect_to_cube.frag.spv", m_iblCubePipelineLayout, VK_FORMAT_R16G16B16A16_SFLOAT, &m_equirectToCubePipeline);
        createFullscreenPipeline("ibl_irradiance.frag.spv", m_iblCubePipelineLayout, VK_FORMAT_R16G16B16A16_SFLOAT, &m_irradiancePipeline);
        createFullscreenPipeline("ibl_prefilter.frag.spv", m_iblCubePipelineLayout, VK_FORMAT_R16G16B16A16_SFLOAT, &m_prefilterPipeline);
        createFullscreenPipeline("ibl_brdf_lut.frag.spv", m_brdfLutPipelineLayout, VK_FORMAT_R16G16_SFLOAT, &m_brdfLutPipeline);

        VkDescriptorImageInfo cubeDesc{ m_environmentSampler, m_envCubeView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet cubeWrite{};
        cubeWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cubeWrite.dstSet = m_iblCubemapDescSet;
        cubeWrite.dstBinding = 0;
        cubeWrite.descriptorCount = 1;
        cubeWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cubeWrite.pImageInfo = &cubeDesc;
        vkUpdateDescriptorSets(dev, 1, &cubeWrite, 0, nullptr);

        loadEnvironmentHdri(m_selectedHdriIndex);
    }

    void VulkanRenderer::bakeIblMaps() {
        VkCommandBuffer cmd = beginOneShotCommands(m_context);

        auto drawFace = [&](VkImageView targetView, uint32_t size, VkPipeline pipe, VkDescriptorSet ds, const IblPushConstants* pc) {
            VkRenderingAttachmentInfo att{};
            att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            att.imageView = targetView;
            att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo ri{};
            ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            ri.renderArea = { {0, 0}, {size, size} };
            ri.layerCount = 1;
            ri.colorAttachmentCount = 1;
            ri.pColorAttachments = &att;

            vkCmdBeginRendering(cmd, &ri);
            VkViewport vp{ 0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f };
            VkRect2D sc{ {0, 0}, {size, size} };
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            if (ds != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_iblCubePipelineLayout, 0, 1, &ds, 0, nullptr);
            }
            if (pc != nullptr) {
                vkCmdPushConstants(cmd, m_iblCubePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(IblPushConstants), pc);
            }
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRendering(cmd);
        };

        // 1. Konwersja Equirectangular -> 6 ścian Cubemapy (Mip 0) + łańcuch mipmap dla GGX PDF
        transitionImage(cmd, m_envCubeImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        transitionImage(cmd, m_envCubeImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 1, ENV_CUBE_MIPS - 1, 0, 6,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

        for (int f = 0; f < 6; ++f) {
            IblPushConstants pc{ f, 0.0f, static_cast<float>(ENV_CUBE_SIZE), 0.0f };
            drawFace(m_envCubeFaceViews[f], ENV_CUBE_SIZE, m_equirectToCubePipeline, m_iblEquirectDescSet, &pc);
        }

        transitionImage(cmd, m_envCubeImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

        int32_t mipSize = static_cast<int32_t>(ENV_CUBE_SIZE);
        for (uint32_t m = 1; m < ENV_CUBE_MIPS; ++m) {
            const int32_t nextSize = std::max(1, mipSize / 2);
            VkImageBlit blit{};
            blit.srcOffsets[1] = { mipSize, mipSize, 1 };
            blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, 6 };
            blit.dstOffsets[1] = { nextSize, nextSize, 1 };
            blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 6 };
            vkCmdBlitImage(cmd, m_envCubeImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m_envCubeImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            transitionImage(cmd, m_envCubeImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 1, 0, 6,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

            if (m + 1 < ENV_CUBE_MIPS) {
                transitionImage(cmd, m_envCubeImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, m, 1, 0, 6,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
            } else {
                transitionImage(cmd, m_envCubeImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, m, 1, 0, 6,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            }
            mipSize = nextSize;
        }

        // 2. Splot mapy rozproszonej Irradiance (64x64)
        transitionImage(cmd, m_irradianceCubeImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        for (int f = 0; f < 6; ++f) {
            IblPushConstants pc{ f, 0.0f, static_cast<float>(ENV_CUBE_SIZE), 0.0f };
            drawFace(m_irradianceFaceViews[f], IRR_CUBE_SIZE, m_irradiancePipeline, m_iblCubemapDescSet, &pc);
        }
        transitionImage(cmd, m_irradianceCubeImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        // 3. Prefiltrowanie GGX dla 5 poziomów chropowatości (256x256 -> 16x16)
        transitionImage(cmd, m_prefilterCubeImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, PREFILTER_CUBE_MIPS, 0, 6,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        for (uint32_t m = 0; m < PREFILTER_CUBE_MIPS; ++m) {
            const uint32_t mSize = std::max(1u, PREFILTER_CUBE_SIZE >> m);
            const float rough = static_cast<float>(m) / static_cast<float>(PREFILTER_CUBE_MIPS - 1u);
            for (int f = 0; f < 6; ++f) {
                IblPushConstants pc{ f, rough, static_cast<float>(ENV_CUBE_SIZE), 0.0f };
                drawFace(m_prefilterFaceMipViews[m][f], mSize, m_prefilterPipeline, m_iblCubemapDescSet, &pc);
            }
        }
        transitionImage(cmd, m_prefilterCubeImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, PREFILTER_CUBE_MIPS, 0, 6,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        // 4. Jednorazowe całkowanie 2D BRDF Split-Sum LUT (512x512 RG16F)
        if (!m_brdfLutBaked) {
            transitionImage(cmd, m_brdfLutImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            drawFace(m_brdfLutView, BRDF_LUT_SIZE, m_brdfLutPipeline, VK_NULL_HANDLE, nullptr);
            transitionImage(cmd, m_brdfLutImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            m_brdfLutBaked = true;
        }

        endOneShotCommands(m_context, cmd);
        core::Logger::info("GPU Cubemap IBL baked (Sky 512, Irradiance 64, GGX Prefilter 256, DFG LUT 512).");
    }

    void VulkanRenderer::cleanupEnvironmentResources() {
        VkDevice dev = m_context->getDevice();
        VmaAllocator alloc = m_context->getAllocator();

        if (m_equirectToCubePipeline) { vkDestroyPipeline(dev, m_equirectToCubePipeline, nullptr); m_equirectToCubePipeline = VK_NULL_HANDLE; }
        if (m_irradiancePipeline)     { vkDestroyPipeline(dev, m_irradiancePipeline, nullptr); m_irradiancePipeline = VK_NULL_HANDLE; }
        if (m_prefilterPipeline)      { vkDestroyPipeline(dev, m_prefilterPipeline, nullptr); m_prefilterPipeline = VK_NULL_HANDLE; }
        if (m_brdfLutPipeline)        { vkDestroyPipeline(dev, m_brdfLutPipeline, nullptr); m_brdfLutPipeline = VK_NULL_HANDLE; }
        if (m_iblCubePipelineLayout)  { vkDestroyPipelineLayout(dev, m_iblCubePipelineLayout, nullptr); m_iblCubePipelineLayout = VK_NULL_HANDLE; }
        if (m_brdfLutPipelineLayout)  { vkDestroyPipelineLayout(dev, m_brdfLutPipelineLayout, nullptr); m_brdfLutPipelineLayout = VK_NULL_HANDLE; }
        if (m_iblDescPool)            { vkDestroyDescriptorPool(dev, m_iblDescPool, nullptr); m_iblDescPool = VK_NULL_HANDLE; }

        for (uint32_t f = 0; f < 6; ++f) {
            if (m_envCubeFaceViews[f])    { vkDestroyImageView(dev, m_envCubeFaceViews[f], nullptr); m_envCubeFaceViews[f] = VK_NULL_HANDLE; }
            if (m_irradianceFaceViews[f]) { vkDestroyImageView(dev, m_irradianceFaceViews[f], nullptr); m_irradianceFaceViews[f] = VK_NULL_HANDLE; }
            for (uint32_t m = 0; m < PREFILTER_CUBE_MIPS; ++m) {
                if (m_prefilterFaceMipViews[m][f]) { vkDestroyImageView(dev, m_prefilterFaceMipViews[m][f], nullptr); m_prefilterFaceMipViews[m][f] = VK_NULL_HANDLE; }
            }
        }

        auto destroyImg = [&](VkImage& img, VmaAllocation& mem, VkImageView& view) {
            if (view) { vkDestroyImageView(dev, view, nullptr); view = VK_NULL_HANDLE; }
            if (img)  { vmaDestroyImage(alloc, img, mem); img = VK_NULL_HANDLE; mem = VK_NULL_HANDLE; }
        };
        destroyImg(m_environmentImage, m_environmentAlloc, m_environmentImageView);
        destroyImg(m_envCubeImage, m_envCubeAlloc, m_envCubeView);
        destroyImg(m_irradianceCubeImage, m_irradianceCubeAlloc, m_irradianceCubeView);
        destroyImg(m_prefilterCubeImage, m_prefilterCubeAlloc, m_prefilterCubeView);
        destroyImg(m_brdfLutImage, m_brdfLutAlloc, m_brdfLutView);
        if (m_environmentSampler) { vkDestroySampler(dev, m_environmentSampler, nullptr); m_environmentSampler = VK_NULL_HANDLE; }
    }

    void VulkanRenderer::createSkyboxPipeline() {
        VkDevice dev = m_context->getDevice();

        VkPipelineLayoutCreateInfo plInfo{};
        plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_descriptorSetLayout;
        vkCreatePipelineLayout(dev, &plInfo, nullptr, &m_skyboxPipelineLayout);

        VkShaderModule vertMod = createShaderModule(loadSpvBinary("fullscreen.vert.spv"));
        VkShaderModule fragMod = createShaderModule(loadSpvBinary("skybox.frag.spv"));

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertMod;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragMod;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_FALSE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;

        const VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynStates;

        const VkFormat hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkPipelineRenderingCreateInfo ri{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        ri.colorAttachmentCount = 1;
        ri.pColorAttachmentFormats = &hdrFormat;
        ri.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

        VkGraphicsPipelineCreateInfo gp{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        gp.pNext = &ri;
        gp.stageCount = 2;
        gp.pStages = stages;
        gp.pVertexInputState = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb;
        gp.pDynamicState = &dyn;
        gp.layout = m_skyboxPipelineLayout;

        vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp, nullptr, &m_skyboxPipeline);
        vkDestroyShaderModule(dev, fragMod, nullptr);
        vkDestroyShaderModule(dev, vertMod, nullptr);
    }

    void VulkanRenderer::cleanupSkyboxPipeline() {
        VkDevice dev = m_context->getDevice();
        if (m_skyboxPipeline)       { vkDestroyPipeline(dev, m_skyboxPipeline, nullptr); m_skyboxPipeline = VK_NULL_HANDLE; }
        if (m_skyboxPipelineLayout) { vkDestroyPipelineLayout(dev, m_skyboxPipelineLayout, nullptr); m_skyboxPipelineLayout = VK_NULL_HANDLE; }
    }

    void VulkanRenderer::createPostProcessResources() {
        VkDevice dev = m_context->getDevice();

        if (!m_singleTextureDescLayout) {
            VkDescriptorSetLayoutBinding b0{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
            VkDescriptorSetLayoutCreateInfo dl{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &b0 };
            vkCreateDescriptorSetLayout(dev, &dl, nullptr, &m_singleTextureDescLayout);
        }
        if (!m_dualTextureDescLayout) {
            VkDescriptorSetLayoutBinding bindings[2] = {
                { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
            };
            VkDescriptorSetLayoutCreateInfo dl{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 2, bindings };
            vkCreateDescriptorSetLayout(dev, &dl, nullptr, &m_dualTextureDescLayout);
        }

        VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 24 };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 16;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_postProcessDescPool);

        auto allocSet = [&](VkDescriptorSetLayout layout) {
            VkDescriptorSet ds = VK_NULL_HANDLE;
            VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_postProcessDescPool, 1, &layout };
            vkAllocateDescriptorSets(dev, &ai, &ds);
            return ds;
        };

        m_gtaoDescSet         = allocSet(m_dualTextureDescLayout);
        m_gtaoBlurDescSet     = allocSet(m_dualTextureDescLayout);
        m_bloomDownHdrDescSet = allocSet(m_singleTextureDescLayout);
        for (uint32_t i = 0; i < BLOOM_MIP_COUNT; ++i) {
            m_bloomMipDescSets[i] = allocSet(m_singleTextureDescLayout);
        }
        m_compositeDescSet    = allocSet(m_dualTextureDescLayout);
        m_fxaaDescSet         = allocSet(m_singleTextureDescLayout);

        auto makeLayout = [&](VkDescriptorSetLayout dsLayout, uint32_t pcBytes, VkPipelineLayout* outLayout) {
            VkPushConstantRange pc{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, pcBytes };
            VkPipelineLayoutCreateInfo pl{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            pl.setLayoutCount = 1;
            pl.pSetLayouts = &dsLayout;
            pl.pushConstantRangeCount = 1;
            pl.pPushConstantRanges = &pc;
            vkCreatePipelineLayout(dev, &pl, nullptr, outLayout);
        };

        // gtaoPipelineLayout: vec4 params + vec4 screenParams + vec4 flags + mat4 prevViewProjFromView = 112 bytes <= 128 bytes
        makeLayout(m_dualTextureDescLayout, sizeof(glm::vec4) * 7, &m_gtaoPipelineLayout);
        // gtaoBlurPipelineLayout: vec4 params + vec4 screenParams = 32 bytes <= 128 bytes
        makeLayout(m_dualTextureDescLayout, sizeof(glm::vec4) * 2, &m_gtaoBlurPipelineLayout);
        makeLayout(m_singleTextureDescLayout, sizeof(glm::vec4),     &m_bloomPipelineLayout);
        makeLayout(m_dualTextureDescLayout,   sizeof(glm::vec4) * 2, &m_compositePipelineLayout);
        makeLayout(m_singleTextureDescLayout, sizeof(glm::vec4),     &m_fxaaPipelineLayout);

        createFullscreenPipeline("gtao.frag.spv",             m_gtaoPipelineLayout,      VK_FORMAT_R16G16B16A16_SFLOAT,&m_gtaoPipeline,      false);
        createFullscreenPipeline("gtao_blur.frag.spv",        m_gtaoBlurPipelineLayout,  VK_FORMAT_R16G16B16A16_SFLOAT,&m_gtaoBlurPipeline,  false);
        createFullscreenPipeline("bloom_downsample.frag.spv", m_bloomPipelineLayout,     VK_FORMAT_R16G16B16A16_SFLOAT,&m_bloomDownPipeline, false);
        createFullscreenPipeline("bloom_upsample.frag.spv",   m_bloomPipelineLayout,     VK_FORMAT_R16G16B16A16_SFLOAT,&m_bloomUpPipeline,   true);
        createFullscreenPipeline("composite.frag.spv",        m_compositePipelineLayout, VK_FORMAT_R8G8B8A8_UNORM,     &m_compositePipeline, false);
        createFullscreenPipeline("fxaa.frag.spv",             m_fxaaPipelineLayout,      VK_FORMAT_R8G8B8A8_UNORM,     &m_fxaaPipeline,      false);

        updatePostProcessDescriptorSets();
    }

    void VulkanRenderer::updatePostProcessDescriptorSets() {
        VkDevice dev = m_context->getDevice();
        std::vector<VkWriteDescriptorSet> writes;
        std::deque<VkDescriptorImageInfo> infos;
        writes.reserve(64);

        auto bind1 = [&](VkDescriptorSet ds, uint32_t binding, VkSampler samp, VkImageView view, VkImageLayout layout) {
            infos.push_back({ samp, view, layout });
            VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            w.dstSet = ds;
            w.dstBinding = binding;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &infos.back();
            writes.push_back(w);
        };

        bind1(m_gtaoDescSet,         0, m_depthSampler,  m_depthImageView,   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        bind1(m_gtaoDescSet,         1, m_linearSampler, m_hdrImageView,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        bind1(m_gtaoBlurDescSet,     0, m_linearSampler, m_aoImageView,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        bind1(m_gtaoBlurDescSet,     1, m_depthSampler,  m_depthImageView,   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        bind1(m_bloomDownHdrDescSet, 0, m_linearSampler, m_hdrImageView,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        for (uint32_t i = 0; i < BLOOM_MIP_COUNT; ++i) {
            bind1(m_bloomMipDescSets[i], 0, m_linearSampler, m_bloomMipViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        bind1(m_compositeDescSet, 0, m_linearSampler, m_hdrImageView,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        bind1(m_compositeDescSet, 1, m_linearSampler, m_bloomMipViews[0],  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        bind1(m_fxaaDescSet,      0, m_linearSampler, m_ldrTempImageView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (m_descriptorSet != VK_NULL_HANDLE) {
            bind1(m_descriptorSet, 7, m_linearSampler, m_aoBlurImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        for (const auto& pair : m_materialDescriptorSets) {
            if (pair.second != VK_NULL_HANDLE && pair.second != m_descriptorSet) {
                bind1(pair.second, 7, m_linearSampler, m_aoBlurImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }

        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void VulkanRenderer::cleanupPostProcessResources() {
        VkDevice dev = m_context->getDevice();
        auto destroyPipe = [&](VkPipeline& p) { if (p) { vkDestroyPipeline(dev, p, nullptr); p = VK_NULL_HANDLE; } };
        auto destroyLayout = [&](VkPipelineLayout& l) { if (l) { vkDestroyPipelineLayout(dev, l, nullptr); l = VK_NULL_HANDLE; } };

        destroyPipe(m_gtaoPipeline);
        destroyPipe(m_gtaoBlurPipeline);
        destroyPipe(m_bloomDownPipeline);
        destroyPipe(m_bloomUpPipeline);
        destroyPipe(m_compositePipeline);
        destroyPipe(m_fxaaPipeline);

        destroyLayout(m_gtaoPipelineLayout);
        destroyLayout(m_gtaoBlurPipelineLayout);
        destroyLayout(m_bloomPipelineLayout);
        destroyLayout(m_compositePipelineLayout);
        destroyLayout(m_fxaaPipelineLayout);

        if (m_postProcessDescPool)     { vkDestroyDescriptorPool(dev, m_postProcessDescPool, nullptr); m_postProcessDescPool = VK_NULL_HANDLE; }
        if (m_dualTextureDescLayout)   { vkDestroyDescriptorSetLayout(dev, m_dualTextureDescLayout, nullptr); m_dualTextureDescLayout = VK_NULL_HANDLE; }
        if (m_singleTextureDescLayout) { vkDestroyDescriptorSetLayout(dev, m_singleTextureDescLayout, nullptr); m_singleTextureDescLayout = VK_NULL_HANDLE; }

        if (m_viewportSampler) { vkDestroySampler(dev, m_viewportSampler, nullptr); m_viewportSampler = VK_NULL_HANDLE; }
        if (m_linearSampler)   { vkDestroySampler(dev, m_linearSampler, nullptr);   m_linearSampler   = VK_NULL_HANDLE; }
        if (m_depthSampler)    { vkDestroySampler(dev, m_depthSampler, nullptr);    m_depthSampler    = VK_NULL_HANDLE; }
    }

}
