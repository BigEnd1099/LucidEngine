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

#define TINYEXR_IMPLEMENTATION
#include "renderer/tinyexr.h"

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
        createPointShadowResources();
        createDefaultTextures();
        createEnvironmentResources();
        loadEnvironmentHdri(m_selectedHdriIndex);
        createDescriptorSets();
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
        if (m_context->getDevice()) {
            vkDeviceWaitIdle(m_context->getDevice());
            cleanupCaptureResources();
            cleanupParticlePipeline();
            cleanupPostProcessResources();
            cleanupShadowResources();
            cleanupPointShadowResources();
            cleanupEnvironmentResources();
            cleanupDefaultTextures();
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
        if (m_aoBlurImageView) {
            vkDestroyImageView(device, m_aoBlurImageView, nullptr);
            m_aoBlurImageView = VK_NULL_HANDLE;
        }
        if (m_aoBlurImage) {
            vmaDestroyImage(allocator, m_aoBlurImage, m_aoBlurAllocation);
            m_aoBlurImage = VK_NULL_HANDLE;
            m_aoBlurAllocation = VK_NULL_HANDLE;
        }
        if (m_aoImageView) {
            vkDestroyImageView(device, m_aoImageView, nullptr);
            m_aoImageView = VK_NULL_HANDLE;
        }
        if (m_aoImage) {
            vmaDestroyImage(allocator, m_aoImage, m_aoAllocation);
            m_aoImage = VK_NULL_HANDLE;
            m_aoAllocation = VK_NULL_HANDLE;
        }
        if (m_bloomImageViewB) {
            vkDestroyImageView(device, m_bloomImageViewB, nullptr);
            m_bloomImageViewB = VK_NULL_HANDLE;
        }
        if (m_bloomImageB) {
            vmaDestroyImage(allocator, m_bloomImageB, m_bloomAllocationB);
            m_bloomImageB = VK_NULL_HANDLE;
            m_bloomAllocationB = VK_NULL_HANDLE;
        }
        if (m_bloomImageViewA) {
            vkDestroyImageView(device, m_bloomImageViewA, nullptr);
            m_bloomImageViewA = VK_NULL_HANDLE;
        }
        if (m_bloomImageA) {
            vmaDestroyImage(allocator, m_bloomImageA, m_bloomAllocationA);
            m_bloomImageA = VK_NULL_HANDLE;
            m_bloomAllocationA = VK_NULL_HANDLE;
        }
        if (m_hdrImageView) {
            vkDestroyImageView(device, m_hdrImageView, nullptr);
            m_hdrImageView = VK_NULL_HANDLE;
        }
        if (m_hdrImage) {
            vmaDestroyImage(allocator, m_hdrImage, m_hdrAllocation);
            m_hdrImage = VK_NULL_HANDLE;
            m_hdrAllocation = VK_NULL_HANDLE;
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
        updatePostProcessDescriptorSets();
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

        // --- Depth Buffer (Depth/Stencil Attachment + Shader Sampled for GTAO) ---
        VkImageCreateInfo depthImageInfo = imageInfo;
        depthImageInfo.format = VK_FORMAT_D32_SFLOAT;
        depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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

        // --- HDR Scene Buffer (RGBA16F - Pure Uncompressed Radiance) ---
        VkImageCreateInfo hdrImageInfo = imageInfo;
        hdrImageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        hdrImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (vmaCreateImage(m_context->getAllocator(), &hdrImageInfo, &allocInfo, &m_hdrImage, &m_hdrAllocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("failed to create HDR image!");
        }
        VkImageViewCreateInfo hdrViewInfo = viewInfo;
        hdrViewInfo.image = m_hdrImage;
        hdrViewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        if (vkCreateImageView(m_context->getDevice(), &hdrViewInfo, nullptr, &m_hdrImageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create HDR image view!");
        }

        // --- Dual Kawase Bloom Ping-Pong Buffers (Half-Resolution RGBA16F) ---
        uint32_t bloomW = std::max(1u, (m_width + 1) / 2);
        uint32_t bloomH = std::max(1u, (m_height + 1) / 2);
        VkImageCreateInfo bloomImageInfo = hdrImageInfo;
        bloomImageInfo.extent.width = bloomW;
        bloomImageInfo.extent.height = bloomH;

        if (vmaCreateImage(m_context->getAllocator(), &bloomImageInfo, &allocInfo, &m_bloomImageA, &m_bloomAllocationA, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("failed to create Bloom Image A!");
        }
        VkImageViewCreateInfo bloomViewInfoA = hdrViewInfo;
        bloomViewInfoA.image = m_bloomImageA;
        if (vkCreateImageView(m_context->getDevice(), &bloomViewInfoA, nullptr, &m_bloomImageViewA) != VK_SUCCESS) {
            throw std::runtime_error("failed to create Bloom Image View A!");
        }

        if (vmaCreateImage(m_context->getAllocator(), &bloomImageInfo, &allocInfo, &m_bloomImageB, &m_bloomAllocationB, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("failed to create Bloom Image B!");
        }
        VkImageViewCreateInfo bloomViewInfoB = hdrViewInfo;
        bloomViewInfoB.image = m_bloomImageB;
        if (vkCreateImageView(m_context->getDevice(), &bloomViewInfoB, nullptr, &m_bloomImageViewB) != VK_SUCCESS) {
            throw std::runtime_error("failed to create Bloom Image View B!");
        }

        // --- Ground Truth Ambient Occlusion Buffers (R8_UNORM) ---
        VkImageCreateInfo aoImageInfo = imageInfo;
        aoImageInfo.format = VK_FORMAT_R8_UNORM;
        aoImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        if (vmaCreateImage(m_context->getAllocator(), &aoImageInfo, &allocInfo, &m_aoImage, &m_aoAllocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("failed to create GTAO image!");
        }
        VkImageViewCreateInfo aoViewInfo = viewInfo;
        aoViewInfo.image = m_aoImage;
        aoViewInfo.format = VK_FORMAT_R8_UNORM;
        if (vkCreateImageView(m_context->getDevice(), &aoViewInfo, nullptr, &m_aoImageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create GTAO image view!");
        }

        if (vmaCreateImage(m_context->getAllocator(), &aoImageInfo, &allocInfo, &m_aoBlurImage, &m_aoBlurAllocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("failed to create GTAO blur image!");
        }
        VkImageViewCreateInfo aoBlurViewInfo = aoViewInfo;
        aoBlurViewInfo.image = m_aoBlurImage;
        if (vkCreateImageView(m_context->getDevice(), &aoBlurViewInfo, nullptr, &m_aoBlurImageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create GTAO blur image view!");
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

    void VulkanRenderer::createPointShadowResources() {
        VkImageCreateInfo cubeInfo{};
        cubeInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        cubeInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        cubeInfo.imageType = VK_IMAGE_TYPE_2D;
        cubeInfo.extent.width = POINT_SHADOW_MAP_SIZE;
        cubeInfo.extent.height = POINT_SHADOW_MAP_SIZE;
        cubeInfo.extent.depth = 1;
        cubeInfo.mipLevels = 1;
        cubeInfo.arrayLayers = 6;
        cubeInfo.format = VK_FORMAT_D32_SFLOAT;
        cubeInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        cubeInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        cubeInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        cubeInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        cubeInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

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
        cubeViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        cubeViewInfo.subresourceRange.baseMipLevel = 0;
        cubeViewInfo.subresourceRange.levelCount = 1;
        cubeViewInfo.subresourceRange.baseArrayLayer = 0;
        cubeViewInfo.subresourceRange.layerCount = 6;

        if (vkCreateImageView(m_context->getDevice(), &cubeViewInfo, nullptr, &m_pointShadowCubeView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create point shadow cubemap view!");
        }

        for (uint32_t f = 0; f < 6; ++f) {
            VkImageViewCreateInfo faceViewInfo = cubeViewInfo;
            faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            faceViewInfo.subresourceRange.baseArrayLayer = f;
            faceViewInfo.subresourceRange.layerCount = 1;
            if (vkCreateImageView(m_context->getDevice(), &faceViewInfo, nullptr, &m_pointShadowFaceViews[f]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create point shadow face view " + std::to_string(f));
            }
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        if (vkCreateSampler(m_context->getDevice(), &samplerInfo, nullptr, &m_pointShadowSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create point shadow sampler!");
        }

        core::Logger::info("Omnidirectional Point Light Shadow Cubemap (512x512 x 6 faces) initialized");
    }

    void VulkanRenderer::cleanupPointShadowResources() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice device = m_context->getDevice();
        VmaAllocator allocator = m_context->getAllocator();

        if (m_pointShadowSampler) {
            vkDestroySampler(device, m_pointShadowSampler, nullptr);
            m_pointShadowSampler = VK_NULL_HANDLE;
        }
        for (uint32_t f = 0; f < 6; ++f) {
            if (m_pointShadowFaceViews[f]) {
                vkDestroyImageView(device, m_pointShadowFaceViews[f], nullptr);
                m_pointShadowFaceViews[f] = VK_NULL_HANDLE;
            }
        }
        if (m_pointShadowCubeView) {
            vkDestroyImageView(device, m_pointShadowCubeView, nullptr);
            m_pointShadowCubeView = VK_NULL_HANDLE;
        }
        if (m_pointShadowImage) {
            vmaDestroyImage(allocator, m_pointShadowImage, m_pointShadowAlloc);
            m_pointShadowImage = VK_NULL_HANDLE;
            m_pointShadowAlloc = VK_NULL_HANDLE;
        }
    }

    void VulkanRenderer::uploadTexture2D(VkImage& dstImage, VmaAllocation& dstAlloc, VkImageView& dstView,
                                         const void* pixels, uint32_t width, uint32_t height, VkFormat format, bool generateMips) {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice device = m_context->getDevice();
        VmaAllocator allocator = m_context->getAllocator();

        uint32_t bytesPerPixel = (format == VK_FORMAT_R32G32B32A32_SFLOAT) ? 16 : 4;
        VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * bytesPerPixel;

        VkBuffer stagingBuffer{VK_NULL_HANDLE};
        VmaAllocation stagingAlloc{VK_NULL_HANDLE};

        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = imageSize;
        bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo stgAllocInfo{};
        stgAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        stgAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo stgRes{};
        if (vmaCreateBuffer(allocator, &bufInfo, &stgAllocInfo, &stagingBuffer, &stagingAlloc, &stgRes) != VK_SUCCESS) {
            throw std::runtime_error("failed to create texture staging buffer!");
        }
        std::memcpy(stgRes.pMappedData, pixels, imageSize);

        uint32_t mipLevels = generateMips ? (static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1) : 1;

        VkImageCreateInfo imgInfo{};
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.extent = {width, height, 1};
        imgInfo.mipLevels = mipLevels;
        imgInfo.arrayLayers = 1;
        imgInfo.format = format;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo devAllocInfo{};
        devAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateImage(allocator, &imgInfo, &devAllocInfo, &dstImage, &dstAlloc, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("failed to create destination texture image!");
        }

        VkCommandBufferAllocateInfo cmdAlloc{};
        cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandPool = m_context->getCommandPool();
        cmdAlloc.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device, &cmdAlloc, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = 0;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.image = dstImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);

        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        if (mipLevels > 1) {
            int32_t mipW = width;
            int32_t mipH = height;

            for (uint32_t i = 1; i < mipLevels; ++i) {
                VkImageMemoryBarrier2 mipSrc{};
                mipSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                mipSrc.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                mipSrc.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                mipSrc.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                mipSrc.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                mipSrc.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                mipSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                mipSrc.image = dstImage;
                mipSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1};

                VkDependencyInfo dSrc{};
                dSrc.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dSrc.imageMemoryBarrierCount = 1;
                dSrc.pImageMemoryBarriers = &mipSrc;
                vkCmdPipelineBarrier2(cmd, &dSrc);

                int32_t nextW = std::max(1, mipW / 2);
                int32_t nextH = std::max(1, mipH / 2);

                VkImageBlit blit{};
                blit.srcOffsets[0] = {0, 0, 0};
                blit.srcOffsets[1] = {mipW, mipH, 1};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {nextW, nextH, 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};

                vkCmdBlitImage(cmd, dstImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &blit, VK_FILTER_LINEAR);

                mipSrc.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                mipSrc.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                mipSrc.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                mipSrc.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                mipSrc.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                mipSrc.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                vkCmdPipelineBarrier2(cmd, &dSrc);

                mipW = nextW;
                mipH = nextH;
            }

            VkImageMemoryBarrier2 finalMip{};
            finalMip.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            finalMip.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            finalMip.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            finalMip.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            finalMip.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            finalMip.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            finalMip.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            finalMip.image = dstImage;
            finalMip.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mipLevels - 1, 1, 0, 1};

            VkDependencyInfo dFinal{};
            dFinal.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dFinal.imageMemoryBarrierCount = 1;
            dFinal.pImageMemoryBarriers = &finalMip;
            vkCmdPipelineBarrier2(cmd, &dFinal);
        } else {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier2(cmd, &depInfo);
        }

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_context->getGraphicsQueue());

        vkFreeCommandBuffers(device, m_context->getCommandPool(), 1, &cmd);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = dstImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
        if (vkCreateImageView(device, &viewInfo, nullptr, &dstView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create texture image view!");
        }
    }

    void VulkanRenderer::createDefaultTextures() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice device = m_context->getDevice();

        // 1. Sampler for material textures
        VkSamplerCreateInfo sInfo{};
        sInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sInfo.magFilter = VK_FILTER_LINEAR;
        sInfo.minFilter = VK_FILTER_LINEAR;
        sInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sInfo.maxAnisotropy = 1.0f;
        if (vkCreateSampler(device, &sInfo, nullptr, &m_defaultTextureSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create default texture sampler!");
        }

        // 2. Default Albedo (1x1 pure white 0xFFFFFFFF)
        uint32_t whitePixel = 0xFFFFFFFF;
        uploadTexture2D(m_defaultAlbedoImage, m_defaultAlbedoAlloc, m_defaultAlbedoView, &whitePixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, false);

        // 3. Default Tangent Normal (1x1 flat normal pointing (0, 0, 1) -> RGBA = 128, 128, 255, 255)
        uint8_t normalPixel[4] = {128, 128, 255, 255};
        uploadTexture2D(m_defaultNormalImage, m_defaultNormalAlloc, m_defaultNormalView, normalPixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, false);

        // 4. Default ORM (1x1: AO=1.0, Roughness=0.5, Metallic=0.0 -> RGBA = 255, 128, 0, 255)
        uint8_t ormPixel[4] = {255, 128, 0, 255};
        uploadTexture2D(m_defaultOrmImage, m_defaultOrmAlloc, m_defaultOrmView, ormPixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, false);

        core::Logger::info("Default 1x1 PBR Material Textures (Albedo, Normal, ORM) initialized");
    }

    void VulkanRenderer::cleanupDefaultTextures() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice device = m_context->getDevice();
        VmaAllocator allocator = m_context->getAllocator();

        if (m_defaultTextureSampler) { vkDestroySampler(device, m_defaultTextureSampler, nullptr); m_defaultTextureSampler = VK_NULL_HANDLE; }
        if (m_defaultOrmView) { vkDestroyImageView(device, m_defaultOrmView, nullptr); m_defaultOrmView = VK_NULL_HANDLE; }
        if (m_defaultOrmImage) { vmaDestroyImage(allocator, m_defaultOrmImage, m_defaultOrmAlloc); m_defaultOrmImage = VK_NULL_HANDLE; }
        if (m_defaultNormalView) { vkDestroyImageView(device, m_defaultNormalView, nullptr); m_defaultNormalView = VK_NULL_HANDLE; }
        if (m_defaultNormalImage) { vmaDestroyImage(allocator, m_defaultNormalImage, m_defaultNormalAlloc); m_defaultNormalImage = VK_NULL_HANDLE; }
        if (m_defaultAlbedoView) { vkDestroyImageView(device, m_defaultAlbedoView, nullptr); m_defaultAlbedoView = VK_NULL_HANDLE; }
        if (m_defaultAlbedoImage) { vmaDestroyImage(allocator, m_defaultAlbedoImage, m_defaultAlbedoAlloc); m_defaultAlbedoImage = VK_NULL_HANDLE; }
    }

    void VulkanRenderer::createEnvironmentResources() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice device = m_context->getDevice();

        VkSamplerCreateInfo sInfo{};
        sInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sInfo.magFilter = VK_FILTER_LINEAR;
        sInfo.minFilter = VK_FILTER_LINEAR;
        sInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sInfo.maxAnisotropy = 1.0f;
        sInfo.minLod = 0.0f;
        sInfo.maxLod = 12.0f;
        if (vkCreateSampler(device, &sInfo, nullptr, &m_environmentSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create environment sampler!");
        }
    }

    void VulkanRenderer::cleanupEnvironmentResources() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice device = m_context->getDevice();
        VmaAllocator allocator = m_context->getAllocator();

        if (m_environmentSampler) { vkDestroySampler(device, m_environmentSampler, nullptr); m_environmentSampler = VK_NULL_HANDLE; }
        if (m_environmentImageView) { vkDestroyImageView(device, m_environmentImageView, nullptr); m_environmentImageView = VK_NULL_HANDLE; }
        if (m_environmentImage) { vmaDestroyImage(allocator, m_environmentImage, m_environmentAlloc); m_environmentImage = VK_NULL_HANDLE; }
    }

    void VulkanRenderer::loadEnvironmentHdri(int index) {
        if (index < 0 || index > 7) index = 0;
        m_selectedHdriIndex = index;

        static const char* s_hdriFiles[8] = {
            "sunset.exr",
            "studio.exr",
            "city.exr",
            "courtyard.exr",
            "forest.exr",
            "interior.exr",
            "night.exr",
            "sunrise.exr"
        };

        const char* targetFile = s_hdriFiles[m_selectedHdriIndex];
        std::vector<std::string> searchPaths = {
            std::string("engine/assets/textures/") + targetFile,
            std::string("assets/textures/") + targetFile,
            std::string("../engine/assets/textures/") + targetFile,
            std::string("../../engine/assets/textures/") + targetFile,
            std::string("textures/") + targetFile,
            targetFile
        };

        float* out_rgba = nullptr;
        int width = 0, height = 0;
        const char* err = nullptr;
        bool loaded = false;

        for (const auto& path : searchPaths) {
            if (std::filesystem::exists(path)) {
                int ret = LoadEXR(&out_rgba, &width, &height, path.c_str(), &err);
                if (ret == TINYEXR_SUCCESS && out_rgba && width > 0 && height > 0) {
                    loaded = true;
                    core::Logger::info("Loaded HDRI Environment EXR: " + path + " (" + std::to_string(width) + "x" + std::to_string(height) + ")");
                    break;
                } else if (err) {
                    FreeEXRErrorMessage(err);
                    err = nullptr;
                }
            }
        }

        if (m_context && m_context->getDevice()) {
            vkDeviceWaitIdle(m_context->getDevice());
        }

        if (m_environmentImageView) {
            vkDestroyImageView(m_context->getDevice(), m_environmentImageView, nullptr);
            m_environmentImageView = VK_NULL_HANDLE;
        }
        if (m_environmentImage) {
            vmaDestroyImage(m_context->getAllocator(), m_environmentImage, m_environmentAlloc);
            m_environmentImage = VK_NULL_HANDLE;
        }

        if (loaded && out_rgba) {
            uploadTexture2D(m_environmentImage, m_environmentAlloc, m_environmentImageView, out_rgba, width, height, VK_FORMAT_R32G32B32A32_SFLOAT, true);
            free(out_rgba);
        } else {
            // High-fidelity procedural HDRI gradient fallback (Sunset / Studio / Night / Horizon)
            const uint32_t fw = 512, fh = 256;
            std::vector<glm::vec4> fallbackData(fw * fh);
            for (uint32_t y = 0; y < fh; ++y) {
                float v = static_cast<float>(y) / static_cast<float>(fh);
                for (uint32_t x = 0; x < fw; ++x) {
                    float u = static_cast<float>(x) / static_cast<float>(fw);
                    glm::vec3 c;
                    if (m_selectedHdriIndex == 0) { // Sunset
                        glm::vec3 skyTop(0.12f, 0.22f, 0.45f);
                        glm::vec3 horizon(1.85f, 0.85f, 0.35f);
                        glm::vec3 ground(0.08f, 0.06f, 0.05f);
                        c = (v < 0.5f) ? glm::mix(skyTop, horizon, v * 2.0f) : glm::mix(horizon, ground, (v - 0.5f) * 2.0f);
                    } else if (m_selectedHdriIndex == 1) { // Studio
                        glm::vec3 lightGrey(1.2f, 1.25f, 1.3f);
                        glm::vec3 darkGrey(0.3f, 0.32f, 0.35f);
                        c = glm::mix(lightGrey, darkGrey, v);
                    } else if (m_selectedHdriIndex == 6) { // Night
                        glm::vec3 nightSky(0.02f, 0.03f, 0.08f);
                        glm::vec3 ground(0.01f, 0.01f, 0.02f);
                        c = glm::mix(nightSky, ground, v);
                    } else { // Generic atmospheric daylight
                        glm::vec3 sky(0.35f, 0.55f, 0.95f);
                        glm::vec3 ground(0.15f, 0.12f, 0.10f);
                        c = glm::mix(sky, ground, v);
                    }
                    fallbackData[y * fw + x] = glm::vec4(c, 1.0f);
                }
            }
            uploadTexture2D(m_environmentImage, m_environmentAlloc, m_environmentImageView, fallbackData.data(), fw, fh, VK_FORMAT_R32G32B32A32_SFLOAT, true);
            core::Logger::info("Initialized high-fidelity procedural HDRI Environment: " + std::string(s_hdriFiles[m_selectedHdriIndex]));
        }

        // If m_descriptorSet exists, update binding 3
        if (m_descriptorSet && m_environmentSampler && m_environmentImageView) {
            VkDescriptorImageInfo envInfo{};
            envInfo.sampler = m_environmentSampler;
            envInfo.imageView = m_environmentImageView;
            envInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writeEnv{};
            writeEnv.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeEnv.dstSet = m_descriptorSet;
            writeEnv.dstBinding = 3;
            writeEnv.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writeEnv.descriptorCount = 1;
            writeEnv.pImageInfo = &envInfo;
            vkUpdateDescriptorSets(m_context->getDevice(), 1, &writeEnv, 0, nullptr);
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

        std::vector<VkDescriptorSetLayoutBinding> bindings(7);
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 3: Environment Map (Equirectangular HDRI IBL)
        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 4: Albedo Map
        bindings[4].binding = 4;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 5: Tangent Normal Map
        bindings[5].binding = 5;
        bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 6: ORM Map (AO, Roughness, Metallic)
        bindings[6].binding = 6;
        bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_context->getDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout!");
        }

        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32 },
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

        VkDescriptorImageInfo cubeImageInfo{};
        cubeImageInfo.sampler = m_pointShadowSampler;
        cubeImageInfo.imageView = m_pointShadowCubeView;
        cubeImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo envInfo{};
        envInfo.sampler = m_environmentSampler;
        envInfo.imageView = m_environmentImageView;
        envInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo albedoInfo{};
        albedoInfo.sampler = m_defaultTextureSampler;
        albedoInfo.imageView = m_defaultAlbedoView;
        albedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo normalInfo{};
        normalInfo.sampler = m_defaultTextureSampler;
        normalInfo.imageView = m_defaultNormalView;
        normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo ormInfo{};
        ormInfo.sampler = m_defaultTextureSampler;
        ormInfo.imageView = m_defaultOrmView;
        ormInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::vector<VkWriteDescriptorSet> descriptorWrites(7);
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

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = m_descriptorSet;
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &cubeImageInfo;

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = m_descriptorSet;
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pImageInfo = &envInfo;

        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = m_descriptorSet;
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pImageInfo = &albedoInfo;

        descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5].dstSet = m_descriptorSet;
        descriptorWrites[5].dstBinding = 5;
        descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[5].descriptorCount = 1;
        descriptorWrites[5].pImageInfo = &normalInfo;

        descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[6].dstSet = m_descriptorSet;
        descriptorWrites[6].dstBinding = 6;
        descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[6].descriptorCount = 1;
        descriptorWrites[6].pImageInfo = &ormInfo;

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

        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
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

        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
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

    void VulkanRenderer::createPostProcessResources() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice device = m_context->getDevice();

        // 1a. Depth Sampler for GTAO (Nearest)
        VkSamplerCreateInfo depthSamplerInfo{};
        depthSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        depthSamplerInfo.magFilter = VK_FILTER_NEAREST;
        depthSamplerInfo.minFilter = VK_FILTER_NEAREST;
        depthSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        depthSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        depthSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        depthSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        depthSamplerInfo.maxAnisotropy = 1.0f;
        depthSamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        if (vkCreateSampler(device, &depthSamplerInfo, nullptr, &m_depthSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create depth sampler for GTAO!");
        }

        // 1b. Bilinear Sampler with Clamp to Edge
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        if (vkCreateSampler(device, &samplerInfo, nullptr, &m_linearSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create linear post-process sampler!");
        }

        // 2. Descriptor Set Layouts
        // Single Texture layout (Binding 0: Combined Image Sampler)
        VkDescriptorSetLayoutBinding singleBinding{};
        singleBinding.binding = 0;
        singleBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        singleBinding.descriptorCount = 1;
        singleBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo singleLayoutInfo{};
        singleLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        singleLayoutInfo.bindingCount = 1;
        singleLayoutInfo.pBindings = &singleBinding;
        if (vkCreateDescriptorSetLayout(device, &singleLayoutInfo, nullptr, &m_singleTextureDescLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create single texture descriptor set layout!");
        }

        // Dual Texture layout (Binding 0: Scene HDR, Binding 1: Bloom Texture)
        VkDescriptorSetLayoutBinding dualBindings[2]{};
        dualBindings[0].binding = 0;
        dualBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        dualBindings[0].descriptorCount = 1;
        dualBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        dualBindings[1].binding = 1;
        dualBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        dualBindings[1].descriptorCount = 1;
        dualBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dualLayoutInfo{};
        dualLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dualLayoutInfo.bindingCount = 2;
        dualLayoutInfo.pBindings = dualBindings;
        if (vkCreateDescriptorSetLayout(device, &dualLayoutInfo, nullptr, &m_dualTextureDescLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create dual texture descriptor set layout!");
        }

        // Triple Texture layout (Binding 0: HDR Scene, Binding 1: Bloom, Binding 2: GTAO)
        VkDescriptorSetLayoutBinding tripleBindings[3]{};
        for (uint32_t b = 0; b < 3; ++b) {
            tripleBindings[b].binding = b;
            tripleBindings[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            tripleBindings[b].descriptorCount = 1;
            tripleBindings[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo tripleLayoutInfo{};
        tripleLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        tripleLayoutInfo.bindingCount = 3;
        tripleLayoutInfo.pBindings = tripleBindings;
        if (vkCreateDescriptorSetLayout(device, &tripleLayoutInfo, nullptr, &m_tripleTextureDescLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create triple texture descriptor set layout!");
        }

        // 3. Post-Process Descriptor Pool
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 32;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 8;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_postProcessDescPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create post-process descriptor pool!");
        }

        // 4. Allocate Descriptor Sets
        VkDescriptorSetAllocateInfo allocInfoSingle{};
        allocInfoSingle.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfoSingle.descriptorPool = m_postProcessDescPool;
        allocInfoSingle.descriptorSetCount = 1;
        allocInfoSingle.pSetLayouts = &m_singleTextureDescLayout;

        if (vkAllocateDescriptorSets(device, &allocInfoSingle, &m_bloomDownDescSet) != VK_SUCCESS ||
            vkAllocateDescriptorSets(device, &allocInfoSingle, &m_kawaseDescSetA) != VK_SUCCESS ||
            vkAllocateDescriptorSets(device, &allocInfoSingle, &m_kawaseDescSetB) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate single texture descriptor sets!");
        }

        // Allocate GTAO descriptor sets
        if (vkAllocateDescriptorSets(device, &allocInfoSingle, &m_gtaoDescSet) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate GTAO descriptor set!");
        }

        VkDescriptorSetAllocateInfo allocInfoDual{};
        allocInfoDual.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfoDual.descriptorPool = m_postProcessDescPool;
        allocInfoDual.descriptorSetCount = 1;
        allocInfoDual.pSetLayouts = &m_dualTextureDescLayout;
        if (vkAllocateDescriptorSets(device, &allocInfoDual, &m_gtaoBlurDescSet) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate GTAO blur descriptor set!");
        }

        VkDescriptorSetAllocateInfo allocInfoTriple{};
        allocInfoTriple.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfoTriple.descriptorPool = m_postProcessDescPool;
        allocInfoTriple.descriptorSetCount = 1;
        allocInfoTriple.pSetLayouts = &m_tripleTextureDescLayout;
        if (vkAllocateDescriptorSets(device, &allocInfoTriple, &m_compositeDescSet) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate composite descriptor set!");
        }

        // Update descriptors with current images
        updatePostProcessDescriptorSets();

        // 5. Pipeline Layouts
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(glm::vec4);

        VkPipelineLayoutCreateInfo bloomDownLayoutInfo{};
        bloomDownLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        bloomDownLayoutInfo.setLayoutCount = 1;
        bloomDownLayoutInfo.pSetLayouts = &m_singleTextureDescLayout;
        bloomDownLayoutInfo.pushConstantRangeCount = 1;
        bloomDownLayoutInfo.pPushConstantRanges = &pcRange;
        if (vkCreatePipelineLayout(device, &bloomDownLayoutInfo, nullptr, &m_bloomDownPipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create bloom downsample pipeline layout!");
        }

        VkPipelineLayoutCreateInfo kawaseLayoutInfo = bloomDownLayoutInfo;
        if (vkCreatePipelineLayout(device, &kawaseLayoutInfo, nullptr, &m_kawasePipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create kawase pipeline layout!");
        }

        // GTAO Pipeline Layout (Push constants: 32 bytes)
        VkPushConstantRange gtaoPcRange{};
        gtaoPcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        gtaoPcRange.offset = 0;
        gtaoPcRange.size = sizeof(glm::vec4) * 2;

        VkPipelineLayoutCreateInfo gtaoLayoutInfo{};
        gtaoLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        gtaoLayoutInfo.setLayoutCount = 1;
        gtaoLayoutInfo.pSetLayouts = &m_singleTextureDescLayout;
        gtaoLayoutInfo.pushConstantRangeCount = 1;
        gtaoLayoutInfo.pPushConstantRanges = &gtaoPcRange;
        if (vkCreatePipelineLayout(device, &gtaoLayoutInfo, nullptr, &m_gtaoPipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create GTAO pipeline layout!");
        }

        VkPipelineLayoutCreateInfo gtaoBlurLayoutInfo{};
        gtaoBlurLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        gtaoBlurLayoutInfo.setLayoutCount = 1;
        gtaoBlurLayoutInfo.pSetLayouts = &m_dualTextureDescLayout;
        gtaoBlurLayoutInfo.pushConstantRangeCount = 1;
        gtaoBlurLayoutInfo.pPushConstantRanges = &pcRange;
        if (vkCreatePipelineLayout(device, &gtaoBlurLayoutInfo, nullptr, &m_gtaoBlurPipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create GTAO blur pipeline layout!");
        }

        VkPushConstantRange compPcRange{};
        compPcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        compPcRange.offset = 0;
        compPcRange.size = sizeof(glm::vec4) * 2;

        VkPipelineLayoutCreateInfo compLayoutInfo{};
        compLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        compLayoutInfo.setLayoutCount = 1;
        compLayoutInfo.pSetLayouts = &m_tripleTextureDescLayout;
        compLayoutInfo.pushConstantRangeCount = 1;
        compLayoutInfo.pPushConstantRanges = &compPcRange;
        if (vkCreatePipelineLayout(device, &compLayoutInfo, nullptr, &m_compositePipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create composite pipeline layout!");
        }

        // 6. Helper lambda to create Fullscreen Graphics Pipelines
        auto createFullscreenPipeline = [&](const std::string& fragShaderName,
                                            VkPipelineLayout layout,
                                            VkFormat targetFormat,
                                            VkPipeline* outPipeline) {
            auto vertShaderCode = utils::readFile("fullscreen.vert.spv");
            auto fragShaderCode = utils::readFile(fragShaderName);

            auto createShaderMod = [&](const std::vector<char>& code) {
                VkShaderModuleCreateInfo ci{};
                ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                ci.codeSize = code.size();
                ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
                VkShaderModule mod;
                if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS) {
                    throw std::runtime_error("failed to create shader module for " + fragShaderName);
                }
                return mod;
            };

            VkShaderModule vertMod = createShaderMod(vertShaderCode);
            VkShaderModule fragMod = createShaderMod(fragShaderCode);

            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vertMod;
            stages[0].pName = "main";

            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fragMod;
            stages[1].pName = "main";

            VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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

            VkPipelineColorBlendAttachmentState colorBlend{};
            colorBlend.blendEnable = VK_FALSE;
            colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

            VkPipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlend;

            VkPipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencil.depthTestEnable = VK_FALSE;
            depthStencil.depthWriteEnable = VK_FALSE;

            std::vector<VkDynamicState> dynamicStates = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
            };
            VkPipelineDynamicStateCreateInfo dynamicState{};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
            dynamicState.pDynamicStates = dynamicStates.data();

            VkPipelineRenderingCreateInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachmentFormats = &targetFormat;

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
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = layout;
            pipelineInfo.renderPass = VK_NULL_HANDLE;

            if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, outPipeline) != VK_SUCCESS) {
                throw std::runtime_error("failed to create fullscreen pipeline for " + fragShaderName);
            }

            vkDestroyShaderModule(device, fragMod, nullptr);
            vkDestroyShaderModule(device, vertMod, nullptr);
        };

        createFullscreenPipeline("gtao.frag.spv", m_gtaoPipelineLayout, VK_FORMAT_R8_UNORM, &m_gtaoPipeline);
        createFullscreenPipeline("gtao_blur.frag.spv", m_gtaoBlurPipelineLayout, VK_FORMAT_R8_UNORM, &m_gtaoBlurPipeline);
        createFullscreenPipeline("bloom_downsample.frag.spv", m_bloomDownPipelineLayout, VK_FORMAT_R16G16B16A16_SFLOAT, &m_bloomDownPipeline);
        createFullscreenPipeline("bloom_kawase.frag.spv", m_kawasePipelineLayout, VK_FORMAT_R16G16B16A16_SFLOAT, &m_kawasePipeline);
        createFullscreenPipeline("composite.frag.spv", m_compositePipelineLayout, VK_FORMAT_R8G8B8A8_UNORM, &m_compositePipeline);

        core::Logger::info("GTAO, Dual Kawase Bloom & ACES Filmic Tone Mapping HDR Pipelines initialized");
    }

    void VulkanRenderer::updatePostProcessDescriptorSets() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice device = m_context->getDevice();
        if (!m_linearSampler || !m_depthSampler || !m_hdrImageView || !m_bloomImageViewA || !m_bloomImageViewB ||
            !m_depthImageView || !m_aoImageView || !m_aoBlurImageView || !m_bloomDownDescSet) return;

        // Downsample: samples m_hdrImageView
        VkDescriptorImageInfo hdrInfo{};
        hdrInfo.sampler = m_linearSampler;
        hdrInfo.imageView = m_hdrImageView;
        hdrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writeDown{};
        writeDown.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDown.dstSet = m_bloomDownDescSet;
        writeDown.dstBinding = 0;
        writeDown.dstArrayElement = 0;
        writeDown.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDown.descriptorCount = 1;
        writeDown.pImageInfo = &hdrInfo;

        // Kawase A: samples m_bloomImageViewA
        VkDescriptorImageInfo bloomAInfo{};
        bloomAInfo.sampler = m_linearSampler;
        bloomAInfo.imageView = m_bloomImageViewA;
        bloomAInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writeKawaseA{};
        writeKawaseA.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeKawaseA.dstSet = m_kawaseDescSetA;
        writeKawaseA.dstBinding = 0;
        writeKawaseA.dstArrayElement = 0;
        writeKawaseA.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeKawaseA.descriptorCount = 1;
        writeKawaseA.pImageInfo = &bloomAInfo;

        // Kawase B: samples m_bloomImageViewB
        VkDescriptorImageInfo bloomBInfo{};
        bloomBInfo.sampler = m_linearSampler;
        bloomBInfo.imageView = m_bloomImageViewB;
        bloomBInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writeKawaseB{};
        writeKawaseB.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeKawaseB.dstSet = m_kawaseDescSetB;
        writeKawaseB.dstBinding = 0;
        writeKawaseB.dstArrayElement = 0;
        writeKawaseB.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeKawaseB.descriptorCount = 1;
        writeKawaseB.pImageInfo = &bloomBInfo;

        // GTAO: binding 0 = depth buffer
        VkDescriptorImageInfo depthInfo{};
        depthInfo.sampler = m_depthSampler;
        depthInfo.imageView = m_depthImageView;
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writeGtao{};
        writeGtao.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeGtao.dstSet = m_gtaoDescSet;
        writeGtao.dstBinding = 0;
        writeGtao.dstArrayElement = 0;
        writeGtao.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeGtao.descriptorCount = 1;
        writeGtao.pImageInfo = &depthInfo;

        // GTAO Blur: binding 0 = raw AO image, binding 1 = depth buffer
        VkDescriptorImageInfo aoRawInfo{};
        aoRawInfo.sampler = m_linearSampler;
        aoRawInfo.imageView = m_aoImageView;
        aoRawInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writeGtaoBlur0{};
        writeGtaoBlur0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeGtaoBlur0.dstSet = m_gtaoBlurDescSet;
        writeGtaoBlur0.dstBinding = 0;
        writeGtaoBlur0.dstArrayElement = 0;
        writeGtaoBlur0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeGtaoBlur0.descriptorCount = 1;
        writeGtaoBlur0.pImageInfo = &aoRawInfo;

        VkWriteDescriptorSet writeGtaoBlur1{};
        writeGtaoBlur1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeGtaoBlur1.dstSet = m_gtaoBlurDescSet;
        writeGtaoBlur1.dstBinding = 1;
        writeGtaoBlur1.dstArrayElement = 0;
        writeGtaoBlur1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeGtaoBlur1.descriptorCount = 1;
        writeGtaoBlur1.pImageInfo = &depthInfo;

        // Composite: binding 0 = scene HDR, binding 1 = blurred bloom (m_bloomImageViewB), binding 2 = blurred AO (m_aoBlurImageView)
        VkDescriptorImageInfo aoBlurInfo{};
        aoBlurInfo.sampler = m_linearSampler;
        aoBlurInfo.imageView = m_aoBlurImageView;
        aoBlurInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writeComp0{};
        writeComp0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeComp0.dstSet = m_compositeDescSet;
        writeComp0.dstBinding = 0;
        writeComp0.dstArrayElement = 0;
        writeComp0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeComp0.descriptorCount = 1;
        writeComp0.pImageInfo = &hdrInfo;

        VkWriteDescriptorSet writeComp1{};
        writeComp1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeComp1.dstSet = m_compositeDescSet;
        writeComp1.dstBinding = 1;
        writeComp1.dstArrayElement = 0;
        writeComp1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeComp1.descriptorCount = 1;
        writeComp1.pImageInfo = &bloomBInfo;

        VkWriteDescriptorSet writeComp2{};
        writeComp2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeComp2.dstSet = m_compositeDescSet;
        writeComp2.dstBinding = 2;
        writeComp2.dstArrayElement = 0;
        writeComp2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeComp2.descriptorCount = 1;
        writeComp2.pImageInfo = &aoBlurInfo;

        VkWriteDescriptorSet writes[] = {writeDown, writeKawaseA, writeKawaseB, writeGtao, writeGtaoBlur0, writeGtaoBlur1, writeComp0, writeComp1, writeComp2};
        vkUpdateDescriptorSets(device, 9, writes, 0, nullptr);
    }

    void VulkanRenderer::cleanupPostProcessResources() {
        if (!m_context || !m_context->getDevice()) return;
        VkDevice device = m_context->getDevice();

        if (m_gtaoPipeline) { vkDestroyPipeline(device, m_gtaoPipeline, nullptr); m_gtaoPipeline = VK_NULL_HANDLE; }
        if (m_gtaoPipelineLayout) { vkDestroyPipelineLayout(device, m_gtaoPipelineLayout, nullptr); m_gtaoPipelineLayout = VK_NULL_HANDLE; }

        if (m_gtaoBlurPipeline) { vkDestroyPipeline(device, m_gtaoBlurPipeline, nullptr); m_gtaoBlurPipeline = VK_NULL_HANDLE; }
        if (m_gtaoBlurPipelineLayout) { vkDestroyPipelineLayout(device, m_gtaoBlurPipelineLayout, nullptr); m_gtaoBlurPipelineLayout = VK_NULL_HANDLE; }

        if (m_depthSampler) { vkDestroySampler(device, m_depthSampler, nullptr); m_depthSampler = VK_NULL_HANDLE; }

        if (m_tripleTextureDescLayout) { vkDestroyDescriptorSetLayout(device, m_tripleTextureDescLayout, nullptr); m_tripleTextureDescLayout = VK_NULL_HANDLE; }

        if (m_bloomDownPipeline) { vkDestroyPipeline(device, m_bloomDownPipeline, nullptr); m_bloomDownPipeline = VK_NULL_HANDLE; }
        if (m_bloomDownPipelineLayout) { vkDestroyPipelineLayout(device, m_bloomDownPipelineLayout, nullptr); m_bloomDownPipelineLayout = VK_NULL_HANDLE; }

        if (m_kawasePipeline) { vkDestroyPipeline(device, m_kawasePipeline, nullptr); m_kawasePipeline = VK_NULL_HANDLE; }
        if (m_kawasePipelineLayout) { vkDestroyPipelineLayout(device, m_kawasePipelineLayout, nullptr); m_kawasePipelineLayout = VK_NULL_HANDLE; }

        if (m_compositePipeline) { vkDestroyPipeline(device, m_compositePipeline, nullptr); m_compositePipeline = VK_NULL_HANDLE; }
        if (m_compositePipelineLayout) { vkDestroyPipelineLayout(device, m_compositePipelineLayout, nullptr); m_compositePipelineLayout = VK_NULL_HANDLE; }

        if (m_postProcessDescPool) { vkDestroyDescriptorPool(device, m_postProcessDescPool, nullptr); m_postProcessDescPool = VK_NULL_HANDLE; }
        if (m_singleTextureDescLayout) { vkDestroyDescriptorSetLayout(device, m_singleTextureDescLayout, nullptr); m_singleTextureDescLayout = VK_NULL_HANDLE; }
        if (m_dualTextureDescLayout) { vkDestroyDescriptorSetLayout(device, m_dualTextureDescLayout, nullptr); m_dualTextureDescLayout = VK_NULL_HANDLE; }

        if (m_linearSampler) { vkDestroySampler(device, m_linearSampler, nullptr); m_linearSampler = VK_NULL_HANDLE; }
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
        // PASS 1: DIRECTIONAL SHADOW PASS (Dynamic Sun & 360-degree Camera Following)
        // =========================================================================
        glm::vec3 sunDir = glm::normalize(glm::vec3(0.45f, 0.85f, 0.30f));
        glm::vec3 sunColor = glm::vec3(1.0f, 0.98f, 0.92f);
        float sunIntensity = 2.8f;
        glm::vec3 ambientColor = glm::vec3(0.20f, 0.24f, 0.34f);
        float ambientIntensity = 0.85f;

        if (engine) {
            const auto& env = engine->getEnvironmentSettings();
            sunDir = env.sunDirection;
            sunColor = env.sunColor;
            sunIntensity = env.sunIntensity;
            ambientColor = env.ambientColor;
            ambientIntensity = env.ambientIntensity;
        }

        // Active Directional Light entity in scene overrides environment & drives altitude/height
        glm::vec3 sunPos(0.0f, 18.0f, 0.0f);
        auto dirLightView = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
        bool hasDirLight = false;
        for (auto dlEntity : dirLightView) {
            const auto& lc = dirLightView.get<ecs::LightComponent>(dlEntity);
            if (lc.type == "directional" || lc.type == "sun") {
                hasDirLight = true;
                const auto& lt = dirLightView.get<ecs::TransformComponent>(dlEntity);
                sunPos = lt.position;
                float pitch = glm::radians(lt.rotation.x);
                float yaw = glm::radians(lt.rotation.y);
                glm::vec3 sunForward = glm::normalize(glm::vec3(
                    cos(yaw) * cos(pitch),
                    sin(pitch),
                    sin(yaw) * cos(pitch)
                ));
                sunDir = glm::normalize(-sunForward);
                sunColor = lc.color;
                sunIntensity = lc.intensity;
                break;
            }
        }

        // Auto-ensure default Directional Light entity exists if absent
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

            float pitch = glm::radians(-45.0f);
            float yaw = glm::radians(-40.0f);
            glm::vec3 sunForward = glm::normalize(glm::vec3(
                cos(yaw) * cos(pitch),
                sin(pitch),
                sin(yaw) * cos(pitch)
            ));
            sunDir = glm::normalize(-sunForward);
            sunColor = sl.color;
            sunIntensity = sl.intensity;
        }

        glm::vec3 camPos = engine ? engine->getCameraPosition() : glm::vec3(glm::inverse(viewMatrix)[3]);

        glm::vec3 lightTarget = camPos;
        if (engine) {
            float yaw = glm::radians(engine->getCameraYaw());
            float pitch = glm::radians(engine->getCameraPitch());
            glm::vec3 front = glm::normalize(glm::vec3(
                cos(yaw) * cos(pitch),
                sin(pitch),
                sin(yaw) * cos(pitch)
            ));
            lightTarget = camPos + front * std::min(m_shadowDistance * 0.45f, 25.0f);
        }
        lightTarget.y = 0.0f;

        float sunAltitude = sunPos.y;
        if (sunAltitude <= 0.05f) {
            sunIntensity = 0.0f; // Sun under ground cannot illuminate top of ground
        }

        // =========================================================================
        // Cascaded Shadow Maps (CSM - 4 Splits) with Texel Stabilization
        // =========================================================================
        float fovY = glm::radians(45.0f);
        float aspect = (m_height > 0) ? (static_cast<float>(m_width) / static_cast<float>(m_height)) : 1.777f;

        float cSplits[SHADOW_CASCADE_COUNT] = {
            std::min(m_shadowDistance * 0.12f, 7.0f),
            std::min(m_shadowDistance * 0.30f, 20.0f),
            std::min(m_shadowDistance * 0.65f, 48.0f),
            m_shadowDistance
        };

        glm::vec3 camFront(0.0f, 0.0f, -1.0f);
        if (engine) {
            float yRad = glm::radians(engine->getCameraYaw());
            float pRad = glm::radians(engine->getCameraPitch());
            camFront = glm::normalize(glm::vec3(cos(yRad) * cos(pRad), sin(pRad), sin(yRad) * cos(pRad)));
        }
        glm::vec3 camRight = glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 camUp = glm::normalize(glm::cross(camRight, camFront));

        glm::mat4 cascadeLightSpace[SHADOW_CASCADE_COUNT]{};

        for (uint32_t c = 0; c < SHADOW_CASCADE_COUNT; ++c) {
            float nDist = (c == 0) ? 0.1f : (cSplits[c - 1] * 0.82f);
            float fDist = cSplits[c];

            float hNear = tan(fovY * 0.5f) * nDist; float wNear = hNear * aspect;
            float hFar  = tan(fovY * 0.5f) * fDist; float wFar  = hFar * aspect;

            glm::vec3 nCenter = camPos + camFront * nDist;
            glm::vec3 fCenter = camPos + camFront * fDist;
            glm::vec3 fMid = (nCenter + fCenter) * 0.5f;

            glm::vec3 farTopRight = fCenter + camRight * wFar + camUp * hFar;
            float radius = glm::length(farTopRight - fMid);

            // Texel stabilization to eliminate shadow shimmering on rotation/movement
            float texelsPerUnit = static_cast<float>(m_shadowWidth) / (radius * 2.0f);
            glm::mat4 tempLook = glm::lookAt(fMid + sunDir * (radius + 25.0f), fMid, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::vec3 lightMid = glm::vec3(tempLook * glm::vec4(fMid, 1.0f));
            lightMid.x = std::floor(lightMid.x * texelsPerUnit) / texelsPerUnit;
            lightMid.y = std::floor(lightMid.y * texelsPerUnit) / texelsPerUnit;
            glm::vec3 snappedMid = glm::vec3(glm::inverse(tempLook) * glm::vec4(lightMid, 1.0f));

            float cHeight = std::max(0.5f, sunAltitude);
            float cDist = std::max(radius + 30.0f, cHeight / std::max(0.15f, sunDir.y));
            glm::vec3 cLightPos = snappedMid + sunDir * cDist;

            glm::mat4 lView = glm::lookAt(cLightPos, snappedMid, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 lProj = glm::ortho(-radius, radius, -radius, radius, 1.0f, cDist * 2.5f);
            lProj[1][1] *= -1.0f;
            cascadeLightSpace[c] = lProj * lView;
        }

        glm::mat4 lightSpaceMatrix = cascadeLightSpace[0];

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
            shadowBarrierInit.subresourceRange.layerCount = SHADOW_CASCADE_COUNT;

            VkDependencyInfo shadowDepInfo{};
            shadowDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            shadowDepInfo.imageMemoryBarrierCount = 1;
            shadowDepInfo.pImageMemoryBarriers = &shadowBarrierInit;
            vkCmdPipelineBarrier2(m_commandBuffer, &shadowDepInfo);

            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
            VkViewport shadowVp{0.0f, 0.0f, static_cast<float>(m_shadowWidth), static_cast<float>(m_shadowHeight), 0.0f, 1.0f};
            vkCmdSetViewport(m_commandBuffer, 0, 1, &shadowVp);
            VkRect2D shadowScissor{{0, 0}, {m_shadowWidth, m_shadowHeight}};
            vkCmdSetScissor(m_commandBuffer, 0, 1, &shadowScissor);

            auto shadowView = registry.get().view<ecs::TransformComponent, ecs::MeshComponent>();

            // Render all 4 Cascades into corresponding layers of the shadow array
            for (uint32_t c = 0; c < SHADOW_CASCADE_COUNT; ++c) {
                VkRenderingAttachmentInfo shadowDepthAttachment{};
                shadowDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                shadowDepthAttachment.imageView = m_shadowCascadeImageViews[c];
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

                for (auto entity : shadowView) {
                    const auto& transform = shadowView.get<ecs::TransformComponent>(entity);
                    const auto& meshComp = shadowView.get<ecs::MeshComponent>(entity);

                    std::string tag = "";
                    if (registry.get().all_of<ecs::TagComponent>(entity)) {
                        tag = registry.get().get<ecs::TagComponent>(entity).name;
                    }
                    if (meshComp.meshId == 1 || tag == "GroundPlane" || tag == "Ground") continue;
                    if (registry.get().all_of<ecs::LightComponent>(entity) || (tag.find("Light") != std::string::npos)) continue;
                    if (registry.get().all_of<ecs::RigidBodyComponent>(entity) &&
                        registry.get().get<ecs::RigidBodyComponent>(entity).isSensor) {
                        continue;
                    }

                    // If sun is below ground, no objects cast shadows on the floor
                    if (sunAltitude <= 0.05f) continue;

                    // If object is completely above the sun in world space, it cannot cast a downward shadow on surfaces below the sun
                    if (transform.position.y - transform.scale.y * 0.5f > sunPos.y) continue;

                    glm::mat4 model = glm::mat4(1.0f);
                    model = glm::translate(model, transform.position);
                    model *= glm::mat4_cast(transform.orientation);
                    model = glm::scale(model, transform.scale);

                    struct ShadowPushConstants {
                        glm::mat4 lightSpaceMvp;
                    } shadowPcs;
                    shadowPcs.lightSpaceMvp = cascadeLightSpace[c] * model;

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
            }

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
            shadowToSampleBarrier.subresourceRange.layerCount = SHADOW_CASCADE_COUNT;

            VkDependencyInfo shadowReadDepInfo{};
            shadowReadDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            shadowReadDepInfo.imageMemoryBarrierCount = 1;
            shadowReadDepInfo.pImageMemoryBarriers = &shadowToSampleBarrier;
            vkCmdPipelineBarrier2(m_commandBuffer, &shadowReadDepInfo);
        }

        // =========================================================================
        // PASS 1b: OMNIDIRECTIONAL POINT LIGHT SHADOW CUBEMAP PASS (6 FACES)
        // =========================================================================
        int shadowedPointLightIdx = -1;
        float shadowedPointLightRange = 25.0f;
        {
            auto lightView = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
            int curL = 0;
            glm::vec3 pLightPos(0.0f);
            for (auto lEntity : lightView) {
                if (curL >= 8) break;
                const auto& lc = lightView.get<ecs::LightComponent>(lEntity);
                if (lc.type == "point" && shadowedPointLightIdx < 0 && lc.range > 0.1f && lc.intensity > 0.05f) {
                    const auto& lt = lightView.get<ecs::TransformComponent>(lEntity);
                    pLightPos = lt.position;
                    shadowedPointLightRange = lc.range;
                    shadowedPointLightIdx = curL;
                }
                curL++;
            }

            if (shadowedPointLightIdx >= 0 && m_pointShadowImage && m_shadowPipeline) {
                // 6 cubemap directions: +X, -X, +Y, -Y, +Z, -Z
                const glm::vec3 targets[6] = {
                    glm::vec3( 1.0f,  0.0f,  0.0f),
                    glm::vec3(-1.0f,  0.0f,  0.0f),
                    glm::vec3( 0.0f,  1.0f,  0.0f),
                    glm::vec3( 0.0f, -1.0f,  0.0f),
                    glm::vec3( 0.0f,  0.0f,  1.0f),
                    glm::vec3( 0.0f,  0.0f, -1.0f)
                };
                const glm::vec3 ups[6] = {
                    glm::vec3( 0.0f, -1.0f,  0.0f),
                    glm::vec3( 0.0f, -1.0f,  0.0f),
                    glm::vec3( 0.0f,  0.0f,  1.0f),
                    glm::vec3( 0.0f,  0.0f, -1.0f),
                    glm::vec3( 0.0f, -1.0f,  0.0f),
                    glm::vec3( 0.0f, -1.0f,  0.0f)
                };

                glm::mat4 pProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, shadowedPointLightRange);
                pProj[1][1] *= -1.0f;

                for (uint32_t face = 0; face < 6; ++face) {
                    VkImageMemoryBarrier2 toAtt{};
                    toAtt.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    toAtt.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                    toAtt.srcAccessMask = 0;
                    toAtt.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                    toAtt.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    toAtt.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    toAtt.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    toAtt.image = m_pointShadowImage;
                    toAtt.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, face, 1};

                    VkDependencyInfo depAtt{};
                    depAtt.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    depAtt.imageMemoryBarrierCount = 1;
                    depAtt.pImageMemoryBarriers = &toAtt;
                    vkCmdPipelineBarrier2(m_commandBuffer, &depAtt);

                    VkRenderingAttachmentInfo faceAtt{};
                    faceAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    faceAtt.imageView = m_pointShadowFaceViews[face];
                    faceAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    faceAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    faceAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    faceAtt.clearValue.depthStencil = {1.0f, 0};

                    VkRenderingInfo faceRender{};
                    faceRender.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                    faceRender.renderArea.offset = {0, 0};
                    faceRender.renderArea.extent = {POINT_SHADOW_MAP_SIZE, POINT_SHADOW_MAP_SIZE};
                    faceRender.layerCount = 1;
                    faceRender.pDepthAttachment = &faceAtt;

                    vkCmdBeginRendering(m_commandBuffer, &faceRender);

                    VkViewport vp{0.0f, 0.0f, static_cast<float>(POINT_SHADOW_MAP_SIZE), static_cast<float>(POINT_SHADOW_MAP_SIZE), 0.0f, 1.0f};
                    VkRect2D sc{{0, 0}, {POINT_SHADOW_MAP_SIZE, POINT_SHADOW_MAP_SIZE}};
                    vkCmdSetViewport(m_commandBuffer, 0, 1, &vp);
                    vkCmdSetScissor(m_commandBuffer, 0, 1, &sc);
                    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);

                    glm::mat4 pView = glm::lookAt(pLightPos, pLightPos + targets[face], ups[face]);
                    glm::mat4 pVp = pProj * pView;

                    auto view = registry.get().view<ecs::TransformComponent, ecs::MeshComponent>();
                    for (auto entity : view) {
                        const auto& tc = view.get<ecs::TransformComponent>(entity);
                        const auto& mc = view.get<ecs::MeshComponent>(entity);
                        if (mc.meshId == 1) continue; // Skip ground plane
                        std::string tag = "";
                        if (registry.get().all_of<ecs::TagComponent>(entity)) tag = registry.get().get<ecs::TagComponent>(entity).name;
                        if (tag.find("Light") != std::string::npos || (tag.find("light") != std::string::npos)) continue;

                        glm::mat4 model = glm::translate(glm::mat4(1.0f), tc.position)
                                        * glm::mat4_cast(tc.orientation)
                                        * glm::scale(glm::mat4(1.0f), tc.scale);

                        glm::mat4 mvp = pVp * model;
                        vkCmdPushConstants(m_commandBuffer, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);

                        core::MeshAsset* mesh = assetManager ? assetManager->getMesh(mc.meshId) : nullptr;
                        if (mesh && mesh->vertexBuffer) {
                            VkDeviceSize offset = 0;
                            vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &mesh->vertexBuffer, &offset);
                            vkCmdBindIndexBuffer(m_commandBuffer, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                            vkCmdDrawIndexed(m_commandBuffer, mesh->indexCount, 1, 0, 0, 0);
                        }
                    }
                    vkCmdEndRendering(m_commandBuffer);
                }

                // Transition entire cubemap to SHADER_READ_ONLY_OPTIMAL
                VkImageMemoryBarrier2 toSample{};
                toSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                toSample.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                toSample.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                toSample.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                toSample.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                toSample.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                toSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                toSample.image = m_pointShadowImage;
                toSample.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 6};

                VkDependencyInfo depSample{};
                depSample.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                depSample.imageMemoryBarrierCount = 1;
                depSample.pImageMemoryBarriers = &toSample;
                vkCmdPipelineBarrier2(m_commandBuffer, &depSample);
            }
        }
        // =========================================================================
        // PASS 2: CLEAN UNFILTERED 3D RENDER PASS (HDR RGBA16F Render Target)
        // =========================================================================
        VkImageMemoryBarrier2 colorBarrier{};
        colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        colorBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        colorBarrier.srcAccessMask = 0;
        colorBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        colorBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        colorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorBarrier.image = m_hdrImage;
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
        colorAttachment.imageView = m_hdrImageView;
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
                ubo.sunDirection = glm::vec4(sunDir, sunIntensity);
                ubo.sunPosition = glm::vec4(sunPos, 1.0f);
                ubo.sunColor = glm::vec4(sunColor, 1.0f);
                ubo.ambientColor = glm::vec4(ambientColor, ambientIntensity);
                ubo.fogParams = glm::vec4(0.0f, 1000.0f, 2000.0f, static_cast<float>(m_antiAliasingMode)); // w = Anti-Aliasing Mode
                ubo.fogColor = glm::vec4(0.039f, 0.039f, 0.078f, 1.0f);
                ubo.pointShadowParams = glm::vec4(static_cast<float>(shadowedPointLightIdx), 0.1f, shadowedPointLightRange, 0.005f);

                auto lightView = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
                int lightIdx = 0;
                for (auto lEntity : lightView) {
                    if (lightIdx >= 8) break;
                    const auto& lt = lightView.get<ecs::TransformComponent>(lEntity);
                    const auto& lc = lightView.get<ecs::LightComponent>(lEntity);
                    if (lc.type == "directional" || lc.type == "sun") continue; // Sun handled above

                    float lType = 0.0f; // 0: Point
                    if (lc.type == "spot") lType = 1.0f;
                    else if (lc.type == "area") lType = 2.0f;

                    float pitch = glm::radians(lt.rotation.x);
                    float yaw = glm::radians(lt.rotation.y);
                    glm::vec3 forward = glm::normalize(glm::vec3(
                        cos(yaw) * cos(pitch),
                        sin(pitch),
                        sin(yaw) * cos(pitch)
                    ));

                    ubo.lightsPos[lightIdx] = glm::vec4(lt.position, std::max(1.0f, lc.range));
                    ubo.lightsCol[lightIdx] = glm::vec4(lc.color, std::max(0.0f, lc.intensity));
                    ubo.lightsDir[lightIdx] = glm::vec4(forward, lType);
                    ubo.lightsParams[lightIdx] = glm::vec4(
                        cos(glm::radians(std::clamp(lc.innerConeAngle, 1.0f, 85.0f))),
                        cos(glm::radians(std::clamp(lc.outerConeAngle, lc.innerConeAngle + 1.0f, 89.0f))),
                        std::max(0.1f, lc.areaSizeX),
                        std::max(0.1f, lc.areaSizeY)
                    );
                    lightIdx++;
                }
                ubo.numLights = lightIdx;

                // Populate dynamic scene occluders for Point, Spot, and Area light shadows
                auto occMeshView = registry.get().view<ecs::TransformComponent, ecs::MeshComponent>();
                int occIdx = 0;
                for (auto mEnt : occMeshView) {
                    if (occIdx >= 32) break;
                    const auto& tc = occMeshView.get<ecs::TransformComponent>(mEnt);
                    const auto& mc = occMeshView.get<ecs::MeshComponent>(mEnt);

                    std::string tag = "";
                    if (registry.get().all_of<ecs::TagComponent>(mEnt)) {
                        tag = registry.get().get<ecs::TagComponent>(mEnt).name;
                    }
                    if (mc.meshId == 1 || tag == "GroundPlane" || tag == "Ground") continue;
                    if (registry.get().all_of<ecs::LightComponent>(mEnt) || (tag.find("Light") != std::string::npos)) continue;
                    if (registry.get().all_of<ecs::RigidBodyComponent>(mEnt) &&
                        registry.get().get<ecs::RigidBodyComponent>(mEnt).isSensor) {
                        continue;
                    }

                    glm::mat3 rot = glm::mat3_cast(tc.orientation);
                    glm::mat3 absRot = glm::mat3(glm::abs(rot[0]), glm::abs(rot[1]), glm::abs(rot[2]));
                    glm::vec3 halfExt = absRot * glm::max(tc.scale * 0.5f, glm::vec3(0.05f));
                    float radius = glm::length(halfExt);
                    float shapeType = 0.0f; // 0: Box
                    if (mc.meshId == 3) shapeType = 1.0f; // Sphere
                    else if (mc.meshId == 4 || mc.meshId == 5) shapeType = 2.0f; // Cylinder/Capsule

                    ubo.occluders[occIdx].centerRadius = glm::vec4(tc.position, radius);
                    ubo.occluders[occIdx].halfExtents = glm::vec4(halfExt, shapeType);
                    occIdx++;
                }
                ubo.numOccluders = occIdx;

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

                    // In Blender & Unreal, lights are represented by 3D viewport billboard icons, NEVER mesh balls!
                    if (isLight) continue;

                    glm::vec3 col(0.22f, 0.74f, 0.97f);
                    float emissive = 0.0f;
                    if (tag == "Sun" || tag == "Coin" || tag == "CoinTrigger") {
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

        // =========================================================================
        // PASS 3: HDR POST-PROCESSING (GTAO, Dual Kawase Bloom & ACES Filmic Tone Mapping)
        // =========================================================================
        {
            // 1. Transition m_hdrImage to SHADER_READ_ONLY_OPTIMAL and m_depthImage to DEPTH_STENCIL_READ_ONLY_OPTIMAL
            VkImageMemoryBarrier2 hdrToSampleBarrier{};
            hdrToSampleBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            hdrToSampleBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            hdrToSampleBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            hdrToSampleBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            hdrToSampleBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            hdrToSampleBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            hdrToSampleBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            hdrToSampleBarrier.image = m_hdrImage;
            hdrToSampleBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            VkImageMemoryBarrier2 depthToReadBarrier{};
            depthToReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            depthToReadBarrier.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            depthToReadBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            depthToReadBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            depthToReadBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            depthToReadBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthToReadBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            depthToReadBarrier.image = m_depthImage;
            depthToReadBarrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

            VkImageMemoryBarrier2 postInitBarriers[] = {hdrToSampleBarrier, depthToReadBarrier};
            VkDependencyInfo postInitDep{};
            postInitDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            postInitDep.imageMemoryBarrierCount = 2;
            postInitDep.pImageMemoryBarriers = postInitBarriers;
            vkCmdPipelineBarrier2(m_commandBuffer, &postInitDep);

            VkViewport fullVp{0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f};
            VkRect2D fullSc{{0, 0}, {m_width, m_height}};
            float fovDeg = engine ? engine->getCameraFov() : 45.0f;
            float aspect = static_cast<float>(m_width) / static_cast<float>(std::max(1u, m_height));

            // 1b. Ground Truth Ambient Occlusion (GTAO) Pass
            if (m_gtaoEnabled && m_gtaoPipeline && m_gtaoBlurPipeline) {
                // Render raw GTAO
                VkImageMemoryBarrier2 toAoAtt{};
                toAoAtt.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                toAoAtt.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                toAoAtt.srcAccessMask = 0;
                toAoAtt.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                toAoAtt.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                toAoAtt.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                toAoAtt.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                toAoAtt.image = m_aoImage;
                toAoAtt.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                VkDependencyInfo aoDep{};
                aoDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                aoDep.imageMemoryBarrierCount = 1;
                aoDep.pImageMemoryBarriers = &toAoAtt;
                vkCmdPipelineBarrier2(m_commandBuffer, &aoDep);

                VkRenderingAttachmentInfo aoAtt{};
                aoAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                aoAtt.imageView = m_aoImageView;
                aoAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                aoAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                aoAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                VkRenderingInfo aoRender{};
                aoRender.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                aoRender.renderArea.offset = {0, 0};
                aoRender.renderArea.extent = {m_width, m_height};
                aoRender.layerCount = 1;
                aoRender.colorAttachmentCount = 1;
                aoRender.pColorAttachments = &aoAtt;

                vkCmdBeginRendering(m_commandBuffer, &aoRender);
                vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
                vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gtaoPipeline);
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gtaoPipelineLayout, 0, 1, &m_gtaoDescSet, 0, nullptr);

                struct GtaoPushConsts {
                    glm::vec4 params;
                    glm::vec4 screenParams;
                } gtaoPcs;
                gtaoPcs.params = glm::vec4(m_gtaoRadius, m_gtaoIntensity, glm::radians(fovDeg), aspect);
                gtaoPcs.screenParams = glm::vec4(1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height), 0.1f, 1000.0f);
                vkCmdPushConstants(m_commandBuffer, m_gtaoPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GtaoPushConsts), &gtaoPcs);
                vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
                vkCmdEndRendering(m_commandBuffer);

                // Transition raw AO to shader read
                toAoAtt.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                toAoAtt.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                toAoAtt.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                toAoAtt.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                toAoAtt.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                toAoAtt.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                vkCmdPipelineBarrier2(m_commandBuffer, &aoDep);

                // Bilateral Blur Pass
                VkImageMemoryBarrier2 toAoBlurAtt{};
                toAoBlurAtt.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                toAoBlurAtt.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                toAoBlurAtt.srcAccessMask = 0;
                toAoBlurAtt.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                toAoBlurAtt.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                toAoBlurAtt.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                toAoBlurAtt.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                toAoBlurAtt.image = m_aoBlurImage;
                toAoBlurAtt.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                VkDependencyInfo aoBlurDep{};
                aoBlurDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                aoBlurDep.imageMemoryBarrierCount = 1;
                aoBlurDep.pImageMemoryBarriers = &toAoBlurAtt;
                vkCmdPipelineBarrier2(m_commandBuffer, &aoBlurDep);

                VkRenderingAttachmentInfo aoBlurAtt{};
                aoBlurAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                aoBlurAtt.imageView = m_aoBlurImageView;
                aoBlurAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                aoBlurAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                aoBlurAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                VkRenderingInfo aoBlurRender{};
                aoBlurRender.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                aoBlurRender.renderArea.offset = {0, 0};
                aoBlurRender.renderArea.extent = {m_width, m_height};
                aoBlurRender.layerCount = 1;
                aoBlurRender.colorAttachmentCount = 1;
                aoBlurRender.pColorAttachments = &aoBlurAtt;

                vkCmdBeginRendering(m_commandBuffer, &aoBlurRender);
                vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
                vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gtaoBlurPipeline);
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gtaoBlurPipelineLayout, 0, 1, &m_gtaoBlurDescSet, 0, nullptr);

                glm::vec4 blurPcs(1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height), 0.0f, 0.0f);
                vkCmdPushConstants(m_commandBuffer, m_gtaoBlurPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), &blurPcs);
                vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
                vkCmdEndRendering(m_commandBuffer);

                // Transition blurred AO to shader read
                toAoBlurAtt.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                toAoBlurAtt.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                toAoBlurAtt.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                toAoBlurAtt.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                toAoBlurAtt.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                toAoBlurAtt.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                vkCmdPipelineBarrier2(m_commandBuffer, &aoBlurDep);
            } else {
                // Transition m_aoBlurImage to SHADER_READ_ONLY_OPTIMAL if disabled
                VkImageMemoryBarrier2 dummyAo{};
                dummyAo.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                dummyAo.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                dummyAo.srcAccessMask = 0;
                dummyAo.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                dummyAo.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                dummyAo.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                dummyAo.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                dummyAo.image = m_aoBlurImage;
                dummyAo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                VkDependencyInfo depDummyAo{};
                depDummyAo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                depDummyAo.imageMemoryBarrierCount = 1;
                depDummyAo.pImageMemoryBarriers = &dummyAo;
                vkCmdPipelineBarrier2(m_commandBuffer, &depDummyAo);
            }

            uint32_t bloomW = std::max(1u, (m_width + 1) / 2);
            uint32_t bloomH = std::max(1u, (m_height + 1) / 2);
            VkViewport bloomVp{0.0f, 0.0f, static_cast<float>(bloomW), static_cast<float>(bloomH), 0.0f, 1.0f};
            VkRect2D bloomSc{{0, 0}, {bloomW, bloomH}};

            if (m_bloomEnabled && m_bloomDownPipeline && m_kawasePipeline) {
                // 2a. Bloom Downsample Pass (m_hdrImage -> m_bloomImageA)
                VkImageMemoryBarrier2 toBloomAttA{};
                toBloomAttA.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                toBloomAttA.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                toBloomAttA.srcAccessMask = 0;
                toBloomAttA.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                toBloomAttA.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                toBloomAttA.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                toBloomAttA.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                toBloomAttA.image = m_bloomImageA;
                toBloomAttA.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                VkDependencyInfo bloomDepA{};
                bloomDepA.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                bloomDepA.imageMemoryBarrierCount = 1;
                bloomDepA.pImageMemoryBarriers = &toBloomAttA;
                vkCmdPipelineBarrier2(m_commandBuffer, &bloomDepA);

                VkRenderingAttachmentInfo bloomAttA{};
                bloomAttA.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                bloomAttA.imageView = m_bloomImageViewA;
                bloomAttA.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                bloomAttA.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                bloomAttA.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                VkRenderingInfo bloomDownRender{};
                bloomDownRender.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                bloomDownRender.renderArea.offset = {0, 0};
                bloomDownRender.renderArea.extent = {bloomW, bloomH};
                bloomDownRender.layerCount = 1;
                bloomDownRender.colorAttachmentCount = 1;
                bloomDownRender.pColorAttachments = &bloomAttA;

                vkCmdBeginRendering(m_commandBuffer, &bloomDownRender);
                vkCmdSetViewport(m_commandBuffer, 0, 1, &bloomVp);
                vkCmdSetScissor(m_commandBuffer, 0, 1, &bloomSc);
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomDownPipeline);
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomDownPipelineLayout, 0, 1, &m_bloomDownDescSet, 0, nullptr);

                glm::vec4 downParams(m_bloomThreshold, 0.5f, 1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height));
                vkCmdPushConstants(m_commandBuffer, m_bloomDownPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), &downParams);
                vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
                vkCmdEndRendering(m_commandBuffer);

                // Transition bloom A to shader read
                toBloomAttA.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                toBloomAttA.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                toBloomAttA.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                toBloomAttA.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                toBloomAttA.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                toBloomAttA.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                vkCmdPipelineBarrier2(m_commandBuffer, &bloomDepA);

                // 2b. Dual Kawase Blur Passes (Ping-Pong A <-> B)
                // Pass 1: A -> B (offset 1.5)
                {
                    VkImageMemoryBarrier2 toBloomAttB{};
                    toBloomAttB.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    toBloomAttB.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                    toBloomAttB.srcAccessMask = 0;
                    toBloomAttB.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    toBloomAttB.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    toBloomAttB.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    toBloomAttB.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    toBloomAttB.image = m_bloomImageB;
                    toBloomAttB.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                    VkDependencyInfo depB{};
                    depB.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    depB.imageMemoryBarrierCount = 1;
                    depB.pImageMemoryBarriers = &toBloomAttB;
                    vkCmdPipelineBarrier2(m_commandBuffer, &depB);

                    VkRenderingAttachmentInfo attB{};
                    attB.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    attB.imageView = m_bloomImageViewB;
                    attB.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    attB.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    attB.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                    VkRenderingInfo rB{};
                    rB.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                    rB.renderArea.offset = {0, 0};
                    rB.renderArea.extent = {bloomW, bloomH};
                    rB.layerCount = 1;
                    rB.colorAttachmentCount = 1;
                    rB.pColorAttachments = &attB;

                    vkCmdBeginRendering(m_commandBuffer, &rB);
                    vkCmdSetViewport(m_commandBuffer, 0, 1, &bloomVp);
                    vkCmdSetScissor(m_commandBuffer, 0, 1, &bloomSc);
                    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_kawasePipeline);
                    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_kawasePipelineLayout, 0, 1, &m_kawaseDescSetA, 0, nullptr);

                    glm::vec4 kawaseParams1(1.0f / static_cast<float>(bloomW), 1.0f / static_cast<float>(bloomH), 1.5f, 0.0f);
                    vkCmdPushConstants(m_commandBuffer, m_kawasePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), &kawaseParams1);
                    vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
                    vkCmdEndRendering(m_commandBuffer);

                    toBloomAttB.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    toBloomAttB.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    toBloomAttB.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    toBloomAttB.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    toBloomAttB.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    toBloomAttB.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    vkCmdPipelineBarrier2(m_commandBuffer, &depB);
                }

                // Pass 2: B -> A (offset 2.5)
                {
                    VkImageMemoryBarrier2 toAttA{};
                    toAttA.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    toAttA.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    toAttA.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    toAttA.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    toAttA.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    toAttA.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    toAttA.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    toAttA.image = m_bloomImageA;
                    toAttA.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                    VkDependencyInfo depA{};
                    depA.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    depA.imageMemoryBarrierCount = 1;
                    depA.pImageMemoryBarriers = &toAttA;
                    vkCmdPipelineBarrier2(m_commandBuffer, &depA);

                    VkRenderingAttachmentInfo attA{};
                    attA.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    attA.imageView = m_bloomImageViewA;
                    attA.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    attA.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    attA.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                    VkRenderingInfo rA{};
                    rA.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                    rA.renderArea.offset = {0, 0};
                    rA.renderArea.extent = {bloomW, bloomH};
                    rA.layerCount = 1;
                    rA.colorAttachmentCount = 1;
                    rA.pColorAttachments = &attA;

                    vkCmdBeginRendering(m_commandBuffer, &rA);
                    vkCmdSetViewport(m_commandBuffer, 0, 1, &bloomVp);
                    vkCmdSetScissor(m_commandBuffer, 0, 1, &bloomSc);
                    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_kawasePipeline);
                    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_kawasePipelineLayout, 0, 1, &m_kawaseDescSetB, 0, nullptr);

                    glm::vec4 kawaseParams2(1.0f / static_cast<float>(bloomW), 1.0f / static_cast<float>(bloomH), 2.5f, 0.0f);
                    vkCmdPushConstants(m_commandBuffer, m_kawasePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), &kawaseParams2);
                    vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
                    vkCmdEndRendering(m_commandBuffer);

                    toAttA.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    toAttA.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    toAttA.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    toAttA.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    toAttA.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    toAttA.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    vkCmdPipelineBarrier2(m_commandBuffer, &depA);
                }

                // Pass 3: A -> B (offset 3.5)
                {
                    VkImageMemoryBarrier2 toAttB{};
                    toAttB.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    toAttB.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    toAttB.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    toAttB.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    toAttB.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    toAttB.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    toAttB.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    toAttB.image = m_bloomImageB;
                    toAttB.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                    VkDependencyInfo depB{};
                    depB.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    depB.imageMemoryBarrierCount = 1;
                    depB.pImageMemoryBarriers = &toAttB;
                    vkCmdPipelineBarrier2(m_commandBuffer, &depB);

                    VkRenderingAttachmentInfo attB{};
                    attB.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    attB.imageView = m_bloomImageViewB;
                    attB.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    attB.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    attB.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                    VkRenderingInfo rB{};
                    rB.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                    rB.renderArea.offset = {0, 0};
                    rB.renderArea.extent = {bloomW, bloomH};
                    rB.layerCount = 1;
                    rB.colorAttachmentCount = 1;
                    rB.pColorAttachments = &attB;

                    vkCmdBeginRendering(m_commandBuffer, &rB);
                    vkCmdSetViewport(m_commandBuffer, 0, 1, &bloomVp);
                    vkCmdSetScissor(m_commandBuffer, 0, 1, &bloomSc);
                    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_kawasePipeline);
                    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_kawasePipelineLayout, 0, 1, &m_kawaseDescSetA, 0, nullptr);

                    glm::vec4 kawaseParams3(1.0f / static_cast<float>(bloomW), 1.0f / static_cast<float>(bloomH), 3.5f, 0.0f);
                    vkCmdPushConstants(m_commandBuffer, m_kawasePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), &kawaseParams3);
                    vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
                    vkCmdEndRendering(m_commandBuffer);

                    toAttB.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    toAttB.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    toAttB.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    toAttB.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    toAttB.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    toAttB.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    vkCmdPipelineBarrier2(m_commandBuffer, &depB);
                }
            } else {
                // If bloom disabled, ensure m_bloomImageB is in SHADER_READ_ONLY_OPTIMAL layout
                VkImageMemoryBarrier2 dummyBloomB{};
                dummyBloomB.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                dummyBloomB.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                dummyBloomB.srcAccessMask = 0;
                dummyBloomB.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                dummyBloomB.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                dummyBloomB.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                dummyBloomB.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                dummyBloomB.image = m_bloomImageB;
                dummyBloomB.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                VkDependencyInfo depDummy{};
                depDummy.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                depDummy.imageMemoryBarrierCount = 1;
                depDummy.pImageMemoryBarriers = &dummyBloomB;
                vkCmdPipelineBarrier2(m_commandBuffer, &depDummy);
            }

            // 3. Composite & ACES Tone Mapping Pass -> Writes into m_colorImage
            VkImageMemoryBarrier2 toColorAtt{};
            toColorAtt.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            toColorAtt.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            toColorAtt.srcAccessMask = 0;
            toColorAtt.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            toColorAtt.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            toColorAtt.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toColorAtt.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            toColorAtt.image = m_colorImage;
            toColorAtt.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            VkDependencyInfo colorAttDep{};
            colorAttDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            colorAttDep.imageMemoryBarrierCount = 1;
            colorAttDep.pImageMemoryBarriers = &toColorAtt;
            vkCmdPipelineBarrier2(m_commandBuffer, &colorAttDep);

            VkRenderingAttachmentInfo compAtt{};
            compAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            compAtt.imageView = m_colorImageView;
            compAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            compAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            compAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo compRender{};
            compRender.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            compRender.renderArea.offset = {0, 0};
            compRender.renderArea.extent = {m_width, m_height};
            compRender.layerCount = 1;
            compRender.colorAttachmentCount = 1;
            compRender.pColorAttachments = &compAtt;

            vkCmdBeginRendering(m_commandBuffer, &compRender);

            vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);

            if (m_compositePipeline) {
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePipeline);
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePipelineLayout, 0, 1, &m_compositeDescSet, 0, nullptr);

                struct CompPushConsts {
                    glm::vec4 params;
                    glm::vec4 aoParams;
                } compPcs;
                compPcs.params = glm::vec4(m_bloomIntensity, 1.0f, static_cast<float>(m_antiAliasingMode), m_bloomEnabled ? 1.0f : 0.0f);
                compPcs.aoParams = glm::vec4(m_gtaoEnabled ? 1.0f : 0.0f, 1.0f, 1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height));
                vkCmdPushConstants(m_commandBuffer, m_compositePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CompPushConsts), &compPcs);
                vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            }
            vkCmdEndRendering(m_commandBuffer);
        }

#ifdef USE_GLFW
        if (!m_context->isHeadless()) {
            VkImage swapchainImg = m_context->getSwapchainImages()[imageIndex];

            if (m_standaloneGameMode) {
                // Direct Swapchain Blit in Standalone Mode
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
            } else {
                // Editor Mode: Direct transition without redundant background blit
                VkImageMemoryBarrier2 imgBarrierDst{};
                imgBarrierDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                imgBarrierDst.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                imgBarrierDst.srcAccessMask = 0;
                imgBarrierDst.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                imgBarrierDst.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                imgBarrierDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imgBarrierDst.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                imgBarrierDst.image = swapchainImg;
                imgBarrierDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                VkImageMemoryBarrier2 imgBarrierSrc{};
                imgBarrierSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                imgBarrierSrc.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                imgBarrierSrc.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                imgBarrierSrc.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                imgBarrierSrc.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                imgBarrierSrc.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                imgBarrierSrc.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imgBarrierSrc.image = m_colorImage;
                imgBarrierSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                VkImageMemoryBarrier2 editorBarriers[] = {imgBarrierDst, imgBarrierSrc};
                VkDependencyInfo depInfo{};
                depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                depInfo.imageMemoryBarrierCount = 2;
                depInfo.pImageMemoryBarriers = editorBarriers;
                vkCmdPipelineBarrier2(m_commandBuffer, &depInfo);
            }

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

        style.WindowPadding     = ImVec2(6.0f, 5.0f);
        style.FramePadding      = ImVec2(6.0f, 3.5f);
        style.ItemSpacing       = ImVec2(5.0f, 4.0f);
        style.ItemInnerSpacing  = ImVec2(4.0f, 3.0f);
        style.ScrollbarSize     = 10.0f;
    }

    static void loadUnrealFont(ImGuiIO& io) {
        const std::vector<std::string> fontCandidates = {
            "public/fonts/Roboto-Medium.ttf",
            "engine/assets/fonts/Roboto-Medium.ttf",
            "assets/fonts/Roboto-Medium.ttf",
            "D:/UE_5.8/Engine/Content/Slate/Fonts/Roboto-Medium.ttf",
            "public/fonts/Roboto-Regular.ttf",
            "engine/assets/fonts/Roboto-Regular.ttf",
            "D:/UE_5.8/Engine/Content/Slate/Fonts/Roboto-Regular.ttf",
            "public/fonts/Roboto-Bold.ttf",
            "engine/assets/fonts/Roboto-Bold.ttf",
            "D:/UE_5.8/Engine/Content/Slate/Fonts/Roboto-Bold.ttf",
            "C:/Windows/Fonts/segoeui.ttf",
            "C:/Windows/Fonts/arial.ttf"
        };

        static const ImWchar glyphRanges[] = {
            0x0020, 0x00FF,
            0x0100, 0x017F, // Polish characters
            0x2000, 0x206F, // Punctuation
            0x25A0, 0x25FF, // Geometric shapes
            0,
        };

        ImFont* loadedFont = nullptr;
        for (const auto& path : fontCandidates) {
            if (std::filesystem::exists(path)) {
                ImFontConfig config;
                config.OversampleH = 2;
                config.OversampleV = 2;
                config.RasterizerMultiply = 1.05f;
                loadedFont = io.Fonts->AddFontFromFileTTF(path.c_str(), 16.0f, &config, glyphRanges);
                if (loadedFont) {
                    engine::core::Logger::info("Loaded authentic Unreal Engine 5.8 Slate font: " + path);
                    break;
                }
            }
        }
        if (!loadedFont) {
            io.Fonts->AddFontDefault();
        }
    }

    // Crisp vector icon primitives (Lucide-style) matching App.tsx without missing font glyphs
    enum class VectorIconType {
        Cube,
        Character,
        Pawn,
        Light,
        SkyLight,
        PlayerStart,
        TriggerBox,
        TriggerSphere,
        Star,
        Clock,
        Shapes,
        Sparkles,
        Folder,
        WarningTri,
        Eye,
        Translate,
        Rotate,
        Scale,
        Globe,
        Camera,
        Speaker,
        Wand,
        Pointer,
        Mountain,
        Leaf,
        Brush,
        Gear
    };

    static void drawVectorIcon(ImDrawList* dl, ImVec2 center, float r, VectorIconType type, ImU32 col) {
        switch (type) {
            case VectorIconType::Cube: {
                ImVec2 t0(center.x, center.y - r);
                ImVec2 t1(center.x + r * 0.86f, center.y - r * 0.5f);
                ImVec2 t2(center.x, center.y);
                ImVec2 t3(center.x - r * 0.86f, center.y - r * 0.5f);
                ImVec2 b1(center.x + r * 0.86f, center.y + r * 0.5f);
                ImVec2 b2(center.x, center.y + r);
                ImVec2 b3(center.x - r * 0.86f, center.y + r * 0.5f);
                dl->AddLine(t0, t1, col, 1.4f); dl->AddLine(t1, t2, col, 1.4f);
                dl->AddLine(t2, t3, col, 1.4f); dl->AddLine(t3, t0, col, 1.4f);
                dl->AddLine(t1, b1, col, 1.4f); dl->AddLine(t2, b2, col, 1.4f); dl->AddLine(t3, b3, col, 1.4f);
                dl->AddLine(b3, b2, col, 1.4f); dl->AddLine(b2, b1, col, 1.4f);
                break;
            }
            case VectorIconType::Character: {
                dl->AddCircle(ImVec2(center.x, center.y - r * 0.42f), r * 0.34f, col, 12, 1.5f);
                dl->PathArcTo(ImVec2(center.x, center.y + r * 0.75f), r * 0.65f, 3.45f, 5.97f, 12);
                dl->PathStroke(col, 0, 1.5f);
                break;
            }
            case VectorIconType::Pawn: {
                dl->AddCircle(ImVec2(center.x, center.y - r * 0.48f), r * 0.28f, col, 12, 1.5f);
                dl->AddRect(ImVec2(center.x - r * 0.40f, center.y - r * 0.10f), ImVec2(center.x + r * 0.40f, center.y + r * 0.78f), col, r * 0.35f, 0, 1.5f);
                break;
            }
            case VectorIconType::Light: {
                dl->AddCircle(center, r * 0.40f, col, 12, 1.5f);
                for (int i = 0; i < 8; ++i) {
                    float a = i * 0.785398f;
                    dl->AddLine(
                        ImVec2(center.x + cosf(a) * r * 0.60f, center.y + sinf(a) * r * 0.60f),
                        ImVec2(center.x + cosf(a) * r * 0.95f, center.y + sinf(a) * r * 0.95f),
                        col, 1.3f
                    );
                }
                break;
            }
            case VectorIconType::SkyLight: {
                dl->AddLine(ImVec2(center.x - r * 0.8f, center.y), ImVec2(center.x + r * 0.8f, center.y), col, 1.4f);
                dl->AddLine(ImVec2(center.x, center.y - r * 0.8f), ImVec2(center.x, center.y + r * 0.8f), col, 1.4f);
                dl->AddLine(ImVec2(center.x - r * 0.55f, center.y - r * 0.55f), ImVec2(center.x + r * 0.55f, center.y + r * 0.55f), col, 1.3f);
                dl->AddLine(ImVec2(center.x + r * 0.55f, center.y - r * 0.55f), ImVec2(center.x - r * 0.55f, center.y + r * 0.55f), col, 1.3f);
                break;
            }
            case VectorIconType::PlayerStart: {
                dl->AddLine(ImVec2(center.x - r * 0.55f, center.y - r * 0.85f), ImVec2(center.x - r * 0.55f, center.y + r * 0.85f), col, 1.6f);
                dl->AddTriangleFilled(
                    ImVec2(center.x - r * 0.50f, center.y - r * 0.80f),
                    ImVec2(center.x + r * 0.75f, center.y - r * 0.30f),
                    ImVec2(center.x - r * 0.50f, center.y + r * 0.20f),
                    col
                );
                break;
            }
            case VectorIconType::TriggerBox: {
                dl->AddRect(ImVec2(center.x - r * 0.78f, center.y - r * 0.68f), ImVec2(center.x + r * 0.78f, center.y + r * 0.68f), col, 2.0f, 0, 1.5f);
                dl->AddCircleFilled(center, r * 0.22f, col);
                break;
            }
            case VectorIconType::TriggerSphere: {
                dl->AddCircle(center, r * 0.80f, col, 16, 1.5f);
                dl->AddCircle(center, r * 0.32f, col, 12, 1.2f);
                break;
            }
            case VectorIconType::Star: {
                ImVec2 pts[5];
                for (int i = 0; i < 5; ++i) {
                    float a = -1.570796f + i * 1.256637f;
                    pts[i] = ImVec2(center.x + cosf(a) * r * 0.85f, center.y + sinf(a) * r * 0.85f);
                }
                dl->AddLine(pts[0], pts[2], col, 1.4f); dl->AddLine(pts[2], pts[4], col, 1.4f);
                dl->AddLine(pts[4], pts[1], col, 1.4f); dl->AddLine(pts[1], pts[3], col, 1.4f);
                dl->AddLine(pts[3], pts[0], col, 1.4f);
                break;
            }
            case VectorIconType::Clock: {
                dl->AddCircle(center, r * 0.82f, col, 16, 1.5f);
                dl->AddLine(center, ImVec2(center.x, center.y - r * 0.50f), col, 1.5f);
                dl->AddLine(center, ImVec2(center.x + r * 0.42f, center.y + r * 0.20f), col, 1.5f);
                break;
            }
            case VectorIconType::Shapes: {
                dl->AddTriangle(ImVec2(center.x, center.y - r * 0.85f), ImVec2(center.x + r * 0.75f, center.y + r * 0.45f), ImVec2(center.x - r * 0.75f, center.y + r * 0.45f), col, 1.5f);
                break;
            }
            case VectorIconType::Sparkles: {
                dl->AddLine(ImVec2(center.x, center.y - r * 0.90f), ImVec2(center.x, center.y + r * 0.90f), col, 1.5f);
                dl->AddLine(ImVec2(center.x - r * 0.90f, center.y), ImVec2(center.x + r * 0.90f, center.y), col, 1.5f);
                dl->AddCircleFilled(center, r * 0.25f, col);
                break;
            }
            case VectorIconType::Folder: {
                dl->AddRectFilled(ImVec2(center.x - r * 0.85f, center.y - r * 0.65f), ImVec2(center.x - r * 0.10f, center.y - r * 0.35f), col, 1.5f);
                dl->AddRectFilled(ImVec2(center.x - r * 0.85f, center.y - r * 0.42f), ImVec2(center.x + r * 0.85f, center.y + r * 0.65f), col, 2.0f);
                break;
            }
            case VectorIconType::WarningTri: {
                dl->AddTriangleFilled(ImVec2(center.x, center.y - r * 0.82f), ImVec2(center.x + r * 0.85f, center.y + r * 0.72f), ImVec2(center.x - r * 0.85f, center.y + r * 0.72f), col);
                break;
            }
            case VectorIconType::Eye: {
                dl->AddCircle(center, r * 0.62f, col, 12, 1.3f);
                dl->AddCircleFilled(center, r * 0.25f, col);
                break;
            }
            case VectorIconType::Translate: {
                dl->AddLine(ImVec2(center.x - r * 0.85f, center.y), ImVec2(center.x + r * 0.85f, center.y), col, 1.5f);
                dl->AddLine(ImVec2(center.x, center.y - r * 0.85f), ImVec2(center.x, center.y + r * 0.85f), col, 1.5f);
                break;
            }
            case VectorIconType::Rotate: {
                dl->PathArcTo(center, r * 0.72f, 0.4f, 5.3f, 14);
                dl->PathStroke(col, 0, 1.6f);
                break;
            }
            case VectorIconType::Scale: {
                dl->AddRect(ImVec2(center.x - r * 0.75f, center.y - r * 0.75f), ImVec2(center.x + r * 0.75f, center.y + r * 0.75f), col, 1.5f, 0, 1.4f);
                dl->AddLine(ImVec2(center.x - r * 0.45f, center.y + r * 0.45f), ImVec2(center.x + r * 0.45f, center.y - r * 0.45f), col, 1.5f);
                break;
            }
            case VectorIconType::Globe: {
                dl->AddCircle(center, r * 0.80f, col, 16, 1.4f);
                dl->AddLine(ImVec2(center.x - r * 0.80f, center.y), ImVec2(center.x + r * 0.80f, center.y), col, 1.2f);
                dl->AddLine(ImVec2(center.x, center.y - r * 0.80f), ImVec2(center.x, center.y + r * 0.80f), col, 1.2f);
                break;
            }
            case VectorIconType::Camera: {
                dl->AddRectFilled(ImVec2(center.x - r * 0.82f, center.y - r * 0.50f), ImVec2(center.x + r * 0.38f, center.y + r * 0.50f), col, 2.0f);
                dl->AddTriangleFilled(ImVec2(center.x + r * 0.35f, center.y), ImVec2(center.x + r * 0.88f, center.y - r * 0.45f), ImVec2(center.x + r * 0.88f, center.y + r * 0.45f), col);
                break;
            }
            case VectorIconType::Speaker: {
                dl->AddRectFilled(ImVec2(center.x - r * 0.75f, center.y - r * 0.32f), ImVec2(center.x - r * 0.30f, center.y + r * 0.32f), col, 1.0f);
                dl->AddTriangleFilled(ImVec2(center.x - r * 0.35f, center.y), ImVec2(center.x + r * 0.25f, center.y - r * 0.68f), ImVec2(center.x + r * 0.25f, center.y + r * 0.68f), col);
                dl->PathArcTo(ImVec2(center.x + r * 0.25f, center.y), r * 0.48f, -0.8f, 0.8f, 8);
                dl->PathStroke(col, 0, 1.4f);
                break;
            }
            case VectorIconType::Wand: {
                dl->AddLine(ImVec2(center.x - r * 0.75f, center.y + r * 0.75f), ImVec2(center.x + r * 0.55f, center.y - r * 0.55f), col, 1.8f);
                dl->AddCircleFilled(ImVec2(center.x + r * 0.65f, center.y - r * 0.65f), r * 0.22f, col);
                break;
            }
            case VectorIconType::Pointer: {
                ImVec2 p0(center.x - r * 0.6f, center.y - r * 0.8f);
                ImVec2 p1(center.x - r * 0.6f, center.y + r * 0.6f);
                ImVec2 p2(center.x - r * 0.1f, center.y + r * 0.2f);
                ImVec2 p3(center.x + r * 0.5f, center.y + r * 0.7f);
                dl->AddTriangleFilled(p0, p1, p2, col);
                dl->AddLine(p2, p3, col, 1.6f);
                break;
            }
            case VectorIconType::Mountain: {
                ImVec2 p0(center.x - r * 0.8f, center.y + r * 0.6f);
                ImVec2 p1(center.x - r * 0.2f, center.y - r * 0.5f);
                ImVec2 p2(center.x + r * 0.4f, center.y + r * 0.6f);
                dl->AddTriangle(p0, p1, p2, col, 1.4f);
                ImVec2 q0(center.x + r * 0.1f, center.y + r * 0.6f);
                ImVec2 q1(center.x + r * 0.5f, center.y - r * 0.2f);
                ImVec2 q2(center.x + r * 0.9f, center.y + r * 0.6f);
                dl->AddTriangle(q0, q1, q2, col, 1.4f);
                break;
            }
            case VectorIconType::Leaf: {
                dl->PathArcTo(ImVec2(center.x - r * 0.2f, center.y), r * 0.6f, -1.57f, 1.57f, 8);
                dl->PathStroke(col, 0, 1.4f);
                dl->PathArcTo(ImVec2(center.x + r * 0.2f, center.y), r * 0.6f, 1.57f, 4.71f, 8);
                dl->PathStroke(col, 0, 1.4f);
                dl->AddLine(ImVec2(center.x, center.y - r * 0.6f), ImVec2(center.x, center.y + r * 0.7f), col, 1.2f);
                break;
            }
            case VectorIconType::Brush: {
                dl->AddLine(ImVec2(center.x + r * 0.6f, center.y - r * 0.6f), ImVec2(center.x - r * 0.1f, center.y + r * 0.1f), col, 2.0f);
                dl->AddTriangleFilled(ImVec2(center.x - r * 0.4f, center.y + r * 0.2f), ImVec2(center.x - r * 0.1f, center.y + r * 0.5f), ImVec2(center.x - r * 0.6f, center.y + r * 0.7f), col);
                break;
            }
            case VectorIconType::Gear: {
                dl->AddCircle(center, r * 0.45f, col, 12, 1.5f);
                for (int i = 0; i < 6; ++i) {
                    float a = i * 1.047197f;
                    dl->AddLine(
                        ImVec2(center.x + cosf(a) * r * 0.45f, center.y + sinf(a) * r * 0.45f),
                        ImVec2(center.x + cosf(a) * r * 0.85f, center.y + sinf(a) * r * 0.85f),
                        col, 2.0f
                    );
                }
                break;
            }
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
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.92f, 1.0f), "%s", label);
        ImGui::NextColumn();

        float lineHeight = ImGui::GetFrameHeight();
        ImVec2 buttonSize = { lineHeight + 2.0f, lineHeight };
        float availW = ImGui::GetContentRegionAvail().x;
        float itemWidth = (availW - 3.0f * buttonSize.x - 24.0f) / 3.0f;
        if (itemWidth < 35.0f) itemWidth = 35.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.18f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.28f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.12f, 0.14f, 1.0f));
        if (ImGui::Button("X", buttonSize)) { values.x = resetValue; changed = true; }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(itemWidth);
        if (ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f")) changed = true;
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.75f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.90f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.60f, 0.18f, 1.0f));
        if (ImGui::Button("Y", buttonSize)) { values.y = resetValue; changed = true; }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(itemWidth);
        if (ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f")) changed = true;
        ImGui::SameLine();

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
        (void)assetManager;
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* mainVp = ImGui::GetMainViewport();
        float screenW = mainVp->Size.x;
        float screenH = mainVp->Size.y;

        float dpiScale = 1.0f;
#ifdef USE_GLFW
        if (m_context && m_context->getWindow()) {
            float xsc = 1.0f, ysc = 1.0f;
            glfwGetWindowContentScale(m_context->getWindow(), &xsc, &ysc);
            dpiScale = std::max(1.0f, std::max(xsc, ysc));
        }
#endif
        // Authentic Unreal Engine 5.8 Slate Scale:
        // Panels stay tight and compact, giving 72%+ of the screen to the 3D viewport.
        float s_uiScale = m_uiScale * (dpiScale > 1.0f ? (1.0f + (dpiScale - 1.0f) * 0.35f) : 1.0f);
        s_uiScale = std::clamp(s_uiScale, 0.65f, 2.00f);
        ImGui::GetIO().FontGlobalScale = s_uiScale;

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
            // ── Layout metrics matching App.tsx (Image 4 / UE5 proportions) ──────
            static bool s_immersivePlay = false;
            static bool s_showPlaceActors = true;
            static bool s_showOutliner = true;
            static bool s_showDetails = true;
            static bool s_showContentDrawer = false;
            static bool s_showOutputLog = false;
            static bool s_showScriptStudio = false;
            static bool s_showEffectsPanel = false;

            if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
                s_immersivePlay = !s_immersivePlay;
            }

            float topTitleBarH = s_immersivePlay ? 0.0f : (28.0f * s_uiScale);
            float topLevelTabsH = s_immersivePlay ? 0.0f : (26.0f * s_uiScale);
            float topToolbarH = 36.0f * s_uiScale;
            float topTotalH = topTitleBarH + topLevelTabsH + topToolbarH;

            float bottomStatusH = s_immersivePlay ? 0.0f : (24.0f * s_uiScale);
            float drawerH = (!s_immersivePlay && (s_showContentDrawer || s_showOutputLog)) ? std::min(220.0f * s_uiScale, screenH * 0.35f) : 0.0f;
            float bottomTotalH = bottomStatusH + drawerH;

            float contentH = std::max(180.0f, screenH - topTotalH - bottomTotalH);
            float leftPanelW = (!s_immersivePlay && s_showPlaceActors) ? std::min(260.0f * s_uiScale, screenW * 0.25f) : 0.0f;
            float rightPanelW = (!s_immersivePlay && (s_showOutliner || s_showDetails)) ? std::min(280.0f * s_uiScale, screenW * 0.26f) : 0.0f;
            float centerViewportW = std::max(200.0f, screenW - leftPanelW - rightPanelW);

            static entt::entity selectedEnt = entt::null;
            static NativeGizmoMode s_gizmoMode = NativeGizmoMode::TRANSLATE;
            static char s_engineNotification[128] = "";
            static float s_engineNotificationTimer = 0.0f;

            float camYaw = engine ? engine->getCameraYaw() : -90.0f;
            float camPitch = engine ? engine->getCameraPitch() : -20.0f;
            float camFov = engine ? engine->getCameraFov() : 45.0f;
            glm::vec3 camPos = engine ? engine->getCameraPosition() : glm::vec3(0.0f, 15.0f, 30.0f);

            glm::vec3 front = glm::normalize(glm::vec3(
                cos(glm::radians(camYaw)) * cos(glm::radians(camPitch)),
                sin(glm::radians(camPitch)),
                sin(glm::radians(camYaw)) * cos(glm::radians(camPitch))
            ));
            glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
            glm::vec3 up = glm::normalize(glm::cross(right, front));

            auto saveCurrentGame = [&]() {
                if (engine) {
                    bool ok = engine->saveCurrentProject("projects/MyGame");
                    if (ok) {
                        snprintf(s_engineNotification, sizeof(s_engineNotification), "Projekt zapisany w projects/MyGame oraz game.json!");
                        s_engineNotificationTimer = 4.0f;
                    } else {
                        snprintf(s_engineNotification, sizeof(s_engineNotification), "Blad zapisu projektu!");
                        s_engineNotificationTimer = 4.0f;
                    }
                }
            };

            auto loadCurrentGame = [&](const std::string& p) {
                if (engine) {
                    bool ok = engine->loadCurrentProject(p);
                    if (ok) {
                        selectedEnt = entt::null;
                        snprintf(s_engineNotification, sizeof(s_engineNotification), "Wczytano projekt: %s", p.c_str());
                        s_engineNotificationTimer = 4.0f;
                    } else {
                        snprintf(s_engineNotification, sizeof(s_engineNotification), "Nie udalo sie wczytac projektu: %s", p.c_str());
                        s_engineNotificationTimer = 4.0f;
                    }
                }
            };

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
            static bool s_gizmoSpaceLocal = false;
            static bool s_snapEnabled = false;
            static float s_snapStep = 0.5f;
            static int s_cameraSpeed = 4;
            static int s_placeActorCategory = 2; // 0:Fav, 1:Rec, 2:Basic, 3:Lights, 4:Shapes, 5:VFX, 6:Props
            static int s_detailsTab = 0;

            if (selectedEnt != entt::null && !registry.get().valid(selectedEnt)) {
                selectedEnt = entt::null;
                s_draggingGizmoAxis = 0;
                if (engine) engine->setSelectedEntity(entt::null);
            }
            if (engine && engine->getSelectedEntity() != entt::null && selectedEnt == entt::null) {
                selectedEnt = engine->getSelectedEntity();
            }

            // ══════════════════════════════════════════════════════════════════════
            // 1. TOP WINDOW BAR (Matches UnrealTitleBar Row 1 in App.tsx)
            // ══════════════════════════════════════════════════════════════════════
            if (!s_immersivePlay) {
                ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(screenW, topTitleBarH), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 3));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.035f, 0.055f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.035f, 0.035f, 0.055f, 1.0f));
                ImGuiWindowFlags titleFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
                ImGui::Begin("TopTitleBar", nullptr, titleFlags);

                if (ImGui::BeginMenuBar()) {
                    // Circular (L) badge + LUCID brand matching Image B
                    ImDrawList* tdl = ImGui::GetWindowDrawList();
                    ImVec2 badgePos = ImGui::GetCursorScreenPos();
                    float badgeR = 8.0f * s_uiScale;
                    ImVec2 badgeCenter(badgePos.x + badgeR + 2.0f, badgePos.y + topTitleBarH * 0.42f);
                    tdl->AddCircleFilled(badgeCenter, badgeR, IM_COL32(0, 140, 235, 255), 16);
                    tdl->AddText(ImVec2(badgeCenter.x - 3.5f * s_uiScale, badgeCenter.y - 6.5f * s_uiScale), IM_COL32(255, 255, 255, 255), "L");
                    ImGui::Dummy(ImVec2(badgeR * 2.0f + 4.0f, badgeR * 2.0f));
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "LUCID");
                    ImGui::SameLine(0.0f, 14.0f * s_uiScale);

                    if (ImGui::BeginMenu("File")) {
                        if (ImGui::MenuItem("New Level (Nowy poziom)", "Ctrl+N")) {
                            if (engine) { selectedEnt = entt::null; engine->setCurrentPreset("empty"); engine->resetScene("empty"); }
                        }
                        if (ImGui::MenuItem("Save Project (Zapisz)", "Ctrl+S")) {
                            saveCurrentGame();
                        }
                        if (ImGui::BeginMenu("Open Project (Otworz)")) {
                            if (ImGui::MenuItem("Wczytaj 'projects/MyGame'")) { loadCurrentGame("projects/MyGame"); }
                            if (ImGui::MenuItem("Wczytaj 'game.json'")) { loadCurrentGame("game.json"); }
                            ImGui::EndMenu();
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Kompiluj pelna gre (.exe)", "Ctrl+Shift+P")) {
                            saveCurrentGame();
                            if (engine) {
                                bool ok = engine->packageStandaloneGame("Build/Windows/MyLucidGame", "MyLucidGame");
                                if (ok) {
                                    snprintf(s_engineNotification, sizeof(s_engineNotification), "Gra pomyslnie skompilowana do Build/Windows/MyLucidGame/!");
                                    s_engineNotificationTimer = 5.0f;
                                }
                            }
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Edit")) {
                        ImGui::MenuItem("Undo", "Ctrl+Z");
                        ImGui::MenuItem("Redo", "Ctrl+Y");
                        ImGui::Separator();
                        if (ImGui::MenuItem("World Settings")) { s_detailsTab = 1; s_showDetails = true; }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Window")) {
                        ImGui::MenuItem("Place Actors", nullptr, &s_showPlaceActors);
                        ImGui::MenuItem("Outliner", nullptr, &s_showOutliner);
                        ImGui::MenuItem("Details", nullptr, &s_showDetails);
                        ImGui::MenuItem("Content Drawer", "Ctrl+Space", &s_showContentDrawer);
                        ImGui::MenuItem("Output Log", nullptr, &s_showOutputLog);
                        ImGui::MenuItem("Blueprints / Scripts", nullptr, &s_showScriptStudio);
                        ImGui::Separator();
                        if (ImGui::BeginMenu("UI Scale / Zoom")) {
                            if (ImGui::MenuItem("75% (Ultra Compact)", nullptr, m_uiScale == 0.75f)) m_uiScale = 0.75f;
                            if (ImGui::MenuItem("90% (Compact)", nullptr, m_uiScale == 0.90f)) m_uiScale = 0.90f;
                            if (ImGui::MenuItem("100% (Default UE5)", nullptr, m_uiScale == 1.0f)) m_uiScale = 1.0f;
                            if (ImGui::MenuItem("115% (Comfort)", nullptr, m_uiScale == 1.15f)) m_uiScale = 1.15f;
                            if (ImGui::MenuItem("125% (Large)", nullptr, m_uiScale == 1.25f)) m_uiScale = 1.25f;
                            ImGui::EndMenu();
                        }
                        if (ImGui::BeginMenu("Anti-Aliasing (AA)")) {
                            if (ImGui::MenuItem("Off (Brak AA)", nullptr, m_antiAliasingMode == 0)) m_antiAliasingMode = 0;
                            if (ImGui::MenuItem("FXAA Fast (Wydajny)", nullptr, m_antiAliasingMode == 1)) m_antiAliasingMode = 1;
                            if (ImGui::MenuItem("FXAA Quality (Subpixel)", nullptr, m_antiAliasingMode == 2)) m_antiAliasingMode = 2;
                            if (ImGui::MenuItem("TAA / Temporal AA", nullptr, m_antiAliasingMode == 3)) m_antiAliasingMode = 3;
                            ImGui::EndMenu();
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Tools")) {
                        if (ImGui::MenuItem("Execute QuickJS Script")) { s_showScriptStudio = true; }
                        if (ImGui::MenuItem("Jolt Physics Diagnostics")) { core::Logger::info("Jolt Physics: 60 Hz active."); }
                        if (ImGui::MenuItem("Vulkan 1.3 Profiler")) { core::Logger::info("Vulkan 1.3 Direct Swapchain active."); }
                        if (ImGui::MenuItem("Export TypeScript (.d.ts)")) { if (engine) engine->exportTypeDefinitions(); }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Build")) {
                        ImGui::MenuItem("Build All Levels");
                        ImGui::MenuItem("Build Lighting");
                        ImGui::MenuItem("Build Collision Meshes");
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Platforms")) {
                        ImGui::MenuItem("Windows (Vulkan 1.3 Native)", nullptr, true);
                        if (ImGui::MenuItem("Package Windows (.exe)")) {
                            if (engine) engine->packageStandaloneGame("Build/Windows/MyLucidGame", "MyLucidGame");
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Select")) {
                        if (ImGui::MenuItem("Deselect", "Esc")) {
                            selectedEnt = entt::null;
                            if (engine) engine->setSelectedEntity(entt::null);
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Actor")) {
                        if (ImGui::MenuItem("Delete Selected", "Del") && selectedEnt != entt::null && registry.get().valid(selectedEnt)) {
                            registry.get().destroy(selectedEnt);
                            selectedEnt = entt::null;
                            if (engine) engine->setSelectedEntity(entt::null);
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Help")) {
                        ImGui::MenuItem("Lucid Engine 2026 Documentation");
                        ImGui::EndMenu();
                    }

                    // Center-Right Project Badge matching Image B
                    float projOffset = screenW * 0.50f;
                    if (ImGui::GetCursorPosX() < projOffset) ImGui::SameLine(projOffset);
                    ImGui::TextDisabled("Project:");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "Platformer 3D (Coin Runner)");
                    ImGui::SameLine();
                    ImGui::TextDisabled("|  Lucid Studio 2026");

                    // Right Window Controls
                    ImGui::SameLine(screenW - 125.0f * s_uiScale);
                    ImGui::TextDisabled("Prototype");
                    ImGui::SameLine(0.0f, 10.0f * s_uiScale);
                    ImGui::TextDisabled("-   []   x");

                    ImGui::EndMenuBar();
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar();

                // ══════════════════════════════════════════════════════════════════
                // 2. LEVEL TABS STRIP (Matches CzystyPoziom.scene tab in Image B)
                // ══════════════════════════════════════════════════════════════════
                ImGui::SetNextWindowPos(ImVec2(0, topTitleBarH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(screenW, topLevelTabsH), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 2));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.043f, 0.067f, 1.0f));
                ImGui::Begin("LevelTabsStrip", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);

                std::string curPreset = engine ? engine->getCurrentPreset() : "empty";
                auto drawSceneTab = [&](const char* title, const char* presetId, bool persistent) {
                    bool active = (curPreset == presetId);
                    ImVec2 tabStart = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.086f, 0.086f, 0.125f, 1.0f) : ImVec4(0.043f, 0.043f, 0.067f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, active ? ImVec4(0.96f, 0.97f, 1.0f, 1.0f) : ImVec4(0.55f, 0.58f, 0.66f, 1.0f));
                    std::string label = std::string("    ") + title + (persistent ? "  (Persistent)   x " : "   x ");
                    if (ImGui::Button(label.c_str(), ImVec2(0, topLevelTabsH - 4.0f))) {
                        if (engine) {
                            selectedEnt = entt::null;
                            s_draggingGizmoAxis = 0;
                            engine->setCurrentPreset(presetId);
                            engine->resetScene(presetId);
                        }
                    }
                    ImGui::PopStyleColor(2);
                    ImVec2 tabEnd = ImGui::GetItemRectMax();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    if (active) {
                        dl->AddLine(ImVec2(tabStart.x, tabStart.y), ImVec2(tabEnd.x, tabStart.y), IM_COL32(0, 130, 240, 255), 2.0f);
                    }
                    drawVectorIcon(dl, ImVec2(tabStart.x + 11.0f * s_uiScale, (tabStart.y + tabEnd.y) * 0.5f), 5.0f * s_uiScale, VectorIconType::Folder, active ? IM_COL32(245, 158, 11, 255) : IM_COL32(130, 140, 160, 200));
                    ImGui::SameLine();
                };

                drawSceneTab("CzystyPoziom.scene", curPreset.c_str(), true);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.055f, 0.055f, 0.085f, 1.0f));
                if (ImGui::Button(" + ##AddLevelTab", ImVec2(24.0f * s_uiScale, topLevelTabsH - 4.0f))) {
                    if (engine) {
                        selectedEnt = entt::null;
                        engine->setCurrentPreset("empty");
                        engine->resetScene("empty");
                    }
                }
                ImGui::PopStyleColor();

                ImGui::End();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }

            // ══════════════════════════════════════════════════════════════════════
            // 3. MAIN TOOLBAR (Matches UnrealMainToolbar in App.tsx)
            // ══════════════════════════════════════════════════════════════════════
            ImGui::SetNextWindowPos(ImVec2(0, topTitleBarH + topLevelTabsH), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(screenW, topToolbarH), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 5));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.055f, 0.082f, 1.0f));
            ImGui::Begin("MainToolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);

            float btnH = topToolbarH - 10.0f;

            // Select Mode Pill with cyan indicator
            {
                ImVec2 p = ImGui::GetCursorScreenPos();
                if (ImGui::Button("    Select Mode v ", ImVec2(0, btnH))) { ImGui::OpenPopup("SelectModePopup"); }
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 10.0f * s_uiScale, p.y + btnH * 0.5f), 3.5f * s_uiScale, IM_COL32(0, 210, 255, 255));
                if (ImGui::BeginPopup("SelectModePopup")) {
                    ImGui::Selectable("Select Mode", true);
                    ImGui::Selectable("Landscape Mode");
                    ImGui::Selectable("Modeling Mode");
                    ImGui::EndPopup();
                }
            }
            ImGui::SameLine();

            // Compact Save button
            if (ImGui::Button(" Save ", ImVec2(0, btnH))) {
                saveCurrentGame();
            }
            ImGui::SameLine();

            // + Add button
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.22f, 0.85f, 1.0f, 1.0f));
            if (ImGui::Button(" + Add ", ImVec2(0, btnH))) { ImGui::OpenPopup("QuickAddPopup"); }
            ImGui::PopStyleColor();
            if (ImGui::BeginPopup("QuickAddPopup")) {
                if (ImGui::Selectable("Cube (StaticMeshActor)")) {
                    auto ent = registry.createEntity();
                    registry.get().emplace<ecs::TagComponent>(ent, ecs::TagComponent{"DynamicCube"});
                    registry.get().emplace<ecs::TransformComponent>(ent, glm::vec3(0.0f, 3.0f, 0.0f));
                    registry.get().emplace<ecs::MeshComponent>(ent, ecs::MeshComponent{2});
                    ecs::RigidBodyComponent rb; rb.mass = 1.0f; rb.shapeType = 0;
                    registry.get().emplace<ecs::RigidBodyComponent>(ent, rb);
                    selectedEnt = ent;
                    if (engine) engine->setSelectedEntity(selectedEnt);
                }
                if (ImGui::Selectable("Sphere (RigidBody)")) {
                    auto ent = registry.createEntity();
                    registry.get().emplace<ecs::TagComponent>(ent, ecs::TagComponent{"DynamicSphere"});
                    registry.get().emplace<ecs::TransformComponent>(ent, glm::vec3(0.0f, 5.0f, 0.0f));
                    registry.get().emplace<ecs::MeshComponent>(ent, ecs::MeshComponent{3});
                    ecs::RigidBodyComponent rb; rb.mass = 1.0f; rb.shapeType = 1;
                    registry.get().emplace<ecs::RigidBodyComponent>(ent, rb);
                    selectedEnt = ent;
                    if (engine) engine->setSelectedEntity(selectedEnt);
                }
                if (ImGui::Selectable("Character (WASD Player)")) {
                    auto ent = registry.createEntity();
                    registry.get().emplace<ecs::TagComponent>(ent, ecs::TagComponent{"Player"});
                    registry.get().emplace<ecs::TransformComponent>(ent, glm::vec3(0.0f, 3.0f, 0.0f));
                    registry.get().emplace<ecs::MeshComponent>(ent, ecs::MeshComponent{2});
                    ecs::RigidBodyComponent rb; rb.mass = 1.0f; rb.shapeType = 2; rb.capsuleRadius = 0.6f; rb.capsuleHalfHeight = 0.8f;
                    rb.lockRotationX = true; rb.lockRotationZ = true;
                    registry.get().emplace<ecs::RigidBodyComponent>(ent, rb);
                    selectedEnt = ent;
                    if (engine) engine->setSelectedEntity(selectedEnt);
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();

            // Studio Tool Buttons matching App.tsx colors
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.72f, 1.0f, 1.0f));
            if (ImGui::Button(" </> Blueprints / Scripts ", ImVec2(0, btnH))) { s_showScriptStudio = !s_showScriptStudio; }
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.32f, 1.0f));
            if (ImGui::Button(" Cinematics & VFX ", ImVec2(0, btnH))) { s_showEffectsPanel = !s_showEffectsPanel; }
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.58f, 0.85f, 1.0f));
            if (ImGui::Button(" UI Designer (UMG) ", ImVec2(0, btnH))) {}
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.82f, 0.35f, 1.0f));
            if (ImGui::Button(" Sound Cue Studio ", ImVec2(0, btnH))) {}
            ImGui::PopStyleColor();
            ImGui::SameLine();

            // Emerald green "Kompiluj grę (.exe)" pill
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.04f, 0.28f, 0.18f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.06f, 0.38f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.12f, 0.78f, 0.48f, 0.65f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.98f, 0.68f, 1.0f));
            if (ImGui::Button(" Kompiluj gre (.exe) ", ImVec2(0, btnH))) {
                if (engine) engine->packageStandaloneGame("Build/Windows/MyLucidGame", "MyLucidGame");
            }
            ImGui::PopStyleColor(4);

            // Center Transport Controls (Play | Pause | Stop | Step) matching Image B
            float transportPos = screenW * 0.585f;
            if (ImGui::GetCursorPosX() < transportPos) ImGui::SameLine(transportPos);

            bool isPaused = engine ? engine->isPaused() : true;

            ImGui::PushStyleColor(ImGuiCol_Button, !isPaused ? ImVec4(0.06f, 0.35f, 0.20f, 1.0f) : ImVec4(0.09f, 0.09f, 0.13f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.95f, 0.52f, 1.0f));
            if (ImGui::Button(" Play ", ImVec2(0, btnH))) { if (engine) engine->playSimulation(); }
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0.0f, 4.0f * s_uiScale);

            ImGui::PushStyleColor(ImGuiCol_Button, isPaused ? ImVec4(0.28f, 0.18f, 0.04f, 1.0f) : ImVec4(0.09f, 0.09f, 0.13f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, isPaused ? ImVec4(0.95f, 0.62f, 0.08f, 0.80f) : ImVec4(0.16f, 0.16f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.75f, 0.18f, 1.0f));
            if (ImGui::Button(" || Pause ", ImVec2(0, btnH))) { if (engine) engine->setPaused(true); }
            ImGui::PopStyleColor(3);
            ImGui::SameLine(0.0f, 4.0f * s_uiScale);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.38f, 0.42f, 1.0f));
            if (ImGui::Button(" Stop ", ImVec2(0, btnH))) { if (engine) engine->stopSimulation(); }
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 4.0f * s_uiScale);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.82f, 1.0f, 1.0f));
            if (ImGui::Button(" >| ", ImVec2(0, btnH))) { if (engine) engine->stepSimulation(1, 0.016f); }
            ImGui::PopStyleColor();

            // Right Toolbar Group: Camera Mode, Audio, Tryb Gry, WS Green Pill
            float rightToolbarX = screenW - 335.0f * s_uiScale;
            if (ImGui::GetCursorPosX() < rightToolbarX) ImGui::SameLine(rightToolbarX);

            const char* camModes[] = { "Editor Cam", "TPP (Gracz)", "Top-Down", "Side View" };
            static int camModeIdx = 0;
            ImGui::SetNextItemWidth(100.0f * s_uiScale);
            if (ImGui::Combo("##CamModeToolbar", &camModeIdx, camModes, IM_ARRAYSIZE(camModes))) {
                if (engine) {
                    if (camModeIdx == 0) engine->setCameraMode("editor");
                    else if (camModeIdx == 1) engine->setCameraMode("thirdPerson");
                    else if (camModeIdx == 2) engine->setCameraMode("topDown");
                    else if (camModeIdx == 3) engine->setCameraMode("sideScroller");
                }
            }
            ImGui::SameLine(0.0f, 5.0f * s_uiScale);

            static bool s_audioMuted = false;
            {
                ImVec2 spPos = ImGui::GetCursorScreenPos();
                if (ImGui::Button("##AudioMuteBtn", ImVec2(26.0f * s_uiScale, btnH))) { s_audioMuted = !s_audioMuted; }
                drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(spPos.x + 13.0f * s_uiScale, spPos.y + btnH * 0.5f), 5.5f * s_uiScale, VectorIconType::Speaker, s_audioMuted ? IM_COL32(240, 80, 80, 255) : IM_COL32(180, 195, 215, 255));
            }
            ImGui::SameLine(0.0f, 5.0f * s_uiScale);

            if (ImGui::Button(" Tryb Gry ", ImVec2(0, btnH))) {
                s_immersivePlay = !s_immersivePlay;
            }
            ImGui::SameLine(0.0f, 6.0f * s_uiScale);

            // Green WS status badge matching Image 4
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.04f, 0.24f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.10f, 0.72f, 0.42f, 0.65f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.32f, 0.96f, 0.62f, 1.0f));
            if (ImGui::Button("  WS: 9000 (OK) ", ImVec2(0, btnH))) {
#ifdef _WIN32
                std::system("start http://127.0.0.1:9000");
#endif
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Polaczenie WebSocket aktywne (port 9000). Kliknij, aby otworzyc Studio w przegladarce.");

            ImGui::SameLine(0.0f, 6.0f * s_uiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.44f, 0.95f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.08f, 0.52f, 0.98f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            if (ImGui::Button(" [>] Web Studio (1:1) ", ImVec2(0, btnH))) {
#ifdef _WIN32
                std::system("start http://127.0.0.1:9000");
#endif
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Otworz identyczna powloke React / Web Studio (1:1 pixel-for-pixel)");

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            // ══════════════════════════════════════════════════════════════════════
            // 4A. FAR-LEFT TOOL DOCK (Exact Image 4 & UE5 Outer Mode Rail)
            // ══════════════════════════════════════════════════════════════════════
            float toolDockW = (!s_immersivePlay && s_showPlaceActors) ? (36.0f * s_uiScale) : 0.0f;
            if (!s_immersivePlay && s_showPlaceActors) {
                ImGui::SetNextWindowPos(ImVec2(0, topTotalH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(toolDockW, contentH), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.055f, 0.075f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f * s_uiScale, 8.0f * s_uiScale));
                ImGui::Begin("FarLeftToolDock", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

                static int s_activeToolMode = 1; // 0: Select, 1: Place Actors, 2: Landscape, 3: Foliage, 4: Paint, 5: Fracture, 6: Animation
                struct ToolDockItem { VectorIconType icon; const char* tooltip; };
                const ToolDockItem dockItems[7] = {
                    { VectorIconType::Pointer,   "Selection Mode (Q)" },
                    { VectorIconType::Cube,      "Place Actors Mode (1)" },
                    { VectorIconType::Mountain,  "Landscape Tool (2)" },
                    { VectorIconType::Leaf,      "Foliage Foliage Mode (3)" },
                    { VectorIconType::Brush,     "Mesh Paint Mode (4)" },
                    { VectorIconType::Shapes,    "Fracture & Destruction (5)" },
                    { VectorIconType::Character, "Animation Sequence (6)" }
                };

                float itemSz = 28.0f * s_uiScale;
                for (int i = 0; i < 7; ++i) {
                    ImGui::PushID(100 + i);
                    bool active = (s_activeToolMode == i);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.000f, 0.440f, 0.950f, 1.0f) : ImVec4(0.086f, 0.106f, 0.133f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.080f, 0.500f, 0.980f, 1.0f));
                    if (ImGui::Button("##DockToolBtn", ImVec2(itemSz, itemSz))) {
                        s_activeToolMode = i;
                    }
                    ImGui::PopStyleColor(2);
                    drawVectorIcon(
                        ImGui::GetWindowDrawList(),
                        ImVec2(p.x + itemSz * 0.5f, p.y + itemSz * 0.5f),
                        8.0f * s_uiScale,
                        dockItems[i].icon,
                        active ? IM_COL32(255, 255, 255, 255) : IM_COL32(148, 163, 184, 230)
                    );
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", dockItems[i].tooltip);
                    ImGui::PopID();
                    ImGui::Spacing();
                }

                // Bottom Settings Gear Icon
                float bottomGearY = contentH - itemSz - 12.0f * s_uiScale;
                if (ImGui::GetCursorPosY() < bottomGearY) {
                    ImGui::SetCursorPosY(bottomGearY);
                }
                ImVec2 gp = ImGui::GetCursorScreenPos();
                if (ImGui::Button("##SettingsGearBtn", ImVec2(itemSz, itemSz))) {
                    s_detailsTab = 1;
                    s_showDetails = true;
                }
                drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(gp.x + itemSz * 0.5f, gp.y + itemSz * 0.5f), 7.5f * s_uiScale, VectorIconType::Gear, IM_COL32(148, 163, 184, 220));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("World & Project Settings");

                ImGui::End();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }

            // ══════════════════════════════════════════════════════════════════════
            // 4B. LEFT PANEL: PLACE ACTORS PALETTE (Offset by Far-Left Tool Dock)
            // ══════════════════════════════════════════════════════════════════════
            if (!s_immersivePlay && s_showPlaceActors) {
                float placePanelX = toolDockW;
                float placePanelW = leftPanelW - toolDockW;
                ImGui::SetNextWindowPos(ImVec2(placePanelX, topTotalH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(placePanelW, contentH), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.051f, 0.067f, 0.090f, 1.0f));
                ImGui::Begin("PlaceActorsPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

                // Clean single header: Place Actors | PALETTE
                ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Place Actors");
                ImGui::SameLine(placePanelW - 74.0f * s_uiScale);
                ImGui::TextDisabled("PALETTE");

                static char classSearch[64] = "";
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##SearchClasses", "Search Classes...", classSearch, sizeof(classSearch));
                ImGui::Separator();

                ImGui::Columns(2, "PlaceActorsRailCols", false);
                float railW = 54.0f * s_uiScale;
                ImGui::SetColumnWidth(0, railW);

                // Vertical Category Rail matching Image 4
                const char* catRailNames[9] = { "Fav", "Recent", "Basic", "Lights", "Shapes", "Cinematic", "VFX", "Volumes", "All" };
                for (int i = 0; i < 9; ++i) {
                    ImGui::PushID(i);
                    bool active = (s_placeActorCategory == i);
                    float tabH = 24.0f * s_uiScale;
                    if (active) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.000f, 0.440f, 0.950f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.086f, 0.106f, 0.133f, 0.5f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.80f, 1.0f));
                    }
                    if (ImGui::Button(catRailNames[i], ImVec2(railW - 6.0f * s_uiScale, tabH))) {
                        s_placeActorCategory = i;
                    }
                    ImGui::PopStyleColor(2);
                    ImGui::PopID();
                }

                ImGui::NextColumn();

                const char* catHeaders[9] = { "FAVORITES", "RECENT", "BASIC", "LIGHTS", "SHAPES", "CINEMATIC", "VFX", "VOLUMES", "ALL" };
                ImGui::TextColored(ImVec4(0.90f, 0.93f, 0.98f, 1.0f), "%s", catHeaders[s_placeActorCategory]);
                if (s_placeActorCategory == 2) {
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24.0f * s_uiScale);
                    ImGui::TextDisabled("(7)");
                }

                auto spawnCard = [&](const char* title, const char* desc, int spawnType, int catIdx, VectorIconType iconType, ImU32 iconCol) {
                    if (s_placeActorCategory != catIdx && s_placeActorCategory != 0 && s_placeActorCategory != 1 && s_placeActorCategory != 8) return;
                    if (classSearch[0] != '\0') {
                        std::string tLower = title, sLower = classSearch;
                        std::transform(tLower.begin(), tLower.end(), tLower.begin(), ::tolower);
                        std::transform(sLower.begin(), sLower.end(), sLower.begin(), ::tolower);
                        if (tLower.find(sLower) == std::string::npos) return;
                    }

                    ImGui::PushID(title);
                    float cardH = 38.0f * s_uiScale;
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.086f, 0.106f, 0.133f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.188f, 0.212f, 0.239f, 0.95f));
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f * s_uiScale, 3.0f * s_uiScale));

                    ImGui::BeginChild(title, ImVec2(0, cardH), true, ImGuiWindowFlags_NoScrollbar);
                    ImDrawList* cdl = ImGui::GetWindowDrawList();
                    ImVec2 cPos = ImGui::GetWindowPos();
                    float w = ImGui::GetWindowWidth();

                    // Left icon container (dark rounded square tile matching UE5)
                    float tileSz = 26.0f * s_uiScale;
                    ImVec2 tileMin(cPos.x + 5.0f * s_uiScale, cPos.y + (cardH - tileSz) * 0.5f);
                    ImVec2 tileMax(tileMin.x + tileSz, tileMin.y + tileSz);
                    cdl->AddRectFilled(tileMin, tileMax, IM_COL32(18, 24, 34, 255), 4.0f * s_uiScale);
                    cdl->AddRect(tileMin, tileMax, IM_COL32(48, 56, 72, 220), 4.0f * s_uiScale);
                    drawVectorIcon(cdl, ImVec2(tileMin.x + tileSz * 0.5f, tileMin.y + tileSz * 0.5f), 6.5f * s_uiScale, iconType, iconCol);

                    // Title & Description (crisp non-overlapping layout matching UE5)
                    float textX = 36.0f * s_uiScale;
                    ImGui::SetCursorPos(ImVec2(textX, 3.5f * s_uiScale));
                    ImGui::TextColored(ImVec4(0.96f, 0.97f, 1.0f, 1.0f), "%s", title);

                    ImGui::SetCursorPos(ImVec2(textX, 19.0f * s_uiScale));
                    ImGui::TextColored(ImVec4(0.58f, 0.64f, 0.74f, 1.0f), "%s", desc);

                    // Six drag handle dots (:::) on right matching Image 4
                    float dotsX = cPos.x + w - 16.0f * s_uiScale;
                    float dotsY = cPos.y + cardH * 0.5f;
                    for (int d = -1; d <= 1; ++d) {
                        cdl->AddCircleFilled(ImVec2(dotsX, dotsY + d * 5.0f * s_uiScale), 1.3f * s_uiScale, IM_COL32(100, 115, 140, 180));
                        cdl->AddCircleFilled(ImVec2(dotsX + 4.5f * s_uiScale, dotsY + d * 5.0f * s_uiScale), 1.3f * s_uiScale, IM_COL32(100, 115, 140, 180));
                    }

                    ImGui::EndChild();

                    bool clicked = ImGui::IsItemClicked();
                    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor(2);
                    ImGui::PopID();

                    if (clicked) {
                        auto entity = registry.createEntity();
                        glm::vec3 spawnPos = camPos + front * 7.0f;
                        if (spawnPos.y < 0.5f) spawnPos.y = 0.5f;

                        if (spawnType == 0) { // Actor (Platform Cube)
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"PlatformBlock"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos, glm::vec3(0.0f), glm::vec3(2.0f, 0.8f, 2.0f));
                            registry.get().emplace<ecs::MeshComponent>(entity, ecs::MeshComponent{2});
                            ecs::RigidBodyComponent rb; rb.mass = 0.0f; rb.shapeType = 0; rb.restitution = 0.2f; rb.friction = 0.7f;
                            registry.get().emplace<ecs::RigidBodyComponent>(entity, rb);
                            ecs::MaterialComponent mat; mat.baseColor = glm::vec4(0.2f, 0.45f, 0.75f, 1.0f); mat.roughness = 0.6f;
                            registry.get().emplace<ecs::MaterialComponent>(entity, mat);
                        } else if (spawnType == 1) { // Character (WASD Player)
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"Player"});
                            registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(spawnPos.x, std::max(2.0f, spawnPos.y), spawnPos.z));
                            registry.get().emplace<ecs::MeshComponent>(entity, ecs::MeshComponent{2});
                            ecs::RigidBodyComponent rb; rb.mass = 1.0f; rb.shapeType = 2; rb.capsuleRadius = 0.6f; rb.capsuleHalfHeight = 0.8f;
                            rb.lockRotationX = true; rb.lockRotationZ = true; rb.restitution = 0.0f; rb.friction = 0.8f;
                            registry.get().emplace<ecs::RigidBodyComponent>(entity, rb);
                            ecs::MaterialComponent mat; mat.baseColor = glm::vec4(0.0f, 0.85f, 1.0f, 1.0f); mat.metallic = 0.2f; mat.roughness = 0.3f;
                            registry.get().emplace<ecs::MaterialComponent>(entity, mat);
                        } else if (spawnType == 2) { // Pawn
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"CapsulePawn"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos);
                            registry.get().emplace<ecs::MeshComponent>(entity, ecs::MeshComponent{2});
                            ecs::RigidBodyComponent rb; rb.mass = 1.0f; rb.shapeType = 2; rb.capsuleRadius = 0.5f; rb.capsuleHalfHeight = 0.8f;
                            registry.get().emplace<ecs::RigidBodyComponent>(entity, rb);
                        } else if (spawnType == 3) { // Point Light
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"PointLight"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos + glm::vec3(0.0f, 2.5f, 0.0f), glm::vec3(0.0f), glm::vec3(1.0f));
                            registry.get().emplace<ecs::LightComponent>(entity, ecs::LightComponent{"point", glm::vec3(1.0f, 0.95f, 0.8f), 4.0f, 20.0f});
                        } else if (spawnType == 9) { // Directional Light (Sun)
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"DirectionalLight (Sun)"});
                            registry.get().emplace<ecs::TransformComponent>(entity, camPos + glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(-45.0f, -40.0f, 0.0f), glm::vec3(1.0f));
                            registry.get().emplace<ecs::LightComponent>(entity, ecs::LightComponent{"directional", glm::vec3(1.0f, 0.98f, 0.92f), 3.5f, 0.0f});
                        } else if (spawnType == 10) { // Spot Light (Blender Spot)
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"SpotLight"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos + glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(-60.0f, 0.0f, 0.0f), glm::vec3(1.0f));
                            registry.get().emplace<ecs::LightComponent>(entity, ecs::LightComponent{"spot", glm::vec3(1.0f, 0.95f, 0.85f), 5.0f, 25.0f, 25.0f, 40.0f, 2.0f, 2.0f});
                        } else if (spawnType == 11) { // Area Light (Blender Area)
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"AreaLight"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos + glm::vec3(0.0f, 3.5f, 0.0f), glm::vec3(-45.0f, 0.0f, 0.0f), glm::vec3(1.0f));
                            registry.get().emplace<ecs::LightComponent>(entity, ecs::LightComponent{"area", glm::vec3(1.0f, 0.98f, 0.95f), 6.0f, 20.0f, 25.0f, 40.0f, 3.0f, 2.0f});
                        } else if (spawnType == 4) { // Player Start
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"PlayerStart"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos);
                            registry.get().emplace<ecs::MeshComponent>(entity, ecs::MeshComponent{2});
                        } else if (spawnType == 5) { // Trigger Box
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"TriggerZone"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos, glm::vec3(0.0f), glm::vec3(2.0f));
                            registry.get().emplace<ecs::MeshComponent>(entity, ecs::MeshComponent{2});
                            ecs::RigidBodyComponent rb; rb.mass = 0.0f; rb.shapeType = 0; rb.isSensor = true;
                            registry.get().emplace<ecs::RigidBodyComponent>(entity, rb);
                            ecs::MaterialComponent mat; mat.baseColor = glm::vec4(0.9f, 0.2f, 0.2f, 0.7f);
                            registry.get().emplace<ecs::MaterialComponent>(entity, mat);
                        } else if (spawnType == 6) { // Coin (Collectible)
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"Coin"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos + glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f), glm::vec3(0.7f));
                            registry.get().emplace<ecs::MeshComponent>(entity, ecs::MeshComponent{3});
                            ecs::RigidBodyComponent rb; rb.mass = 0.0f; rb.shapeType = 1; rb.isSensor = true;
                            registry.get().emplace<ecs::RigidBodyComponent>(entity, rb);
                            ecs::MaterialComponent mat; mat.baseColor = glm::vec4(1.0f, 0.84f, 0.0f, 1.0f); mat.metallic = 0.9f; mat.roughness = 0.2f;
                            registry.get().emplace<ecs::MaterialComponent>(entity, mat);
                        } else if (spawnType == 7) { // Dynamic Sphere
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"DynamicSphere"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos + glm::vec3(0.0f, 3.0f, 0.0f));
                            registry.get().emplace<ecs::MeshComponent>(entity, ecs::MeshComponent{3});
                            ecs::RigidBodyComponent rb; rb.mass = 1.0f; rb.shapeType = 1; rb.restitution = 0.8f; rb.friction = 0.2f;
                            registry.get().emplace<ecs::RigidBodyComponent>(entity, rb);
                            ecs::MaterialComponent mat; mat.baseColor = glm::vec4(1.0f, 0.45f, 0.1f, 1.0f); mat.roughness = 0.3f;
                            registry.get().emplace<ecs::MaterialComponent>(entity, mat);
                        } else if (spawnType == 8) { // Fire VFX
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"FireVFX"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos);
                            registry.get().emplace<ecs::ParticleEmitterComponent>(entity, ecs::ParticleEmitterComponent{"fire", glm::vec3(1.0f, 0.4f, 0.0f), 200, 3.0f, 0.4f});
                        }
                        selectedEnt = entity;
                        if (engine) engine->setSelectedEntity(selectedEnt);
                        s_gizmoMode = NativeGizmoMode::TRANSLATE;
                        snprintf(s_engineNotification, sizeof(s_engineNotification), "Dodano aktora: %s", title);
                        s_engineNotificationTimer = 2.5f;
                    }
                };

                ImGui::BeginChild("PlaceActorListScroll", ImVec2(0, 0), false);
                // Exact Basic cards with all 4 Blender Light types
                spawnCard("Actor",          "Empty actor container (Cube)",     0, 2, VectorIconType::Cube,          IM_COL32(148, 163, 184, 255));
                spawnCard("Directional Light", "Celestial Sun with soft shadows", 9, 2, VectorIconType::Light,       IM_COL32(250, 204, 21, 255));
                spawnCard("Point Light",    "Omni-directional inverse-square light", 3, 2, VectorIconType::Light,   IM_COL32(250, 204, 21, 255));
                spawnCard("Spot Light",     "Conical spotlight with cone angle", 10, 2, VectorIconType::Light,       IM_COL32(245, 158, 11, 255));
                spawnCard("Area Light",     "Rectangular soft surface emitter",  11, 2, VectorIconType::Light,       IM_COL32(56, 189, 248, 255));
                spawnCard("Character",      "WASD controllable player pawn",    1, 2, VectorIconType::Character,     IM_COL32(52, 211, 153, 255));
                spawnCard("Pawn",           "Upright pill capsule character",   2, 2, VectorIconType::Pawn,          IM_COL32(56, 189, 248, 255));
                spawnCard("Player Start",   "Initial player spawn transform",   4, 2, VectorIconType::PlayerStart,   IM_COL32(96, 165, 250, 255));
                spawnCard("Trigger Box",    "Box overlap trigger volume",       5, 2, VectorIconType::TriggerBox,    IM_COL32(251, 146, 60, 255));
                spawnCard("Trigger Sphere", "Spherical overlap trigger volume", 6, 2, VectorIconType::TriggerSphere, IM_COL32(250, 204, 21, 255));

                // Additional categories when selected on the rail
                spawnCard("Point Light",    "Omni-directional light",           3, 3, VectorIconType::Light,         IM_COL32(250, 204, 21, 255));
                spawnCard("Cube Mesh",      "Dynamic Jolt RigidBody Box",       0, 4, VectorIconType::Cube,          IM_COL32(56, 189, 248, 255));
                spawnCard("Fire Emitter",   "Niagara Flame VFX",                8, 5, VectorIconType::Sparkles,      IM_COL32(251, 146, 60, 255));
                spawnCard("Prototype Cube", "Standard 1M Prop Cube",            0, 6, VectorIconType::Cube,          IM_COL32(129, 140, 248, 255));
                ImGui::EndChild();

                ImGui::Columns(1);
                ImGui::End();
                ImGui::PopStyleColor();
            }

            // ══════════════════════════════════════════════════════════════════════
            // 5. CENTER PANEL: BORDERLESS 3D VIEWPORT (Matches App.tsx Viewport)
            // ══════════════════════════════════════════════════════════════════════
            ImGui::SetNextWindowPos(ImVec2(leftPanelW, topTotalH), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(centerViewportW, contentH), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.027f, 0.027f, 0.047f, 1.0f));
            ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
            if (viewportPanelSize.x > 10.0f && viewportPanelSize.y > 10.0f) {
                m_viewportAspectRatio = viewportPanelSize.x / viewportPanelSize.y;
            }

            if (m_viewportDescriptor != VK_NULL_HANDLE && viewportPanelSize.x > 0 && viewportPanelSize.y > 0) {
                ImGui::Image((ImTextureID)m_viewportDescriptor, viewportPanelSize);
            }

            ImVec2 vMin = ImGui::GetItemRectMin();
            ImVec2 vSize = ImGui::GetItemRectSize();
            bool isHovered = ImGui::IsItemHovered();
            bool isWindowFocused = ImGui::IsWindowFocused();
            ImVec2 mousePos = ImGui::GetIO().MousePos;
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            camYaw = engine ? engine->getCameraYaw() : -90.0f;
            camPitch = engine ? engine->getCameraPitch() : -20.0f;
            camFov = engine ? engine->getCameraFov() : 45.0f;
            camPos = engine ? engine->getCameraPosition() : glm::vec3(0.0f, 15.0f, 30.0f);

            front = glm::normalize(glm::vec3(
                cos(glm::radians(camYaw)) * cos(glm::radians(camPitch)),
                sin(glm::radians(camPitch)),
                sin(glm::radians(camYaw)) * cos(glm::radians(camPitch))
            ));
            right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
            up = glm::normalize(glm::cross(right, front));

            float aspect = (m_viewportAspectRatio <= 0.1f) ? (16.0f / 9.0f) : m_viewportAspectRatio;
            glm::mat4 curProj = glm::perspective(glm::radians(camFov), aspect, 0.1f, 1000.0f);
            curProj[1][1] *= -1.0f;
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
                float relX = (screenPt.x - vMin.x) / std::max(1.0f, vSize.x);
                float relY = (screenPt.y - vMin.y) / std::max(1.0f, vSize.y);
                glm::vec4 nearPt = invVP * glm::vec4(relX * 2.0f - 1.0f, relY * 2.0f - 1.0f, 0.0f, 1.0f);
                nearPt /= nearPt.w;
                glm::vec4 farPt = invVP * glm::vec4(relX * 2.0f - 1.0f, relY * 2.0f - 1.0f, 1.0f, 1.0f);
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

            // Viewport Hotkeys
            if ((isHovered || isWindowFocused) && !ImGui::GetIO().WantTextInput) {
                bool ctrlDown = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
                if (ctrlDown && ImGui::IsKeyPressed(ImGuiKey_S)) {
                    saveCurrentGame();
                }
                if (ctrlDown && ImGui::IsKeyPressed(ImGuiKey_D) && selectedEnt != entt::null && registry.get().valid(selectedEnt)) {
                    // Duplicate selected actor
                    auto newEnt = registry.createEntity();
                    if (registry.get().all_of<ecs::TagComponent>(selectedEnt)) {
                        registry.get().emplace<ecs::TagComponent>(newEnt, ecs::TagComponent{registry.get().get<ecs::TagComponent>(selectedEnt).name + "_Copy"});
                    }
                    if (registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                        auto t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                        t.position += glm::vec3(1.5f, 0.0f, 0.0f);
                        registry.get().emplace<ecs::TransformComponent>(newEnt, t);
                    }
                    if (registry.get().all_of<ecs::MeshComponent>(selectedEnt)) {
                        registry.get().emplace<ecs::MeshComponent>(newEnt, registry.get().get<ecs::MeshComponent>(selectedEnt));
                    }
                    if (registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt)) {
                        auto rb = registry.get().get<ecs::RigidBodyComponent>(selectedEnt);
                        rb.bodyId = 0xFFFFFFFF;
                        registry.get().emplace<ecs::RigidBodyComponent>(newEnt, rb);
                    }
                    if (registry.get().all_of<ecs::MaterialComponent>(selectedEnt)) {
                        registry.get().emplace<ecs::MaterialComponent>(newEnt, registry.get().get<ecs::MaterialComponent>(selectedEnt));
                    }
                    if (registry.get().all_of<ecs::LightComponent>(selectedEnt)) {
                        registry.get().emplace<ecs::LightComponent>(newEnt, registry.get().get<ecs::LightComponent>(selectedEnt));
                    }
                    selectedEnt = newEnt;
                    if (engine) engine->setSelectedEntity(newEnt);
                    snprintf(s_engineNotification, sizeof(s_engineNotification), "Zduplikowano aktora (Ctrl+D)");
                    s_engineNotificationTimer = 2.0f;
                }
                if (!s_isRmbNavigating) {
                    if (ImGui::IsKeyPressed(ImGuiKey_W) || ImGui::IsKeyPressed(ImGuiKey_G)) s_gizmoMode = NativeGizmoMode::TRANSLATE;
                    if (ImGui::IsKeyPressed(ImGuiKey_E) || (!ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R))) s_gizmoMode = NativeGizmoMode::ROTATE;
                    if (ImGui::IsKeyPressed(ImGuiKey_S) && !ImGui::GetIO().KeyCtrl) s_gizmoMode = NativeGizmoMode::SCALE;
                    if (ImGui::IsKeyPressed(ImGuiKey_Q)) s_gizmoSpaceLocal = !s_gizmoSpaceLocal;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_F) && selectedEnt != entt::null && registry.get().valid(selectedEnt) && registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                    auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                    if (engine) engine->setCamera(t.position - front * 8.0f, camYaw, camPitch, camFov);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedEnt != entt::null && registry.get().valid(selectedEnt)) {
                    registry.get().destroy(selectedEnt);
                    selectedEnt = entt::null;
                    if (engine) engine->setSelectedEntity(entt::null);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    if (s_immersivePlay) s_immersivePlay = false;
                    selectedEnt = entt::null;
                    if (engine) engine->setSelectedEntity(entt::null);
                }
            }

            // --- LIGHTS & ICONS VIEWPORT HIT-TEST (BLENDER & UNREAL STYLE) ---
            entt::entity hoveredLightEntity = entt::null;
            float closestLightDist = 1e9f;

            if (isHovered && !s_isRmbNavigating && !s_isMmbPanning && !s_isAltOrbiting) {
                auto lightEntities = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
                for (auto lEnt : lightEntities) {
                    const auto& tc = lightEntities.get<ecs::TransformComponent>(lEnt);
                    const auto& lc = lightEntities.get<ecs::LightComponent>(lEnt);
                    ImVec2 sPos; float sW = 0.0f;
                    if (worldToScreen(tc.position, sPos, sW) && sW > 0.05f) {
                        float d = hypotf(mousePos.x - sPos.x, mousePos.y - sPos.y);
                        bool isSun = (lc.type == "directional" || lc.type == "sun");
                        float hitRadius = (isSun ? 26.0f : 20.0f) * s_uiScale;
                        if (d <= hitRadius && d < closestLightDist) {
                            closestLightDist = d;
                            hoveredLightEntity = lEnt;
                        }
                    }
                }
            }

            // Gizmo Hit-Test & Interaction
            int hoveredGizmoAxis = 0;
            ImVec2 originScreen(0, 0), ptX(0, 0), ptY(0, 0), ptZ(0, 0);
            bool hasSelectedTransform = (selectedEnt != entt::null && registry.get().valid(selectedEnt) && registry.get().all_of<ecs::TransformComponent>(selectedEnt));

            if (hasSelectedTransform) {
                auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                float gizmoLen = std::max(0.7f, glm::length(camPos - t.position) * 0.14f);
                float originW = 0.0f, wX = 0.0f, wY = 0.0f, wZ = 0.0f;
                bool originVisible = worldToScreen(t.position, originScreen, originW);
                bool xVis = worldToScreen(t.position + glm::vec3(gizmoLen, 0.0f, 0.0f), ptX, wX);
                bool yVis = worldToScreen(t.position + glm::vec3(0.0f, gizmoLen, 0.0f), ptY, wY);
                bool zVis = worldToScreen(t.position + glm::vec3(0.0f, 0.0f, gizmoLen), ptZ, wZ);

                if (originVisible && isHovered && !s_isRmbNavigating && !s_isMmbPanning && !s_isAltOrbiting) {
                    float dCenter = hypotf(mousePos.x - originScreen.x, mousePos.y - originScreen.y);
                    if (dCenter <= 14.0f) hoveredGizmoAxis = 4;
                    else if (xVis && distToSegment(mousePos, originScreen, ptX) <= 12.0f) hoveredGizmoAxis = 1;
                    else if (yVis && distToSegment(mousePos, originScreen, ptY) <= 12.0f) hoveredGizmoAxis = 2;
                    else if (zVis && distToSegment(mousePos, originScreen, ptZ) <= 12.0f) hoveredGizmoAxis = 3;
                }
            }

            if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { s_isRmbNavigating = true; ImGui::SetWindowFocus("Viewport"); }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) s_isRmbNavigating = false;

            if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) { s_isMmbPanning = true; ImGui::SetWindowFocus("Viewport"); }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) s_isMmbPanning = false;

            bool altDown = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
            if (isHovered && altDown && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { s_isAltOrbiting = true; ImGui::SetWindowFocus("Viewport"); }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { s_isAltOrbiting = false; s_draggingGizmoAxis = 0; }

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
                        if (glm::length(vPerp) < 1e-4f) vPerp = (std::abs(s_dragAxisVec.x) > 0.8f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                        s_dragPlaneNormal = glm::normalize(vPerp);
                    } else {
                        s_dragPlaneNormal = -front;
                    }

                    glm::vec3 clickRayOrig, clickRayDir;
                    getRayFromScreen(mousePos, clickRayOrig, clickRayDir);
                    s_dragHasHit = intersectRayPlane(clickRayOrig, clickRayDir, s_dragInitialEntityPos, s_dragPlaneNormal, s_dragInitialHitPoint);
                    s_dragInitialOffset = (s_dragHasHit && s_draggingGizmoAxis <= 3) ? glm::dot(s_dragInitialHitPoint - s_dragInitialEntityPos, s_dragAxisVec) : 0.0f;
                    if (!s_dragHasHit) s_dragInitialHitPoint = s_dragInitialEntityPos;

                    s_dragInitialAngle = atan2f(mousePos.y - originScreen.y, mousePos.x - originScreen.x) * (180.0f / 3.14159265f);
                    s_lastRotAngle = s_dragInitialAngle;
                    s_accumulatedRotAngle = 0.0f;
                    s_dragInitialDist = std::max(10.0f, hypotf(mousePos.x - originScreen.x, mousePos.y - originScreen.y));
                } else if (hoveredLightEntity != entt::null) {
                    // Direct click on Blender Light Icon in Viewport!
                    selectedEnt = hoveredLightEntity;
                    if (engine) engine->setSelectedEntity(selectedEnt);
                    hasSelectedTransform = (selectedEnt != entt::null && registry.get().valid(selectedEnt) && registry.get().all_of<ecs::TransformComponent>(selectedEnt));
                } else {
                    glm::vec3 rayOrig, rayDir;
                    getRayFromScreen(mousePos, rayOrig, rayDir);
                    entt::entity hitEnt = entt::null;
                    float closestDist = 1e9f;

                    auto tView = registry.get().view<ecs::TransformComponent>();
                    for (auto entity : tView) {
                        const auto& tc = tView.get<ecs::TransformComponent>(entity);
                        if (registry.get().all_of<ecs::MeshComponent>(entity) && registry.get().get<ecs::MeshComponent>(entity).meshId == 1) continue;
                        if (registry.get().all_of<ecs::TagComponent>(entity)) {
                            const auto& tn = registry.get().get<ecs::TagComponent>(entity).name;
                            if (tn == "Ground" || tn == "GroundPlane") continue;
                        }

                        glm::vec3 halfExt = glm::max(tc.scale * 0.5f, glm::vec3(0.05f));
                        if (registry.get().all_of<ecs::LightComponent>(entity)) {
                            halfExt = glm::vec3(1.5f); // 3m generous clickable 3D volume for light sources
                        }

                        glm::quat rot = glm::dot(tc.orientation, tc.orientation) < 0.001f ? glm::quat(glm::radians(tc.rotation)) : glm::normalize(tc.orientation);
                        glm::quat invRot = glm::inverse(rot);
                        glm::vec3 rOrigLocal = invRot * (rayOrig - tc.position);
                        glm::vec3 rDirLocal = invRot * rayDir;

                        glm::vec3 invD(
                            std::abs(rDirLocal.x) > 1e-7f ? 1.0f / rDirLocal.x : 1e7f,
                            std::abs(rDirLocal.y) > 1e-7f ? 1.0f / rDirLocal.y : 1e7f,
                            std::abs(rDirLocal.z) > 1e-7f ? 1.0f / rDirLocal.z : 1e7f
                        );
                        glm::vec3 t0 = (-halfExt - rOrigLocal) * invD;
                        glm::vec3 t1 = (+halfExt - rOrigLocal) * invD;
                        glm::vec3 tMinV = glm::min(t0, t1), tMaxV = glm::max(t0, t1);
                        float tNear = std::max({tMinV.x, tMinV.y, tMinV.z});
                        float tFar = std::min({tMaxV.x, tMaxV.y, tMaxV.z});
                        if (tNear <= tFar && tFar >= 0.0f) {
                            float tHit = (tNear >= 0.0f) ? tNear : tFar;
                            if (tHit < closestDist) { closestDist = tHit; hitEnt = entity; }
                        }
                    }
                    selectedEnt = hitEnt;
                    if (engine) engine->setSelectedEntity(hitEnt);
                    hasSelectedTransform = (selectedEnt != entt::null && registry.get().valid(selectedEnt) && registry.get().all_of<ecs::TransformComponent>(selectedEnt));
                }
            }

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
                            if (s_draggingGizmoAxis <= 3) {
                                float deltaAxis = glm::dot(curHit - s_dragInitialEntityPos, s_dragAxisVec) - s_dragInitialOffset;
                                if (s_snapEnabled && s_snapStep > 0.01f) deltaAxis = std::round(deltaAxis / s_snapStep) * s_snapStep;
                                t.position = s_dragInitialEntityPos + s_dragAxisVec * deltaAxis;
                            } else {
                                t.position = s_dragInitialEntityPos + (curHit - s_dragInitialHitPoint);
                            }
                            if (engine) engine->syncEntityTransformPhysics(selectedEnt);
                        }
                    }
                } else if (s_gizmoMode == NativeGizmoMode::ROTATE) {
                    float curAngle = atan2f(mousePos.y - s_dragPivotScreen.y, mousePos.x - s_dragPivotScreen.x) * (180.0f / 3.14159265f);
                    float dAngle = curAngle - s_lastRotAngle;
                    while (dAngle > 180.0f) dAngle -= 360.0f;
                    while (dAngle < -180.0f) dAngle += 360.0f;
                    s_accumulatedRotAngle += dAngle;
                    s_lastRotAngle = curAngle;
                    float finalAngle = s_snapEnabled ? std::round(s_accumulatedRotAngle / 15.0f) * 15.0f : s_accumulatedRotAngle;
                    t.rotation = s_dragInitialRotation;
                    if (s_draggingGizmoAxis == 1) t.rotation.x += finalAngle;
                    else if (s_draggingGizmoAxis == 2 || s_draggingGizmoAxis == 4) t.rotation.y += finalAngle;
                    else if (s_draggingGizmoAxis == 3) t.rotation.z += finalAngle;
                    t.orientation = glm::quat(glm::radians(t.rotation));
                    if (engine) engine->syncEntityTransformPhysics(selectedEnt);
                } else if (s_gizmoMode == NativeGizmoMode::SCALE) {
                    float scaleFactor = hypotf(mousePos.x - s_dragPivotScreen.x, mousePos.y - s_dragPivotScreen.y) / s_dragInitialDist;
                    if (s_snapEnabled) scaleFactor = std::max(0.1f, std::round(scaleFactor / 0.25f) * 0.25f);
                    if (s_draggingGizmoAxis == 1) t.scale.x = std::max(0.01f, s_dragInitialScale.x * scaleFactor);
                    else if (s_draggingGizmoAxis == 2) t.scale.y = std::max(0.01f, s_dragInitialScale.y * scaleFactor);
                    else if (s_draggingGizmoAxis == 3) t.scale.z = std::max(0.01f, s_dragInitialScale.z * scaleFactor);
                    else t.scale = glm::max(glm::vec3(0.01f), s_dragInitialScale * scaleFactor);
                }
            }

            // Camera Navigation (RMB Fly, MMB Pan, Alt+LMB Orbit, Wheel Zoom)
            if (s_isRmbNavigating && engine) {
                camYaw += ImGui::GetIO().MouseDelta.x * 0.20f;
                camPitch = glm::clamp(camPitch - ImGui::GetIO().MouseDelta.y * 0.20f, -89.0f, 89.0f);
                glm::vec3 cf = glm::normalize(glm::vec3(cos(glm::radians(camYaw)) * cos(glm::radians(camPitch)), sin(glm::radians(camPitch)), sin(glm::radians(camYaw)) * cos(glm::radians(camPitch))));
                glm::vec3 cr = glm::normalize(glm::cross(cf, glm::vec3(0.0f, 1.0f, 0.0f)));
                float speed = s_camFlySpeed * ImGui::GetIO().DeltaTime * (s_cameraSpeed / 4.0f);
                if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) speed *= 3.0f;
                if (ImGui::IsKeyDown(ImGuiKey_W)) camPos += cf * speed;
                if (ImGui::IsKeyDown(ImGuiKey_S)) camPos -= cf * speed;
                if (ImGui::IsKeyDown(ImGuiKey_A)) camPos -= cr * speed;
                if (ImGui::IsKeyDown(ImGuiKey_D)) camPos += cr * speed;
                if (ImGui::IsKeyDown(ImGuiKey_E)) camPos.y += speed;
                if (ImGui::IsKeyDown(ImGuiKey_Q)) camPos.y -= speed;
                engine->setCamera(camPos, camYaw, camPitch, camFov);
            }
            if (s_isMmbPanning && engine) {
                camPos -= (right * ImGui::GetIO().MouseDelta.x - up * ImGui::GetIO().MouseDelta.y) * 0.04f;
                engine->setCamera(camPos, camYaw, camPitch, camFov);
            }
            if (s_isAltOrbiting && engine) {
                glm::vec3 pivot = hasSelectedTransform ? registry.get().get<ecs::TransformComponent>(selectedEnt).position : (camPos + front * 10.0f);
                float orbitDist = std::max(0.5f, glm::length(camPos - pivot));
                camYaw += ImGui::GetIO().MouseDelta.x * 0.25f;
                camPitch = glm::clamp(camPitch - ImGui::GetIO().MouseDelta.y * 0.25f, -89.0f, 89.0f);
                glm::vec3 of = glm::normalize(glm::vec3(cos(glm::radians(camYaw)) * cos(glm::radians(camPitch)), sin(glm::radians(camPitch)), sin(glm::radians(camYaw)) * cos(glm::radians(camPitch))));
                engine->setCamera(pivot - of * orbitDist, camYaw, camPitch, camFov);
            }
            if (isHovered && std::abs(ImGui::GetIO().MouseWheel) > 0.01f && engine) {
                engine->setCamera(camPos + front * (ImGui::GetIO().MouseWheel * 3.0f), camYaw, camPitch, camFov);
            }

            // --- RENDER BLENDER-GRADE VIEWPORT LIGHT ICONS (SUN, POINT, SPOT, AREA) ---
            if (!m_standaloneGameMode) {
                auto lightEntities = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
                for (auto lEnt : lightEntities) {
                    const auto& tc = lightEntities.get<ecs::TransformComponent>(lEnt);
                    const auto& lc = lightEntities.get<ecs::LightComponent>(lEnt);

                    ImVec2 iconCenter; float iconW = 0.0f;
                    if (!worldToScreen(tc.position, iconCenter, iconW) || iconW <= 0.05f) continue;

                    bool isSelected = (selectedEnt == lEnt);
                    bool isHov = (hoveredLightEntity == lEnt);
                    bool isSun = (lc.type == "directional" || lc.type == "sun");
                    bool isSpot = (lc.type == "spot");
                    bool isArea = (lc.type == "area");

                    // 1. Vertical Ground Drop-Line (Shows exact height/altitude in 3D)
                    glm::vec3 groundPt(tc.position.x, 0.0f, tc.position.z);
                    ImVec2 groundScreen; float gW = 0.0f;
                    if (worldToScreen(groundPt, groundScreen, gW) && gW > 0.05f) {
                        drawList->AddLine(iconCenter, groundScreen, IM_COL32(200, 210, 230, isSelected ? 160 : 70), 1.2f);
                        drawList->AddCircleFilled(groundScreen, 3.0f * s_uiScale, IM_COL32(200, 210, 230, isSelected ? 200 : 90));
                    }

                    if (isSun) {
                        // === BLENDER SUN LIGHT ===
                        float R0 = 13.0f * s_uiScale;

                        if (isSelected) {
                            drawList->AddCircle(iconCenter, 22.0f * s_uiScale, IM_COL32(0, 240, 255, 255), 0, 2.5f);
                            float bLen = 6.0f * s_uiScale;
                            float bDist = 24.0f * s_uiScale;
                            drawList->AddLine(ImVec2(iconCenter.x - bDist, iconCenter.y - bDist), ImVec2(iconCenter.x - bDist + bLen, iconCenter.y - bDist), IM_COL32(0, 240, 255, 255), 2.0f);
                            drawList->AddLine(ImVec2(iconCenter.x - bDist, iconCenter.y - bDist), ImVec2(iconCenter.x - bDist, iconCenter.y - bDist + bLen), IM_COL32(0, 240, 255, 255), 2.0f);
                            drawList->AddLine(ImVec2(iconCenter.x + bDist, iconCenter.y - bDist), ImVec2(iconCenter.x + bDist - bLen, iconCenter.y - bDist), IM_COL32(0, 240, 255, 255), 2.0f);
                            drawList->AddLine(ImVec2(iconCenter.x + bDist, iconCenter.y - bDist), ImVec2(iconCenter.x + bDist, iconCenter.y - bDist + bLen), IM_COL32(0, 240, 255, 255), 2.0f);
                            drawList->AddLine(ImVec2(iconCenter.x - bDist, iconCenter.y + bDist), ImVec2(iconCenter.x - bDist + bLen, iconCenter.y + bDist), IM_COL32(0, 240, 255, 255), 2.0f);
                            drawList->AddLine(ImVec2(iconCenter.x - bDist, iconCenter.y + bDist), ImVec2(iconCenter.x - bDist, iconCenter.y + bDist - bLen), IM_COL32(0, 240, 255, 255), 2.0f);
                            drawList->AddLine(ImVec2(iconCenter.x + bDist, iconCenter.y + bDist), ImVec2(iconCenter.x + bDist - bLen, iconCenter.y + bDist), IM_COL32(0, 240, 255, 255), 2.0f);
                            drawList->AddLine(ImVec2(iconCenter.x + bDist, iconCenter.y + bDist), ImVec2(iconCenter.x + bDist, iconCenter.y + bDist - bLen), IM_COL32(0, 240, 255, 255), 2.0f);
                        } else if (isHov) {
                            drawList->AddCircle(iconCenter, 20.0f * s_uiScale, IM_COL32(0, 240, 255, 180), 0, 2.0f);
                        }

                        ImU32 sunFill = IM_COL32(251, 191, 36, isSelected ? 255 : (isHov ? 240 : 210));
                        drawList->AddCircleFilled(iconCenter, R0, sunFill);
                        drawList->AddCircle(iconCenter, R0, IM_COL32(255, 255, 255, 240), 0, 1.8f);
                        drawList->AddCircleFilled(iconCenter, 3.5f * s_uiScale, IM_COL32(255, 255, 255, 255));

                        float r1 = 15.5f * s_uiScale, r2 = 23.5f * s_uiScale;
                        for (int k = 0; k < 8; ++k) {
                            float a = (float)k * (3.14159265f / 4.0f);
                            ImVec2 p1(iconCenter.x + cosf(a) * r1, iconCenter.y + sinf(a) * r1);
                            ImVec2 p2(iconCenter.x + cosf(a) * r2, iconCenter.y + sinf(a) * r2);
                            drawList->AddLine(p1, p2, IM_COL32(251, 191, 36, isSelected ? 255 : (isHov ? 240 : 210)), 2.0f);
                        }

                        // Directional Light Beam & Target Indicator
                        float pitch = glm::radians(tc.rotation.x);
                        float yaw = glm::radians(tc.rotation.y);
                        glm::vec3 sunForward = glm::normalize(glm::vec3(cos(yaw) * cos(pitch), sin(pitch), sin(yaw) * cos(pitch)));
                        glm::vec3 beamEnd = tc.position + sunForward * 5.5f;
                        ImVec2 beamScreenEnd; float bEndW = 0.0f;
                        if (worldToScreen(beamEnd, beamScreenEnd, bEndW) && bEndW > 0.05f) {
                            drawList->AddLine(iconCenter, beamScreenEnd, IM_COL32(253, 224, 71, isSelected ? 255 : 200), 2.2f);
                            float bdx = beamScreenEnd.x - iconCenter.x, bdy = beamScreenEnd.y - iconCenter.y;
                            float blen = hypotf(bdx, bdy);
                            if (blen > 12.0f) {
                                float udx = bdx / blen, udy = bdy / blen, px = -udy, py = udx;
                                ImVec2 a1(beamScreenEnd.x - udx * 12.0f + px * 6.0f, beamScreenEnd.y - udy * 12.0f + py * 6.0f);
                                ImVec2 a2(beamScreenEnd.x - udx * 12.0f - px * 6.0f, beamScreenEnd.y - udy * 12.0f - py * 6.0f);
                                drawList->AddTriangleFilled(beamScreenEnd, a1, a2, IM_COL32(253, 224, 71, 240));
                            }
                            glm::vec3 sideVec = glm::cross(sunForward, glm::vec3(0, 1, 0));
                            if (glm::length(sideVec) < 0.1f) sideVec = glm::vec3(1, 0, 0);
                            else sideVec = glm::normalize(sideVec);
                            for (float off : { -1.2f, 1.2f }) {
                                glm::vec3 s0 = tc.position + sideVec * off, s1 = s0 + sunForward * 4.5f;
                                ImVec2 ss0, ss1; float w0, w1;
                                if (worldToScreen(s0, ss0, w0) && worldToScreen(s1, ss1, w1)) {
                                    drawList->AddLine(ss0, ss1, IM_COL32(253, 224, 71, 140), 1.5f);
                                }
                            }
                        }

                        if (isSelected || isHov) {
                            char sunInfo[128];
                            snprintf(sunInfo, sizeof(sunInfo), "Sun Light | Height: %.1fm | Pitch: %.1f deg | Yaw: %.1f deg", tc.position.y, tc.rotation.x, tc.rotation.y);
                            drawList->AddText(ImVec2(iconCenter.x + 22.0f * s_uiScale, iconCenter.y - 8.0f * s_uiScale),
                                              isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(250, 204, 21, 230), sunInfo);
                        }
                    } else if (isSpot) {
                        // === BLENDER SPOT LIGHT (WITH 3D WIREFRAME CONE) ===
                        float R0 = 10.0f * s_uiScale;
                        ImU32 spotCol = IM_COL32((int)(lc.color.r * 255), (int)(lc.color.g * 255), (int)(lc.color.b * 255), isSelected ? 255 : (isHov ? 245 : 210));

                        if (isSelected) drawList->AddCircle(iconCenter, 18.0f * s_uiScale, IM_COL32(0, 240, 255, 255), 0, 2.2f);
                        else if (isHov) drawList->AddCircle(iconCenter, 16.0f * s_uiScale, IM_COL32(0, 240, 255, 170), 0, 1.8f);

                        drawList->AddCircleFilled(iconCenter, R0, spotCol);
                        drawList->AddCircle(iconCenter, R0, IM_COL32(255, 255, 255, 230), 0, 1.5f);
                        drawList->AddCircleFilled(iconCenter, 2.8f * s_uiScale, IM_COL32(255, 255, 255, 255));

                        // 3D Wireframe Cone Gizmo
                        float pitch = glm::radians(tc.rotation.x);
                        float yaw = glm::radians(tc.rotation.y);
                        glm::vec3 spotForward = glm::normalize(glm::vec3(cos(yaw) * cos(pitch), sin(pitch), sin(yaw) * cos(pitch)));
                        glm::vec3 baseCenter = tc.position + spotForward * lc.range;
                        float baseRadius = lc.range * tanf(glm::radians(std::clamp(lc.outerConeAngle, 5.0f, 85.0f)));

                        glm::vec3 right = glm::cross(spotForward, glm::vec3(0, 1, 0));
                        if (glm::length(right) < 0.1f) right = glm::vec3(1, 0, 0);
                        else right = glm::normalize(right);
                        glm::vec3 up = glm::normalize(glm::cross(right, spotForward));

                        // Draw base circle
                        const int cSegs = 24;
                        ImVec2 prevBase; float pbW = 0.0f; bool hasPb = false;
                        for (int s = 0; s <= cSegs; ++s) {
                            float a = (float)s * (6.2831853f / (float)cSegs);
                            glm::vec3 cPt = baseCenter + (right * cosf(a) + up * sinf(a)) * baseRadius;
                            ImVec2 scPt; float scW = 0.0f;
                            if (worldToScreen(cPt, scPt, scW) && scW > 0.05f) {
                                if (hasPb) drawList->AddLine(prevBase, scPt, spotCol, (isSelected ? 2.0f : 1.2f));
                                prevBase = scPt; hasPb = true;
                            } else hasPb = false;
                        }

                        // 4 Side cone lines connecting apex to base
                        for (int k = 0; k < 4; ++k) {
                            float a = (float)k * (3.14159265f / 2.0f);
                            glm::vec3 cPt = baseCenter + (right * cosf(a) + up * sinf(a)) * baseRadius;
                            ImVec2 scPt; float scW = 0.0f;
                            if (worldToScreen(cPt, scPt, scW) && scW > 0.05f) {
                                drawList->AddLine(iconCenter, scPt, spotCol, 1.4f);
                            }
                        }

                        if (isSelected || isHov) {
                            char spotInfo[128];
                            snprintf(spotInfo, sizeof(spotInfo), "Spot Light | Height: %.1fm | Cone: %.0f/%.0f deg", tc.position.y, lc.innerConeAngle, lc.outerConeAngle);
                            drawList->AddText(ImVec2(iconCenter.x + 18.0f * s_uiScale, iconCenter.y - 7.0f * s_uiScale),
                                              isSelected ? IM_COL32(255, 255, 255, 255) : spotCol, spotInfo);
                        }
                    } else if (isArea) {
                        // === BLENDER AREA LIGHT (WITH 3D RECTANGULAR EMITTER) ===
                        float R0 = 10.0f * s_uiScale;
                        ImU32 areaCol = IM_COL32((int)(lc.color.r * 255), (int)(lc.color.g * 255), (int)(lc.color.b * 255), isSelected ? 255 : (isHov ? 245 : 210));

                        if (isSelected) drawList->AddCircle(iconCenter, 18.0f * s_uiScale, IM_COL32(0, 240, 255, 255), 0, 2.2f);
                        else if (isHov) drawList->AddCircle(iconCenter, 16.0f * s_uiScale, IM_COL32(0, 240, 255, 170), 0, 1.8f);

                        // Draw square icon
                        drawList->AddRectFilled(ImVec2(iconCenter.x - R0, iconCenter.y - R0 * 0.7f), ImVec2(iconCenter.x + R0, iconCenter.y + R0 * 0.7f), areaCol, 2.0f);
                        drawList->AddRect(ImVec2(iconCenter.x - R0, iconCenter.y - R0 * 0.7f), ImVec2(iconCenter.x + R0, iconCenter.y + R0 * 0.7f), IM_COL32(255, 255, 255, 230), 2.0f, 0, 1.5f);

                        // 3D Rectangular Surface Wireframe
                        float pitch = glm::radians(tc.rotation.x);
                        float yaw = glm::radians(tc.rotation.y);
                        glm::vec3 areaForward = glm::normalize(glm::vec3(cos(yaw) * cos(pitch), sin(pitch), sin(yaw) * cos(pitch)));
                        glm::vec3 right = glm::cross(areaForward, glm::vec3(0, 1, 0));
                        if (glm::length(right) < 0.1f) right = glm::vec3(1, 0, 0);
                        else right = glm::normalize(right);
                        glm::vec3 up = glm::normalize(glm::cross(right, areaForward));

                        float hx = std::max(0.2f, lc.areaSizeX) * 0.5f;
                        float hy = std::max(0.2f, lc.areaSizeY) * 0.5f;
                        glm::vec3 rc[4] = {
                            tc.position - right * hx - up * hy,
                            tc.position + right * hx - up * hy,
                            tc.position + right * hx + up * hy,
                            tc.position - right * hx + up * hy
                        };
                        ImVec2 src[4]; float rw[4]; bool rAll = true;
                        for (int i = 0; i < 4; ++i) if (!worldToScreen(rc[i], src[i], rw[i])) rAll = false;
                        if (rAll) {
                            for (int i = 0; i < 4; ++i) drawList->AddLine(src[i], src[(i + 1) % 4], areaCol, isSelected ? 2.5f : 1.5f);
                            // Normal direction arrow
                            glm::vec3 normPt = tc.position + areaForward * 1.5f;
                            ImVec2 snPt; float nw = 0.0f;
                            if (worldToScreen(normPt, snPt, nw)) drawList->AddLine(iconCenter, snPt, IM_COL32(255, 255, 255, 200), 1.6f);
                        }

                        if (isSelected || isHov) {
                            char areaInfo[128];
                            snprintf(areaInfo, sizeof(areaInfo), "Area Light | Height: %.1fm | Size: %.1fx%.1fm", tc.position.y, lc.areaSizeX, lc.areaSizeY);
                            drawList->AddText(ImVec2(iconCenter.x + 18.0f * s_uiScale, iconCenter.y - 7.0f * s_uiScale),
                                              isSelected ? IM_COL32(255, 255, 255, 255) : areaCol, areaInfo);
                        }
                    } else {
                        // === BLENDER POINT LIGHT ===
                        float R0 = 9.5f * s_uiScale;
                        ImU32 bulbCol = IM_COL32((int)(lc.color.r * 255), (int)(lc.color.g * 255), (int)(lc.color.b * 255), isSelected ? 255 : (isHov ? 245 : 210));

                        if (isSelected) drawList->AddCircle(iconCenter, 18.0f * s_uiScale, IM_COL32(0, 240, 255, 255), 0, 2.2f);
                        else if (isHov) drawList->AddCircle(iconCenter, 16.0f * s_uiScale, IM_COL32(0, 240, 255, 170), 0, 1.8f);

                        drawList->AddCircleFilled(iconCenter, R0, bulbCol);
                        drawList->AddCircle(iconCenter, R0, IM_COL32(255, 255, 255, 230), 0, 1.5f);
                        drawList->AddCircleFilled(iconCenter, 2.8f * s_uiScale, IM_COL32(255, 255, 255, 255));

                        float r1 = 12.0f * s_uiScale, r2 = 17.0f * s_uiScale;
                        for (int k = 0; k < 8; ++k) {
                            float a = (float)k * (3.14159265f / 4.0f);
                            ImVec2 p1(iconCenter.x + cosf(a) * r1, iconCenter.y + sinf(a) * r1);
                            ImVec2 p2(iconCenter.x + cosf(a) * r2, iconCenter.y + sinf(a) * r2);
                            drawList->AddLine(p1, p2, bulbCol, 1.6f);
                        }

                        if (isSelected || isHov) {
                            const int segs = 36;
                            ImVec2 lastPt; float lastW = 0.0f; bool hasLast = false;
                            for (int s = 0; s <= segs; ++s) {
                                float a = (float)s * (6.2831853f / (float)segs);
                                glm::vec3 worldPt = tc.position + glm::vec3(cosf(a), 0.0f, sinf(a)) * lc.range;
                                ImVec2 scrPt; float w = 0.0f;
                                if (worldToScreen(worldPt, scrPt, w) && w > 0.05f) {
                                    if (hasLast) drawList->AddLine(lastPt, scrPt, IM_COL32((int)(lc.color.r * 255), (int)(lc.color.g * 255), (int)(lc.color.b * 255), isSelected ? 180 : 100), 1.5f);
                                    lastPt = scrPt; hasLast = true;
                                } else hasLast = false;
                            }

                            char ptInfo[128];
                            snprintf(ptInfo, sizeof(ptInfo), "Point Light | Height: %.1fm | Range: %.1fm", tc.position.y, lc.range);
                            drawList->AddText(ImVec2(iconCenter.x + 18.0f * s_uiScale, iconCenter.y - 7.0f * s_uiScale),
                                              isSelected ? IM_COL32(255, 255, 255, 255) : bulbCol, ptInfo);
                        }
                    }
                }
            }
            // Selection BoxHelper (0x00f0ff cyan matching App.tsx) & 3D Transform Gizmo
            if (hasSelectedTransform) {
                auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                bool isLightEnt = registry.get().all_of<ecs::LightComponent>(selectedEnt);
                if (!isLightEnt) {
                    glm::vec3 halfExt = glm::max(t.scale * 0.5f, glm::vec3(0.4f));
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
                    ImVec2 sCorners[8]; float sW[8]; bool allVis = true;
                    for (int i = 0; i < 8; ++i) if (!worldToScreen(corners[i], sCorners[i], sW[i])) allVis = false;
                    if (allVis) {
                        const int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
                        for (int e = 0; e < 12; ++e) drawList->AddLine(sCorners[edges[e][0]], sCorners[edges[e][1]], IM_COL32(0, 240, 255, 190), 1.5f);
                    }
                }

                float origW = 0.0f;
                if (worldToScreen(t.position, originScreen, origW)) {
                    int act = (s_draggingGizmoAxis > 0) ? s_draggingGizmoAxis : hoveredGizmoAxis;
                    ImU32 colX = (act == 1) ? IM_COL32(255, 255, 60, 255) : IM_COL32(239, 68, 68, 255);
                    ImU32 colY = (act == 2) ? IM_COL32(255, 255, 60, 255) : IM_COL32(34, 197, 94, 255);
                    ImU32 colZ = (act == 3) ? IM_COL32(255, 255, 60, 255) : IM_COL32(59, 130, 246, 255);

                    if (s_gizmoMode == NativeGizmoMode::ROTATE) {
                        float gizmoLen = std::max(0.7f, glm::length(camPos - t.position) * 0.14f);
                        const int rSegs = 32;
                        auto drawRing = [&](const glm::vec3& axisU, const glm::vec3& axisV, ImU32 col, int axisId) {
                            ImVec2 prevPt; float prevW = 0.0f; bool hasPrev = false;
                            float thickness = (act == axisId) ? 3.8f : 2.2f;
                            for (int i = 0; i <= rSegs; ++i) {
                                float a = (float)i * (6.2831853f / (float)rSegs);
                                glm::vec3 rPt = t.position + (axisU * cosf(a) + axisV * sinf(a)) * gizmoLen;
                                ImVec2 sPt; float sW = 0.0f;
                                if (worldToScreen(rPt, sPt, sW) && sW > 0.05f) {
                                    if (hasPrev) drawList->AddLine(prevPt, sPt, col, thickness);
                                    prevPt = sPt;
                                    hasPrev = true;
                                } else hasPrev = false;
                            }
                        };
                        drawRing(glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), colX, 1); // Pitch (Red ring)
                        drawRing(glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), colY, 2); // Yaw (Green ring)
                        drawRing(glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), colZ, 3); // Roll (Blue ring)
                        drawList->AddCircleFilled(originScreen, 5.0f, IM_COL32(245, 245, 255, 230));
                    } else {
                        drawList->AddLine(originScreen, ptX, colX, (act == 1) ? 4.0f : 2.8f);
                        drawList->AddLine(originScreen, ptY, colY, (act == 2) ? 4.0f : 2.8f);
                        drawList->AddLine(originScreen, ptZ, colZ, (act == 3) ? 4.0f : 2.8f);
                        drawList->AddCircleFilled(originScreen, 5.5f, IM_COL32(245, 245, 255, 230));
                    }
                }
            }

            // ── Bottom-Left 3D Coordinate Tripod (Matches UnrealCoordinateGizmo in App.tsx)
            if (!s_immersivePlay) {
                ImVec2 gizmoOrigin(vMin.x + 36.0f * s_uiScale, vMin.y + vSize.y - 34.0f * s_uiScale);
                float axisLen = 22.0f * s_uiScale;
                auto projectAxisDir = [&](const glm::vec3& dir) -> ImVec2 {
                    glm::vec3 viewDir = glm::mat3(curView) * dir;
                    return ImVec2(gizmoOrigin.x + viewDir.x * axisLen, gizmoOrigin.y - viewDir.y * axisLen);
                };
                ImVec2 endX = projectAxisDir(glm::vec3(1, 0, 0));
                ImVec2 endY = projectAxisDir(glm::vec3(0, 1, 0));
                ImVec2 endZ = projectAxisDir(glm::vec3(0, 0, 1));
                drawList->AddLine(gizmoOrigin, endX, IM_COL32(239, 68, 68, 240), 2.0f);
                drawList->AddLine(gizmoOrigin, endY, IM_COL32(34, 197, 94, 240), 2.0f);
                drawList->AddLine(gizmoOrigin, endZ, IM_COL32(59, 130, 246, 240), 2.0f);
                drawList->AddText(ImVec2(endX.x + 2, endX.y - 6), IM_COL32(248, 113, 113, 255), "X");
                drawList->AddText(ImVec2(endY.x + 2, endY.y - 6), IM_COL32(74, 222, 128, 255), "Y");
                drawList->AddText(ImVec2(endZ.x + 2, endZ.y - 6), IM_COL32(96, 165, 250, 255), "Z");
                drawList->AddCircleFilled(gizmoOrigin, 2.8f * s_uiScale, IM_COL32(226, 232, 240, 255));

                // ── Viewport Top-Left Floating Pill (Icons + World + Snap + Cam Speed matching Image B)
                ImGui::SetCursorPos(ImVec2(10.0f * s_uiScale, 10.0f * s_uiScale));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.055f, 0.086f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.18f, 0.25f, 0.85f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f * s_uiScale, 4.0f * s_uiScale));
                ImGui::BeginChild("VpOverlayTopLeft", ImVec2(276.0f * s_uiScale, 30.0f * s_uiScale), true, ImGuiWindowFlags_NoScrollbar);

                auto gizmoToolBtn = [&](const char* id, NativeGizmoMode mode, VectorIconType icon) {
                    bool active = (s_gizmoMode == mode);
                    ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.0f, 0.47f, 0.83f, 1.0f) : ImVec4(0.09f, 0.09f, 0.13f, 1.0f));
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    float sz = 22.0f * s_uiScale;
                    if (ImGui::Button(id, ImVec2(sz, sz))) s_gizmoMode = mode;
                    ImGui::PopStyleColor();
                    drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + sz * 0.5f, p.y + sz * 0.5f), 5.2f * s_uiScale, icon, IM_COL32(255, 255, 255, 245));
                    ImGui::SameLine(0.0f, 3.0f * s_uiScale);
                };

                gizmoToolBtn("##GizT", NativeGizmoMode::TRANSLATE, VectorIconType::Translate);
                gizmoToolBtn("##GizR", NativeGizmoMode::ROTATE,    VectorIconType::Rotate);
                gizmoToolBtn("##GizS", NativeGizmoMode::SCALE,     VectorIconType::Scale);

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.22f, 0.85f, 1.0f, 1.0f));
                if (ImGui::Button(s_gizmoSpaceLocal ? " Local " : " World ", ImVec2(0, 22.0f * s_uiScale))) {
                    s_gizmoSpaceLocal = !s_gizmoSpaceLocal;
                }
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, 4.0f * s_uiScale);

                if (ImGui::Button(s_snapEnabled ? " Snap: 0.5m " : " Snap: Off ", ImVec2(0, 22.0f * s_uiScale))) {
                    s_snapEnabled = !s_snapEnabled;
                }
                ImGui::SameLine(0.0f, 4.0f * s_uiScale);

                if (ImGui::Button((" " + std::to_string(s_cameraSpeed) + " ").c_str(), ImVec2(0, 22.0f * s_uiScale))) {
                    s_cameraSpeed = (s_cameraSpeed % 8) + 1;
                }

                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(2);

                // ── Viewport Top-Right Floating Pill (Perspective | Vulkan | 100% (1920x1080) | UI: 100%)
                float trPillW = 335.0f * s_uiScale;
                ImGui::SetCursorPos(ImVec2(std::max(10.0f, viewportPanelSize.x - trPillW - 10.0f * s_uiScale), 10.0f * s_uiScale));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.055f, 0.086f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.18f, 0.25f, 0.85f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * s_uiScale, 5.0f * s_uiScale));
                ImGui::BeginChild("VpOverlayTopRight", ImVec2(trPillW, 30.0f * s_uiScale), true, ImGuiWindowFlags_NoScrollbar);

                ImGui::TextColored(ImVec4(0.85f, 0.88f, 0.95f, 1.0f), "Perspective");
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.22f, 0.85f, 1.0f, 1.0f), "Vulkan v");
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.20f, 0.92f, 0.58f, 1.0f), "%d%% (%ux%u)", static_cast<int>(std::round(m_resolutionScale * 100.0f)), m_width, m_height);
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.68f, 0.72f, 0.98f, 1.0f), "UI: %d%%", static_cast<int>(std::round(m_uiScale * 100.0f)));

                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(2);

                // ── Bottom-Left Navigation Hint Pill (Elevated above XYZ tripod like Image B)
                float navHintW = std::min(535.0f * s_uiScale, viewportPanelSize.x - 24.0f);
                ImGui::SetCursorPos(ImVec2(12.0f * s_uiScale, viewportPanelSize.y - 84.0f * s_uiScale));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.035f, 0.035f, 0.058f, 0.82f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.20f, 0.28f, 0.75f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * s_uiScale, 5.0f * s_uiScale));
                ImGui::BeginChild("NavHintPill", ImVec2(navHintW, 26.0f * s_uiScale), true, ImGuiWindowFlags_NoScrollbar);
                ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "o");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.80f, 1.0f), "PPM+WASD: Lot - Alt+LPM: Orbituj - SPM: Przesun - Scroll: Zoom - LPM: Zaznacz - W/E/R: Gizmo - F: Skup");
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(2);

                // ── Bottom-Center AI Game Creator Floating Bar (Matches App.tsx Glass Bar)
                float promptBarW = std::min(620.0f * s_uiScale, viewportPanelSize.x * 0.85f);
                ImGui::SetCursorPos(ImVec2((viewportPanelSize.x - promptBarW) * 0.5f, viewportPanelSize.y - 50.0f * s_uiScale));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.051f, 0.067f, 0.090f, 0.94f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.00f, 0.55f, 0.85f, 0.65f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 18.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * s_uiScale, 6.0f * s_uiScale));
                ImGui::BeginChild("AIPromptPill", ImVec2(promptBarW, 40.0f * s_uiScale), true, ImGuiWindowFlags_NoScrollbar);

                ImVec2 wPos = ImGui::GetCursorScreenPos();
                drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(wPos.x + 10.0f * s_uiScale, wPos.y + 14.0f * s_uiScale), 7.5f * s_uiScale, VectorIconType::Wand, IM_COL32(34, 211, 238, 255));
                ImGui::Dummy(ImVec2(22.0f * s_uiScale, 24.0f * s_uiScale));
                ImGui::SameLine();

                static char aiPromptNative[256] = "";
                ImGui::SetNextItemWidth(promptBarW - 145.0f * s_uiScale);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                bool submitPrompt = ImGui::InputTextWithHint("##AIPromptNative", "Napisz do AI co chcesz stworzyc (np. 'Wyscigi aut', 'Arkanoid', 'Uklad Planet')...", aiPromptNative, sizeof(aiPromptNative), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::PopStyleColor(2);
                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.44f, 0.95f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.08f, 0.52f, 0.98f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                if (ImGui::Button(" Generuj ", ImVec2(96.0f * s_uiScale, 28.0f * s_uiScale)) || submitPrompt) {
                    std::string pStr = aiPromptNative;
                    aiPromptNative[0] = '\0';
                    std::transform(pStr.begin(), pStr.end(), pStr.begin(), ::tolower);
                    if (engine) {
                        if (pStr.find("tor") != std::string::npos || pStr.find("przeszkod") != std::string::npos || pStr.find("obstacle") != std::string::npos) {
                            engine->resetScene("empty");
                            engine->spawnEntityFromJson({
                                {"tag", "Player"},
                                {"transform", {{"x", 0}, {"y", 2}, {"z", 10}, {"sx", 1}, {"sy", 1}, {"sz", 1}}},
                                {"mesh", {{"meshId", 2}}},
                                {"rigidBody", {{"mass", 1.0f}, {"shapeType", 2}, {"capsuleRadius", 0.6f}, {"capsuleHalfHeight", 0.8f}, {"lockRotationX", true}, {"lockRotationZ", true}}},
                                {"material", {{"color", {0.0f, 0.85f, 1.0f, 1.0f}}, {"metallic", 0.2f}, {"roughness", 0.3f}}}
                            });
                            for (int i = 0; i < 6; ++i) {
                                float pz = 5.0f - i * 6.0f;
                                float py = 0.5f + i * 1.2f;
                                float px = (i % 2 == 1) ? 2.5f : -2.5f;
                                engine->spawnEntityFromJson({
                                    {"tag", "Platform_" + std::to_string(i)},
                                    {"transform", {{"x", px}, {"y", py}, {"z", pz}, {"sx", 3.0f}, {"sy", 0.6f}, {"sz", 3.0f}}},
                                    {"mesh", {{"meshId", 2}}},
                                    {"rigidBody", {{"mass", 0.0f}, {"shapeType", 0}, {"restitution", 0.1f}, {"friction", 0.8f}}},
                                    {"material", {{"color", {0.2f, 0.5f, 0.9f, 1.0f}}, {"roughness", 0.5f}}}
                                });
                                engine->spawnEntityFromJson({
                                    {"tag", "Coin_" + std::to_string(i)},
                                    {"transform", {{"x", px}, {"y", py + 1.5f}, {"z", pz}, {"sx", 0.8f}, {"sy", 0.8f}, {"sz", 0.8f}}},
                                    {"mesh", {{"meshId", 3}}},
                                    {"rigidBody", {{"mass", 0.0f}, {"shapeType", 1}, {"isSensor", true}}},
                                    {"material", {{"color", {1.0f, 0.84f, 0.0f, 1.0f}}, {"metallic", 0.9f}, {"roughness", 0.2f}}}
                                });
                            }
                            engine->setCameraMode("thirdPerson");
                            snprintf(s_engineNotification, sizeof(s_engineNotification), "AI: Wygenerowano tor przeszkod z monetami i graczem!");
                            s_engineNotificationTimer = 4.0f;
                        } else if (pStr.find("auto") != std::string::npos || pStr.find("racer") != std::string::npos || pStr.find("wyscig") != std::string::npos) {
                            engine->setCurrentPreset("racer"); engine->resetScene("racer");
                            snprintf(s_engineNotification, sizeof(s_engineNotification), "AI: Zaladowano gre wyscigowa Hover Racer!");
                            s_engineNotificationTimer = 3.0f;
                        } else if (pStr.find("arkanoid") != std::string::npos || pStr.find("breakout") != std::string::npos || pStr.find("cegl") != std::string::npos) {
                            engine->setCurrentPreset("breakout"); engine->resetScene("breakout");
                            snprintf(s_engineNotification, sizeof(s_engineNotification), "AI: Zaladowano gre 3D Arkanoid / Breakout!");
                            s_engineNotificationTimer = 3.0f;
                        } else if (pStr.find("platform") != std::string::npos || pStr.find("skok") != std::string::npos) {
                            engine->setCurrentPreset("platformer"); engine->resetScene("platformer");
                            snprintf(s_engineNotification, sizeof(s_engineNotification), "AI: Zaladowano gre platformowa 3D!");
                            s_engineNotificationTimer = 3.0f;
                        } else if (pStr.find("shooter") != std::string::npos || pStr.find("kosmos") != std::string::npos || pStr.find("strzel") != std::string::npos) {
                            engine->setCurrentPreset("shooter"); engine->resetScene("shooter");
                            snprintf(s_engineNotification, sizeof(s_engineNotification), "AI: Zaladowano strzelanke Space Shooter!");
                            s_engineNotificationTimer = 3.0f;
                        } else {
                            engine->setCurrentPreset("empty"); engine->resetScene("empty");
                            snprintf(s_engineNotification, sizeof(s_engineNotification), "AI: Wyczyszczono scene do pustego poziomu.");
                            s_engineNotificationTimer = 3.0f;
                        }
                    }
                }
                ImGui::PopStyleColor(3);

                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(2);
            } else {
                // Exit Immersive Play button (top-right)
                ImGui::SetCursorPos(ImVec2(viewportPanelSize.x - 155.0f * s_uiScale, 14.0f * s_uiScale));
                if (ImGui::Button(" Wyjdz z Gry (F11) ")) {
                    s_immersivePlay = false;
                }
            }

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            // ══════════════════════════════════════════════════════════════════════
            // 6. RIGHT TOP PANEL: OUTLINER (Matches OutlinerPanel in App.tsx)
            // ══════════════════════════════════════════════════════════════════════
            float outlinerH = s_showDetails ? (contentH * 0.46f) : contentH;
            if (!s_immersivePlay && s_showOutliner) {
                ImGui::SetNextWindowPos(ImVec2(leftPanelW + centerViewportW, topTotalH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(rightPanelW, outlinerH), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.055f, 0.086f, 1.0f));
                ImGui::Begin("OutlinerPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

                ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Outliner");

                static char outlinerFilter[64] = "";
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##OutlinerSearch", "Search...", outlinerFilter, sizeof(outlinerFilter));

                ImGui::TextDisabled("  ITEM LABEL");
                ImGui::SameLine(rightPanelW - 72.0f * s_uiScale);
                ImGui::TextDisabled("TYPE");
                ImGui::Separator();

                ImGui::BeginChild("OutlinerTreeScroll", ImVec2(0, -26.0f * s_uiScale), false);
                ImDrawList* odl = ImGui::GetWindowDrawList();

                // World Root Row matching Image B
                {
                    ImVec2 rp = ImGui::GetCursorScreenPos();
                    drawVectorIcon(odl, ImVec2(rp.x + 8.0f * s_uiScale, rp.y + 8.0f * s_uiScale), 5.0f * s_uiScale, VectorIconType::WarningTri, IM_COL32(245, 158, 11, 255));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0f * s_uiScale);
                    ImGui::TextColored(ImVec4(0.92f, 0.94f, 0.98f, 1.0f), "Lvl_MainLevel (Editor)");
                    ImGui::SameLine(rightPanelW - 65.0f * s_uiScale);
                    ImGui::TextDisabled("World");
                }

                // Lighting Folder
                if (outlinerFilter[0] != '\0') ImGui::SetNextItemOpen(true);
                if (ImGui::TreeNodeEx("##LightingFolder", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow, "    Lighting")) {
                    ImVec2 fp = ImGui::GetItemRectMin();
                    drawVectorIcon(odl, ImVec2(fp.x + 24.0f * s_uiScale, fp.y + 8.0f * s_uiScale), 5.0f * s_uiScale, VectorIconType::Folder, IM_COL32(245, 158, 11, 255));

                    auto lightEntities = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
                    for (auto lEnt : lightEntities) {
                        const auto& lc = lightEntities.get<ecs::LightComponent>(lEnt);
                        bool isSun = (lc.type == "directional" || lc.type == "sun");
                        bool isSpot = (lc.type == "spot");
                        bool isArea = (lc.type == "area");
                        std::string tag = isSun ? "DirectionalLight (Sun)" : (isSpot ? "SpotLight" : (isArea ? "AreaLight" : "PointLight"));
                        if (registry.get().all_of<ecs::TagComponent>(lEnt)) {
                            tag = registry.get().get<ecs::TagComponent>(lEnt).name;
                        }

                        if (outlinerFilter[0] != '\0') {
                            std::string f = outlinerFilter, l = tag;
                            std::transform(f.begin(), f.end(), f.begin(), ::tolower);
                            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
                            if (l.find(f) == std::string::npos) continue;
                        }

                        bool isSelected = (selectedEnt == lEnt);
                        ImGui::PushID(static_cast<int>(static_cast<uint32_t>(lEnt)));
                        ImVec2 lp = ImGui::GetCursorScreenPos();
                        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.000f, 0.440f, 0.950f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.080f, 0.500f, 0.980f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.000f, 0.440f, 0.950f, 1.0f));

                        std::string selectId = "##Select_Light_" + std::to_string(static_cast<uint32_t>(lEnt));
                        if (ImGui::Selectable(selectId.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 20.0f * s_uiScale))) {
                            selectedEnt = lEnt;
                            if (engine) engine->setSelectedEntity(selectedEnt);
                        }
                        ImGui::PopStyleColor(3);

                        drawVectorIcon(odl, ImVec2(lp.x + 4.0f * s_uiScale, lp.y + 8.0f * s_uiScale), 4.5f * s_uiScale, VectorIconType::Eye, IM_COL32(148, 163, 184, 255));
                        drawVectorIcon(odl, ImVec2(lp.x + 18.0f * s_uiScale, lp.y + 8.0f * s_uiScale), 5.0f * s_uiScale, VectorIconType::Light, isSun ? IM_COL32(250, 204, 21, 255) : IM_COL32(245, 158, 11, 255));

                        ImGui::SetCursorScreenPos(ImVec2(lp.x + 28.0f * s_uiScale, lp.y + 2.0f * s_uiScale));
                        ImGui::TextColored(isSelected ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.92f, 0.94f, 0.98f, 1.0f), "%s", tag.c_str());

                        const char* typeStr = isSun ? "SunLight" : (isSpot ? "SpotLight" : (isArea ? "AreaLight" : "PointLight"));
                        ImGui::SameLine(rightPanelW - 76.0f * s_uiScale);
                        ImGui::TextDisabled("%s", typeStr);

                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                } else {
                    ImVec2 fp = ImGui::GetItemRectMin();
                    drawVectorIcon(odl, ImVec2(fp.x + 24.0f * s_uiScale, fp.y + 8.0f * s_uiScale), 5.0f * s_uiScale, VectorIconType::Folder, IM_COL32(245, 158, 11, 255));
                }

                // Actors & Geometry Folder with PHYS badges matching Image B
                if (outlinerFilter[0] != '\0') ImGui::SetNextItemOpen(true);
                if (ImGui::TreeNodeEx("##ActorsFolder", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow, "    Actors & Geometry")) {
                    ImVec2 fp = ImGui::GetItemRectMin();
                    drawVectorIcon(odl, ImVec2(fp.x + 24.0f * s_uiScale, fp.y + 8.0f * s_uiScale), 5.0f * s_uiScale, VectorIconType::Folder, IM_COL32(34, 211, 238, 255));

                    auto view = registry.get().view<ecs::TransformComponent>();
                    for (auto entity : view) {
                        if (registry.get().all_of<ecs::LightComponent>(entity)) continue; // Listed cleanly in Lighting folder above
                        std::string label = "Entity_" + std::to_string(static_cast<uint32_t>(entity));
                        if (registry.get().all_of<ecs::TagComponent>(entity)) {
                            label = registry.get().get<ecs::TagComponent>(entity).name;
                        }
                        if (outlinerFilter[0] != '\0') {
                            std::string f = outlinerFilter, l = label;
                            std::transform(f.begin(), f.end(), f.begin(), ::tolower);
                            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
                            if (l.find(f) == std::string::npos) continue;
                        }

                        bool hasRb = registry.get().all_of<ecs::RigidBodyComponent>(entity);
                        bool isDynamic = hasRb && registry.get().get<ecs::RigidBodyComponent>(entity).mass > 0.0f;
                        const char* typeStr = hasRb ? "RigidBodyActor" : "StaticMeshActor";
                        bool isSelected = (selectedEnt == entity);

                        ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity)));
                        ImVec2 rowP = ImGui::GetCursorScreenPos();
                        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.000f, 0.440f, 0.950f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.080f, 0.500f, 0.980f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.000f, 0.440f, 0.950f, 1.0f));
                        if (ImGui::Selectable("##ActorRowSel", isSelected, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 20.0f * s_uiScale))) {
                            selectedEnt = entity;
                            if (engine) engine->setSelectedEntity(selectedEnt);
                        }
                        ImGui::PopStyleColor(3);
                        drawVectorIcon(odl, ImVec2(rowP.x + 5.0f * s_uiScale, rowP.y + 10.0f * s_uiScale), 4.2f * s_uiScale, VectorIconType::Eye, isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(148, 163, 184, 200));
                        drawVectorIcon(odl, ImVec2(rowP.x + 19.0f * s_uiScale, rowP.y + 10.0f * s_uiScale), 4.8f * s_uiScale, VectorIconType::Cube, isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(56, 189, 248, 255));

                        std::string shortName = label.size() > 16 ? (label.substr(0, 14) + "..") : label;
                        odl->AddText(ImVec2(rowP.x + 29.0f * s_uiScale, rowP.y + 2.5f * s_uiScale), isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(230, 236, 248, 255), shortName.c_str());

                        if (isDynamic) {
                            ImVec2 badgeMin(rowP.x + 130.0f * s_uiScale, rowP.y + 2.5f * s_uiScale);
                            ImVec2 badgeMax(rowP.x + 162.0f * s_uiScale, rowP.y + 17.0f * s_uiScale);
                            odl->AddRectFilled(badgeMin, badgeMax, IM_COL32(120, 65, 10, 180), 3.0f);
                            odl->AddText(ImVec2(badgeMin.x + 3.0f * s_uiScale, badgeMin.y + 0.5f * s_uiScale), IM_COL32(251, 191, 36, 255), "PHYS");
                        }

                        odl->AddText(ImVec2(rowP.x + rightPanelW - 118.0f * s_uiScale, rowP.y + 2.5f * s_uiScale), isSelected ? IM_COL32(220, 235, 255, 255) : IM_COL32(115, 125, 148, 255), typeStr);
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                } else {
                    ImVec2 fp = ImGui::GetItemRectMin();
                    drawVectorIcon(odl, ImVec2(fp.x + 24.0f * s_uiScale, fp.y + 8.0f * s_uiScale), 5.0f * s_uiScale, VectorIconType::Folder, IM_COL32(34, 211, 238, 255));
                }

                ImGui::EndChild();

                size_t actorCount = registry.get().view<ecs::TransformComponent>().size();
                ImGui::Separator();
                ImGui::TextDisabled("%zu ACTORS", actorCount);
                ImGui::SameLine(rightPanelW - 92.0f * s_uiScale);
                ImGui::TextDisabled("(%d SELECTED)", selectedEnt != entt::null ? 1 : 0);

                ImGui::End();
                ImGui::PopStyleColor();
            }

            // ══════════════════════════════════════════════════════════════════════
            // 7. RIGHT BOTTOM PANEL: DETAILS & WORLD SETTINGS (Matches Image B)
            // ══════════════════════════════════════════════════════════════════════
            if (!s_immersivePlay && s_showDetails) {
                float detailsY = topTotalH + (s_showOutliner ? outlinerH : 0.0f);
                float detailsH = s_showOutliner ? (contentH - outlinerH) : contentH;

                ImGui::SetNextWindowPos(ImVec2(leftPanelW + centerViewportW, detailsY), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(rightPanelW, detailsH), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.055f, 0.086f, 1.0f));
                ImGui::Begin("DetailsPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

                // Clean top tabs: Details x | World Settings
                {
                    ImVec2 t0 = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_Button, s_detailsTab == 0 ? ImVec4(0.095f, 0.095f, 0.14f, 1.0f) : ImVec4(0.045f, 0.045f, 0.07f, 1.0f));
                    if (ImGui::Button("  Details   x ", ImVec2(0, 24.0f * s_uiScale))) s_detailsTab = 0;
                    ImGui::PopStyleColor();
                    if (s_detailsTab == 0) {
                        ImGui::GetWindowDrawList()->AddLine(t0, ImVec2(ImGui::GetItemRectMax().x, t0.y), IM_COL32(0, 130, 240, 255), 2.0f);
                    }
                    ImGui::SameLine();

                    ImVec2 t1 = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_Button, s_detailsTab == 1 ? ImVec4(0.095f, 0.095f, 0.14f, 1.0f) : ImVec4(0.045f, 0.045f, 0.07f, 1.0f));
                    if (ImGui::Button("  World Settings ", ImVec2(0, 24.0f * s_uiScale))) s_detailsTab = 1;
                    ImGui::PopStyleColor();
                    if (s_detailsTab == 1) {
                        ImGui::GetWindowDrawList()->AddLine(t1, ImVec2(ImGui::GetItemRectMax().x, t1.y), IM_COL32(0, 130, 240, 255), 2.0f);
                    }
                }
                ImGui::Separator();

                if (s_detailsTab == 0) {
                    if (selectedEnt == entt::null || !registry.get().valid(selectedEnt)) {
                        // Centered Empty State matching Image 4
                        float availH = ImGui::GetContentRegionAvail().y;
                        ImGui::Dummy(ImVec2(0, std::max(12.0f, availH * 0.15f)));

                        float winW = ImGui::GetWindowWidth();
                        ImVec2 curScreen = ImGui::GetCursorScreenPos();
                        ImDrawList* ddl = ImGui::GetWindowDrawList();
                        float cubeCx = curScreen.x + winW * 0.5f;
                        float cubeCy = curScreen.y + 45.0f * s_uiScale;
                        float r = 36.0f * s_uiScale;

                        // Isometric 3D wireframe cube illustration matching Image 4
                        ImVec2 vTop(cubeCx, cubeCy - r);
                        ImVec2 vTR(cubeCx + r * 0.866f, cubeCy - r * 0.5f);
                        ImVec2 vBR(cubeCx + r * 0.866f, cubeCy + r * 0.5f);
                        ImVec2 vBot(cubeCx, cubeCy + r);
                        ImVec2 vBL(cubeCx - r * 0.866f, cubeCy + r * 0.5f);
                        ImVec2 vTL(cubeCx - r * 0.866f, cubeCy - r * 0.5f);
                        ImVec2 vCen(cubeCx, cubeCy);

                        ImU32 cubeCol = IM_COL32(0, 180, 255, 230);
                        ImU32 innerCol = IM_COL32(0, 140, 220, 160);
                        float th = 1.8f * s_uiScale;

                        // Outer hexagon
                        ddl->AddLine(vTop, vTR, cubeCol, th);
                        ddl->AddLine(vTR, vBR, cubeCol, th);
                        ddl->AddLine(vBR, vBot, cubeCol, th);
                        ddl->AddLine(vBot, vBL, cubeCol, th);
                        ddl->AddLine(vBL, vTL, cubeCol, th);
                        ddl->AddLine(vTL, vTop, cubeCol, th);
                        // Inner Y-edges
                        ddl->AddLine(vCen, vTop, innerCol, th);
                        ddl->AddLine(vCen, vBR, innerCol, th);
                        ddl->AddLine(vCen, vBL, innerCol, th);

                        ImGui::Dummy(ImVec2(winW, 95.0f * s_uiScale));

                        const char* emptyTitle = "Brak zaznaczonego aktora";
                        float tw = ImGui::CalcTextSize(emptyTitle).x;
                        ImGui::SetCursorPosX((winW - tw) * 0.5f);
                        ImGui::TextColored(ImVec4(0.96f, 0.97f, 1.0f, 1.0f), "%s", emptyTitle);

                        ImGui::Spacing();
                        ImGui::PushTextWrapPos(winW - 20.0f * s_uiScale);
                        ImGui::SetCursorPosX(20.0f * s_uiScale);
                        ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.68f, 1.0f),
                            "Kliknij dowolny obiekt w Viewporcie lub wybierz go z Outlinera, aby edytowac parametry: transform, Jolt Physics, Materialy i Skrypty.");
                        ImGui::PopTextWrapPos();
                    } else {
                        std::string tagStr = "Actor";
                        if (registry.get().all_of<ecs::TagComponent>(selectedEnt)) {
                            tagStr = registry.get().get<ecs::TagComponent>(selectedEnt).name;
                        }

                        // Actor Header Bar
                        ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "%s", tagStr.c_str());
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.22f, 0.85f, 1.0f, 1.0f), "#%u", static_cast<uint32_t>(selectedEnt));
                        ImGui::SameLine(ImGui::GetWindowWidth() - 65.0f * s_uiScale);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.04f, 0.28f, 0.18f, 0.9f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.98f, 0.68f, 1.0f));
                        ImGui::Button("+ Add", ImVec2(55.0f * s_uiScale, 20.0f * s_uiScale));
                        ImGui::PopStyleColor(2);

                        // Component hierarchy card matching Image 2
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.075f, 0.11f, 1.0f));
                        ImGui::BeginChild("ComponentHeaderCard", ImVec2(0, 24.0f * s_uiScale), true, ImGuiWindowFlags_NoScrollbar);
                        drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(ImGui::GetWindowPos().x + 12.0f * s_uiScale, ImGui::GetWindowPos().y + 12.0f * s_uiScale), 4.5f * s_uiScale, VectorIconType::Cube, IM_COL32(56, 189, 248, 255));
                        ImGui::SetCursorPosX(22.0f * s_uiScale);
                        ImGui::TextColored(ImVec4(0.85f, 0.88f, 0.96f, 1.0f), "StaticMeshComponent (StaticMeshComponent0)");
                        ImGui::SameLine(ImGui::GetWindowWidth() - 75.0f * s_uiScale);
                        ImGui::TextDisabled("Edit in C++");
                        ImGui::EndChild();
                        ImGui::PopStyleColor();

                        ImGui::Separator();

                        if (registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                            auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                                bool pCh = drawVec3Control("Location", t.position, 0.0f);
                                bool rCh = drawVec3Control("Rotation", t.rotation, 0.0f);
                                bool sCh = drawVec3Control("Scale", t.scale, 1.0f);
                                if (rCh) t.orientation = glm::quat(glm::radians(t.rotation));
                                if ((pCh || rCh || sCh) && engine) engine->syncEntityTransformPhysics(selectedEnt);
                            }
                        }

                        if (registry.get().all_of<ecs::LightComponent>(selectedEnt)) {
                            auto& lc = registry.get().get<ecs::LightComponent>(selectedEnt);
                            if (ImGui::CollapsingHeader("Light Component (Oswietlenie PBR)", ImGuiTreeNodeFlags_DefaultOpen)) {
                                const char* lightTypes[] = { "point", "sun", "spot", "area" };
                                int curType = 0;
                                if (lc.type == "directional" || lc.type == "sun") curType = 1;
                                else if (lc.type == "spot") curType = 2;
                                else if (lc.type == "area") curType = 3;
                                else curType = 0;

                                if (ImGui::Combo("Light Type (Typ)", &curType, "Point Light (Punktowe)\0Sun Light (Slonce)\0Spot Light (Reflektor)\0Area Light (Powierzchniowe)\0\0")) {
                                    lc.type = lightTypes[curType];
                                }
                                ImGui::ColorEdit3("Light Color", (float*)&lc.color);
                                ImGui::DragFloat("Intensity / Power", &lc.intensity, 0.1f, 0.0f, 100.0f, "%.2f");

                                if (curType == 0) { // Point
                                    ImGui::DragFloat("Attenuation Radius (m)", &lc.range, 0.5f, 1.0f, 200.0f, "%.1f m");
                                } else if (curType == 1) { // Sun
                                    if (registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                                        auto& lt = registry.get().get<ecs::TransformComponent>(selectedEnt);
                                        bool changed = false;
                                        if (ImGui::DragFloat("Sun Altitude / Height (Y)", &lt.position.y, 0.2f, 1.0f, 300.0f, "%.1f m")) changed = true;
                                        if (ImGui::SliderFloat("Sun Elevation (Pitch)", &lt.rotation.x, -85.0f, 85.0f, "%.1f deg")) changed = true;
                                        if (ImGui::SliderFloat("Sun Azimuth (Yaw)", &lt.rotation.y, 0.0f, 360.0f, "%.1f deg")) changed = true;
                                        if (changed) {
                                            lt.orientation = glm::quat(glm::radians(lt.rotation));
                                            if (engine) engine->syncEntityTransformPhysics(selectedEnt);
                                        }
                                    }
                                } else if (curType == 2) { // Spot
                                    ImGui::DragFloat("Attenuation Radius (m)", &lc.range, 0.5f, 1.0f, 200.0f, "%.1f m");
                                    ImGui::SliderFloat("Inner Cone Angle", &lc.innerConeAngle, 1.0f, 80.0f, "%.1f deg");
                                    if (lc.outerConeAngle <= lc.innerConeAngle) lc.outerConeAngle = lc.innerConeAngle + 1.0f;
                                    ImGui::SliderFloat("Outer Cone Angle", &lc.outerConeAngle, lc.innerConeAngle + 0.5f, 89.0f, "%.1f deg");
                                    if (registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                                        auto& lt = registry.get().get<ecs::TransformComponent>(selectedEnt);
                                        bool changed = false;
                                        if (ImGui::SliderFloat("Spot Pitch", &lt.rotation.x, -89.0f, 89.0f, "%.1f deg")) changed = true;
                                        if (ImGui::SliderFloat("Spot Yaw", &lt.rotation.y, 0.0f, 360.0f, "%.1f deg")) changed = true;
                                        if (changed) {
                                            lt.orientation = glm::quat(glm::radians(lt.rotation));
                                            if (engine) engine->syncEntityTransformPhysics(selectedEnt);
                                        }
                                    }
                                } else if (curType == 3) { // Area
                                    ImGui::DragFloat("Attenuation Radius (m)", &lc.range, 0.5f, 1.0f, 200.0f, "%.1f m");
                                    ImGui::SliderFloat("Area Width X", &lc.areaSizeX, 0.2f, 20.0f, "%.2f m");
                                    ImGui::SliderFloat("Area Height Y", &lc.areaSizeY, 0.2f, 20.0f, "%.2f m");
                                    if (registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                                        auto& lt = registry.get().get<ecs::TransformComponent>(selectedEnt);
                                        bool changed = false;
                                        if (ImGui::SliderFloat("Area Pitch", &lt.rotation.x, -89.0f, 89.0f, "%.1f deg")) changed = true;
                                        if (ImGui::SliderFloat("Area Yaw", &lt.rotation.y, 0.0f, 360.0f, "%.1f deg")) changed = true;
                                        if (changed) {
                                            lt.orientation = glm::quat(glm::radians(lt.rotation));
                                            if (engine) engine->syncEntityTransformPhysics(selectedEnt);
                                        }
                                    }
                                }
                            }
                        }

                        if (registry.get().all_of<ecs::MeshComponent>(selectedEnt)) {
                            auto& mc = registry.get().get<ecs::MeshComponent>(selectedEnt);
                            if (ImGui::CollapsingHeader("Static Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
                                const char* meshes[] = { "None", "Plane (Ground)", "Cube (1x1x1)", "Sphere (R=0.5)", "Cylinder", "Capsule" };
                                int curMesh = std::clamp(static_cast<int>(mc.meshId), 0, 5);
                                if (ImGui::Combo("Mesh Asset", &curMesh, meshes, IM_ARRAYSIZE(meshes))) {
                                    mc.meshId = static_cast<uint32_t>(curMesh);
                                }
                            }
                        }

                        if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::TextDisabled("Slot 0: [M_LucidPBR]");
                            static ImVec4 albedoColor(0.2f, 0.7f, 0.9f, 1.0f);
                            ImGui::ColorEdit3("Albedo", (float*)&albedoColor, ImGuiColorEditFlags_NoInputs);
                            static float roughness = 0.35f;
                            ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f);
                            static float metallic = 0.10f;
                            ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f);
                        }

                        if (registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt)) {
                            auto& rb = registry.get().get<ecs::RigidBodyComponent>(selectedEnt);
                            if (ImGui::CollapsingHeader("Jolt Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
                                ImGui::DragFloat("Mass (kg)", &rb.mass, 0.1f, 0.0f, 500.0f);
                                ImGui::SliderFloat("Restitution", &rb.restitution, 0.0f, 1.0f);
                                ImGui::SliderFloat("Friction", &rb.friction, 0.0f, 1.0f);
                                ImGui::Checkbox("Is Sensor (Trigger)", &rb.isSensor);
                            }
                        }

                        ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.16f, 0.20f, 1.0f));
                        if (ImGui::Button(" Delete Actor ")) {
                            registry.get().destroy(selectedEnt);
                            selectedEnt = entt::null;
                            if (engine) engine->setSelectedEntity(entt::null);
                        }
                        ImGui::PopStyleColor();
                    }
                } else {
                    // World Settings Tab
                    static float s_gravity = -9.81f;

                    if (ImGui::CollapsingHeader("Physics & World Dynamics", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (ImGui::DragFloat("Gravity (Y)", &s_gravity, 0.1f, -50.0f, 50.0f, "%.3f m/s2")) {
                            if (engine) engine->setGravity(s_gravity);
                        }
                        ImGui::TextDisabled("Physics Engine: Jolt Physics 60 Hz");
                    }

                    if (ImGui::CollapsingHeader("Environment & Realistic HDRI (IBL)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        const char* hdriItems[] = {
                            "0: Sunset (Zachod slonca - Cieple niebo)",
                            "1: Studio (Neutralne oswietlenie studyjne)",
                            "2: City (Miejska panorama)",
                            "3: Courtyard (Dziedziniec architektoniczny)",
                            "4: Forest (Lesne otoczenie / zielen)",
                            "5: Interior (Wnetrze architektoniczne)",
                            "6: Night (Nocne niebo / gwiazdy)",
                            "7: Sunrise (Wschod slonca / swita)"
                        };
                        const char* hdriPaths[] = {
                            "engine/assets/textures/sunset.exr",
                            "engine/assets/textures/studio.exr",
                            "engine/assets/textures/city.exr",
                            "engine/assets/textures/courtyard.exr",
                            "engine/assets/textures/forest.exr",
                            "engine/assets/textures/interior.exr",
                            "engine/assets/textures/night.exr",
                            "engine/assets/textures/sunrise.exr"
                        };

                        static int currentHdri = 0;
                        if (ImGui::Combo("Predefiniowane HDRI", &currentHdri, hdriItems, IM_ARRAYSIZE(hdriItems))) {
                            loadEnvironmentHdri(currentHdri);
                        }
                        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "Image-Based Lighting (IBL): Aktywne (Specular + Irradiance)");

                        if (engine) {
                            auto env = engine->getEnvironmentSettings();
                            bool changed = false;
                            if (ImGui::ColorEdit3("Sun Color", (float*)&env.sunColor)) changed = true;
                            if (ImGui::DragFloat("Sun Intensity", &env.sunIntensity, 0.1f, 0.0f, 20.0f, "%.2f")) changed = true;
                            if (ImGui::ColorEdit3("Sky Ambient Color", (float*)&env.ambientColor)) changed = true;
                            if (ImGui::DragFloat("Ambient / IBL Intensity", &env.ambientIntensity, 0.05f, 0.0f, 5.0f, "%.2f")) changed = true;
                            if (changed) engine->setEnvironmentSettings(env);
                        }
                    }

                    if (ImGui::CollapsingHeader("HDR & Post-Processing (Bloom / ACES)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Bloom Active (Dual Kawase)", &m_bloomEnabled);
                        if (m_bloomEnabled) {
                            ImGui::SliderFloat("Bloom Intensity", &m_bloomIntensity, 0.0f, 3.0f, "%.2f");
                            ImGui::SliderFloat("Bloom Threshold", &m_bloomThreshold, 0.5f, 5.0f, "%.2f");
                        }
                        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Tone Mapping: ACES Filmic (Unreal Engine 5 standard)");
                        ImGui::TextDisabled("Render Target: 64-bit HDR (RGBA16F) -> ACES -> 8-bit SDR sRGB");
                    }

                    if (ImGui::CollapsingHeader("Ground Truth Ambient Occlusion (GTAO)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("GTAO Active (Horizon-Based)", &m_gtaoEnabled);
                        if (m_gtaoEnabled) {
                            ImGui::SliderFloat("AO Radius", &m_gtaoRadius, 0.1f, 3.0f, "%.2f m");
                            ImGui::SliderFloat("AO Intensity", &m_gtaoIntensity, 0.2f, 4.0f, "%.2f");
                        }
                        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "Contact Shadows: Activision / Sledgehammer GTAO");
                        ImGui::TextDisabled("Screen-space depth horizon integration + Bilateral depth-edge filter.");
                    }

                    if (ImGui::CollapsingHeader("Anti-Aliasing (NVIDIA FXAA 3.11)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        const char* aaModes[] = {
                            "0: Off (Brak AA)",
                            "1: FXAA Fast (Szybki)",
                            "2: FXAA Quality (Subpixel - Aktywny)",
                            "3: FXAA Ultra (Maksymalne wygladzanie)"
                        };
                        int curAA = m_antiAliasingMode;
                        if (ImGui::Combo("AA Method", &curAA, aaModes, IM_ARRAYSIZE(aaModes))) {
                            m_antiAliasingMode = curAA;
                        }
                        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "FXAA 3.11: Aktywny w calym Viewporcie w czasie rzeczywistym.");
                        ImGui::TextDisabled("Wygladza krawedzie geometrii, cieni i modeli z subpikselowym filtrem.");
                    }

                    if (ImGui::CollapsingHeader("Shadows & Quality (Jakosc Cieni)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::SliderFloat("Shadow Distance (m)", &m_shadowDistance, 20.0f, 150.0f, "%.1f m");
                        ImGui::TextDisabled("Cascaded Shadow Maps: 4 Splits (Directional Sun)");
                        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "PCF 16-Tap Poisson Disk Filter: Active");
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Point Light Shadows: Omnidirectional Depth Cubemap (512x512 x 6 faces)");
                    }

                    if (ImGui::CollapsingHeader("Anti-Aliasing (Wygładzanie Krawędzi)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        const char* aaModes[] = { "0: Off (Brak AA)", "1: FXAA Fast (Wydajny)", "2: FXAA Quality (Subpixel)", "3: TAA / Temporal Edge AA" };
                        int curAA = m_antiAliasingMode;
                        if (ImGui::Combo("AA Method", &curAA, aaModes, IM_ARRAYSIZE(aaModes))) {
                            m_antiAliasingMode = curAA;
                        }
                        ImGui::TextDisabled("Filtr wygladzajacy krawedzie geometrii i cieni.");
                    }

                    if (ImGui::CollapsingHeader("Viewport & Scaling", ImGuiTreeNodeFlags_DefaultOpen)) {
                        float resSc = m_resolutionScale;
                        if (ImGui::SliderFloat("Resolution Scale", &resSc, 0.5f, 2.0f, "%.2fx")) {
                            setResolutionScale(resSc);
                        }
                        float uiSc = m_uiScale;
                        if (ImGui::SliderFloat("UI Scale", &uiSc, 0.75f, 1.75f, "%.2fx")) {
                            setUiScale(uiSc);
                        }
                    }
                }

                ImGui::End();
                ImGui::PopStyleColor();
            }

            // ══════════════════════════════════════════════════════════════════════
            // 8. EXPANDABLE BOTTOM DRAWERS (Content Drawer / Output Log)
            // ══════════════════════════════════════════════════════════════════════
            if (!s_immersivePlay && (s_showContentDrawer || s_showOutputLog)) {
                ImGui::SetNextWindowPos(ImVec2(0, screenH - bottomTotalH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(screenW, drawerH), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.043f, 0.067f, 1.0f));
                ImGui::Begin("BottomDrawerWindow", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

                if (s_showContentDrawer) {
                    ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.08f, 1.0f), "Content Drawer");
                    ImGui::SameLine(screenW - 36.0f * s_uiScale);
                    if (ImGui::Button(" x ##CloseCD")) s_showContentDrawer = false;
                    ImGui::Separator();
                    const char* assets[] = { "BP_Player", "BP_Coin", "M_CyanPBR", "M_SlateFloor", "VFX_Fire", "Lvl_MainLevel" };
                    for (const char* a : assets) {
                        ImGui::Button(a, ImVec2(115.0f * s_uiScale, 64.0f * s_uiScale));
                        ImGui::SameLine();
                    }
                } else if (s_showOutputLog) {
                    ImGui::TextColored(ImVec4(0.22f, 0.85f, 1.0f, 1.0f), ">_ Output Log");
                    ImGui::SameLine(screenW - 36.0f * s_uiScale);
                    if (ImGui::Button(" x ##CloseLog")) s_showOutputLog = false;
                    ImGui::Separator();
                    ImGui::BeginChild("OutputLogScroll", ImVec2(0, 0), false);
                    for (const auto& line : core::Logger::getRecentLogs(40)) {
                        ImGui::TextUnformatted(line.c_str());
                    }
                    ImGui::EndChild();
                }

                ImGui::End();
                ImGui::PopStyleColor();
            }

            // ══════════════════════════════════════════════════════════════════════
            // 9. BOTTOM STATUS BAR (Matches App.tsx UE5 Bottom Dock)
            // ══════════════════════════════════════════════════════════════════════
            if (!s_immersivePlay) {
                ImGui::SetNextWindowPos(ImVec2(0, screenH - bottomStatusH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(screenW, bottomStatusH), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 3));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.055f, 0.055f, 1.0f));
                ImGui::Begin("BottomStatusRail", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);

                if (ImGui::Button(" Content Drawer ")) {
                    s_showContentDrawer = !s_showContentDrawer;
                    if (s_showContentDrawer) s_showOutputLog = false;
                }
                ImGui::SameLine();
                if (ImGui::Button(" >_ Output Log ")) {
                    s_showOutputLog = !s_showOutputLog;
                    if (s_showOutputLog) s_showContentDrawer = false;
                }

                float cmdW = std::min(420.0f * s_uiScale, screenW * 0.34f);
                float cmdOffset = screenW * 0.34f;
                if (ImGui::GetCursorPosX() < cmdOffset) ImGui::SameLine(cmdOffset);

                ImGui::TextDisabled("Cmd v");
                ImGui::SameLine();
                static char cmdInput[256] = "";
                ImGui::SetNextItemWidth(cmdW);
                if (ImGui::InputTextWithHint("##ConsoleCmdNative", "Enter Console Command...", cmdInput, sizeof(cmdInput), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    std::string c = cmdInput;
                    cmdInput[0] = '\0';
                    if (engine && !c.empty()) {
                        if (c == "play") engine->playSimulation();
                        else if (c == "pause") engine->setPaused(true);
                        else if (c == "stop") engine->stopSimulation();
                        else if (c == "reset") engine->resetScene(engine->getCurrentPreset());
                    }
                }

                float rightInfoX = screenW - 365.0f * s_uiScale;
                if (ImGui::GetCursorPosX() < rightInfoX) ImGui::SameLine(rightInfoX);
                ImGui::TextDisabled("Diagnostics  •");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.20f, 0.90f, 0.55f, 1.0f), "Zen Server");
                ImGui::SameLine();
                ImGui::TextDisabled("•  All Saved  •");
                ImGui::SameLine();
                if (ImGui::SmallButton(" - ")) { m_uiScale = std::clamp(m_uiScale - 0.10f, 0.60f, 1.60f); }
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.55f, 0.62f, 0.98f, 1.0f), "UI: %.0f%%", s_uiScale * 100.0f);
                ImGui::SameLine();
                if (ImGui::SmallButton(" + ")) { m_uiScale = std::clamp(m_uiScale + 0.10f, 0.60f, 1.60f); }
                ImGui::SameLine();
                if (ImGui::SmallButton("100%")) { m_uiScale = 1.0f; }
                ImGui::SameLine();
                ImGui::TextDisabled("•");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.22f, 0.85f, 1.0f, 1.0f), "Vulkan: %.0f FPS", ImGui::GetIO().Framerate);

                ImGui::End();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }

            // Floating QuickJS Script Studio
            if (s_showScriptStudio) {
                ImGui::SetNextWindowSize(ImVec2(720 * s_uiScale, 520 * s_uiScale), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("Blueprints / QuickJS Script Studio", &s_showScriptStudio)) {
                    static char scriptBuf[16384] = "";
                    static bool s_scriptInit = false;
                    if (engine && (!s_scriptInit || scriptBuf[0] == '\0')) {
                        s_scriptInit = true;
                        std::string code = engine->getCurrentScript();
                        std::strncpy(scriptBuf, code.c_str(), sizeof(scriptBuf) - 1);
                        scriptBuf[sizeof(scriptBuf) - 1] = '\0';
                    }

                    // Template Buttons
                    ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "Szablony Logiki:");
                    ImGui::SameLine();
                    if (ImGui::Button("Gracz (WASD+Skok)")) {
                        std::string c = engine->getDefaultScript("platformer");
                        std::strncpy(scriptBuf, c.c_str(), sizeof(scriptBuf) - 1);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Pojazd Hover")) {
                        std::string c = engine->getDefaultScript("racer");
                        std::strncpy(scriptBuf, c.c_str(), sizeof(scriptBuf) - 1);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Strzelanka")) {
                        std::string c = engine->getDefaultScript("shooter");
                        std::strncpy(scriptBuf, c.c_str(), sizeof(scriptBuf) - 1);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Breakout")) {
                        std::string c = engine->getDefaultScript("breakout");
                        std::strncpy(scriptBuf, c.c_str(), sizeof(scriptBuf) - 1);
                    }

                    ImGui::Separator();
                    if (ImGui::Button("  ▶ Zastosuj i Zapisz do Projektu  ", ImVec2(280.0f * s_uiScale, 28.0f * s_uiScale)) && engine) {
                        bool res = engine->loadScript(scriptBuf);
                        engine->setCurrentScript(scriptBuf);
                        if (res) {
                            snprintf(s_engineNotification, sizeof(s_engineNotification), "Skrypt wstrzykniety pomyslnie i aktywny!");
                            s_engineNotificationTimer = 3.0f;
                        } else {
                            snprintf(s_engineNotification, sizeof(s_engineNotification), "Blad wykonania skryptu!");
                            s_engineNotificationTimer = 3.0f;
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "● QuickJS ESM Active");

                    ImGui::InputTextMultiline("##ScriptCode", scriptBuf, sizeof(scriptBuf), ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput);
                }
                ImGui::End();
            }

            // Notification toast pill
            if (s_engineNotificationTimer > 0.0f) {
                s_engineNotificationTimer -= ImGui::GetIO().DeltaTime;
                float notifW = std::min(480.0f * s_uiScale, screenW * 0.75f);
                ImGui::SetNextWindowPos(ImVec2((screenW - notifW) * 0.5f, topTotalH + 12.0f * s_uiScale), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.26f, 0.18f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.85f, 0.52f, 0.90f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * s_uiScale, 8.0f * s_uiScale));
                if (ImGui::Begin("EngineNotification", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
                    ImGui::TextColored(ImVec4(0.95f, 1.0f, 0.95f, 1.0f), "%s", s_engineNotification);
                }
                ImGui::End();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(2);
            }
        }

        

        ImGui::Render();

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = targetView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = m_standaloneGameMode ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = { {0.027f, 0.027f, 0.047f, 1.0f} };

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