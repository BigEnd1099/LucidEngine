#include "renderer/VulkanRenderer.hpp"
#include "renderer/VulkanRendererInternal.hpp"
#include "core/Logger.hpp"
#include "core/Profiler.hpp"
#include "renderer/Vertex.hpp"
#include "renderer/SpatialAnnotation.hpp"
#include "core/AssetManager.hpp"
#include "core/Engine.hpp"
#include "utils/FileIO.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stdexcept>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <iostream>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "renderer/stb_image_write.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef USE_GLFW
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#endif

namespace engine::renderer {

    uint32_t g_pendingRenderWidth = 0;
    uint32_t g_pendingRenderHeight = 0;
    int g_pendingHdriIndex = -1;

    VulkanRenderer::VulkanRenderer(VulkanContext* context, uint32_t width, uint32_t height)
        : m_context(context), m_width(width), m_height(height), m_baseWidth(width), m_baseHeight(height) {
        m_accelerationStructure.init(m_context);
        createOffscreenTarget();
        createCommandBuffer();

        // Enable GPU timing only when the selected graphics queue supports timestamps.
        if (m_context->getTimestampValidBits() > 0 &&
            m_context->getPhysicalDeviceProperties().limits.timestampPeriod > 0.0f) {
            VkQueryPoolCreateInfo queryInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
            queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queryInfo.queryCount = 11;
            if (vkCreateQueryPool(m_context->getDevice(), &queryInfo, nullptr, &m_timestampQueryPool) != VK_SUCCESS) {
                m_timestampQueryPool = VK_NULL_HANDLE;
                core::Logger::warn("GPU timestamp query pool could not be created; GPU timing is disabled.");
            }
        }
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
        m_worldRadianceCache.init(m_context, m_descriptorPool, m_descriptorSetLayout);
        m_gpuCulling.init(m_context, m_descriptorPool, m_width, m_height);
        initRtDenoiser();
        m_rtDenoiser.setWorldRadianceCacheBuffer(m_worldRadianceCache.getCellBuffer());
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
        {
            VkFenceCreateInfo headlessFenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            headlessFenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            if (vkCreateFence(m_context->getDevice(), &headlessFenceInfo, nullptr, &m_headlessInFlightFence) != VK_SUCCESS) {
                throw std::runtime_error("failed to create headless frame fence!");
            }
        }
    }

    VulkanRenderer::~VulkanRenderer() {
        if (m_context && m_context->getDevice()) {
            VkDevice dev = m_context->getDevice();
            vkDeviceWaitIdle(dev);
            if (m_timestampQueryPool) {
                vkDestroyQueryPool(dev, m_timestampQueryPool, nullptr);
                m_timestampQueryPool = VK_NULL_HANDLE;
            }
            for (auto& entry : m_deformedVertexCache) {
                auto& cached = entry.second;
                if (cached.buffer) vmaDestroyBuffer(m_context->getAllocator(), cached.buffer, cached.allocation);
            }
            m_deformedVertexCache.clear();

            if (m_headlessInFlightFence) {
                vkDestroyFence(dev, m_headlessInFlightFence, nullptr);
                m_headlessInFlightFence = VK_NULL_HANDLE;
            }

            m_rtDenoiser.cleanup();
            m_worldRadianceCache.cleanup();
            m_gpuCulling.cleanup();
            m_accelerationStructure.cleanup();
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
            if (m_gBufferPipeline) {
                vkDestroyPipeline(dev, m_gBufferPipeline, nullptr);
                m_gBufferPipeline = VK_NULL_HANDLE;
            }
            if (m_graphicsPipeline) {
                vkDestroyPipeline(dev, m_graphicsPipeline, nullptr);
                m_graphicsPipeline = VK_NULL_HANDLE;
            }
            if (m_rtGraphicsPipeline) {
                vkDestroyPipeline(dev, m_rtGraphicsPipeline, nullptr);
                m_rtGraphicsPipeline = VK_NULL_HANDLE;
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
        // History buffers are resolution-dependent; never reproject across a resize.
        m_hasPreviousViewProj = false;
        m_prevViewProj = glm::mat4(1.0f);

        createOffscreenTarget();
        updatePostProcessDescriptorSets();
        if (m_rtDenoiser.isReady()) {
            m_rtDenoiser.resize(m_width, m_height, m_depthImageView,
                                m_depthSampler ? m_depthSampler : m_linearSampler,
                                m_gBufferNormalRoughnessImageView,
                                m_gBufferAlbedoMetallicImageView,
                                m_gBufferMotionImageView);
            refreshRtIndirectDescriptors();
        }
        m_gpuCulling.resize(m_width, m_height);
        initCaptureResources();

        core::Logger::info("Render resolution updated to " + std::to_string(m_width) + "x" + std::to_string(m_height));
    }

    void VulkanRenderer::requestResolution(uint32_t width, uint32_t height) {
        width  = std::clamp(width,  64u, 7680u);
        height = std::clamp(height, 64u, 4320u);
        if (m_width == width && m_height == height) return;
        g_pendingRenderWidth = width;
        g_pendingRenderHeight = height;
    }

    void VulkanRenderer::invalidateRtHistory() {
        m_hasPreviousViewProj = false;
        m_prevViewProj = glm::mat4(1.0f);
        if (m_rtDenoiser.isReady()) {
            m_rtDenoiser.invalidateHistory();
        }
    }

    void VulkanRenderer::setResolutionScale(float scale) {
        m_resolutionScale = std::clamp(scale, 0.25f, 2.5f);
        uint32_t targetW = (static_cast<uint32_t>(static_cast<float>(m_baseWidth)  * m_resolutionScale) + 1u) & ~1u;
        uint32_t targetH = (static_cast<uint32_t>(static_cast<float>(m_baseHeight) * m_resolutionScale) + 1u) & ~1u;
        if (m_context && m_context->isHeadless()) {
            setRenderResolution(targetW, targetH);
        } else {
            requestResolution(targetW, targetH);   // zastosowane w bezpiecznym miejscu w render()
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

    void VulkanRenderer::createComputePipelines() {}

    void VulkanRenderer::initCaptureResources() {
        cleanupCaptureResources();
        VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = static_cast<VkDeviceSize>(m_width) * m_height * 4u;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        for (uint32_t i = 0; i < CAPTURE_RING_COUNT; ++i) {
            VmaAllocationInfo stagingAllocInfo{};
            if (vmaCreateBuffer(m_context->getAllocator(), &bufferInfo, &allocInfo, &m_captureStagingRing[i], &m_captureStagingAllocRing[i], &stagingAllocInfo) == VK_SUCCESS) {
                m_captureMappedPtrRing[i] = stagingAllocInfo.pMappedData;
            }
            VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr, &m_captureFenceRing[i]);
        }
        m_captureStagingBuffer = m_captureStagingRing[0];
        m_captureStagingAlloc = m_captureStagingAllocRing[0];
        m_captureMappedPtr = m_captureMappedPtrRing[0];

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
        for (uint32_t i = 0; i < CAPTURE_RING_COUNT; ++i) {
            if (m_captureFenceRing[i] != VK_NULL_HANDLE && m_context->getDevice()) {
                vkDestroyFence(m_context->getDevice(), m_captureFenceRing[i], nullptr);
                m_captureFenceRing[i] = VK_NULL_HANDLE;
            }
            if (m_captureStagingRing[i] != VK_NULL_HANDLE && m_context->getAllocator()) {
                vmaDestroyBuffer(m_context->getAllocator(), m_captureStagingRing[i], m_captureStagingAllocRing[i]);
                m_captureStagingRing[i] = VK_NULL_HANDLE;
                m_captureStagingAllocRing[i] = VK_NULL_HANDLE;
                m_captureMappedPtrRing[i] = nullptr;
            }
        }
        m_captureStagingBuffer = VK_NULL_HANDLE;
        m_captureStagingAlloc = VK_NULL_HANDLE;
        m_captureMappedPtr = nullptr;
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
        nlohmann::json dummyMetadata;
        return captureAovBase64("FinalComposite", dummyMetadata);
    }

    std::string VulkanRenderer::captureAovBase64(const std::string& aovName, nlohmann::json& outMetadata) {
        if (m_width == 0 || m_height == 0) return "";
        if (!m_captureStagingRing[0] || !m_captureMappedPtrRing[0] || !m_captureCommandBuffer) {
            initCaptureResources();
            if (!m_captureStagingRing[0] || !m_captureMappedPtrRing[0] || !m_captureCommandBuffer) return "";
        }

        // Fill telemetry and spatial metadata
        outMetadata["aov"] = aovName.empty() ? "FinalComposite" : aovName;
        outMetadata["width"] = m_width;
        outMetadata["height"] = m_height;
        outMetadata["frameCounter"] = m_frameCounter;

        if (m_sceneUboMappedPtr) {
            const auto* sceneUbo = static_cast<const SceneUboData*>(m_sceneUboMappedPtr);
            outMetadata["camera"] = {
                {"position", {sceneUbo->cameraPos.x, sceneUbo->cameraPos.y, sceneUbo->cameraPos.z}},
                {"sunDirection", {sceneUbo->sunDirection.x, sceneUbo->sunDirection.y, sceneUbo->sunDirection.z}},
                {"viewProj", nlohmann::json::array()},
                {"view", nlohmann::json::array()}
            };
            const float* vp = glm::value_ptr(sceneUbo->viewProj);
            for (int i = 0; i < 16; ++i) outMetadata["camera"]["viewProj"].push_back(vp[i]);
            const float* v = glm::value_ptr(sceneUbo->view);
            for (int i = 0; i < 16; ++i) outMetadata["camera"]["view"].push_back(v[i]);
        }

        outMetadata["profiler"] = core::Profiler::get().getStatsJson();
        outMetadata["renderGraph"] = m_renderGraph.getPassTelemetryJson();
        outMetadata["annotations"] = SpatialAnnotationRegistry::get().toJson();

        // Select source image based on requested AOV
        VkImage srcImage = m_colorImage;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        uint32_t srcWidth = m_width;
        uint32_t srcHeight = m_height;

        if (aovName == "NormalRoughness" || aovName == "normal") {
            if (m_gBufferNormalRoughnessImage) srcImage = m_gBufferNormalRoughnessImage;
        } else if (aovName == "AlbedoMetallic" || aovName == "albedo") {
            if (m_gBufferAlbedoMetallicImage) srcImage = m_gBufferAlbedoMetallicImage;
        } else if (aovName == "GIIndirect" || aovName == "gi" || aovName == "ao") {
            if (m_aoBlurImage) srcImage = m_aoBlurImage;
            else if (m_aoImage) srcImage = m_aoImage;
        } else if (aovName == "ShadowCascadeDebug" || aovName == "shadow") {
            if (m_shadowImage) {
                srcImage = m_shadowImage;
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
                srcWidth = m_shadowWidth;
                srcHeight = m_shadowHeight;
            }
        }

        if (srcImage == VK_NULL_HANDLE) {
            srcImage = m_colorImage;
            aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            srcWidth = m_width;
            srcHeight = m_height;
        }
        if (srcImage == VK_NULL_HANDLE) return "";

        uint32_t currIdx = m_captureRingIndex;
        uint32_t prevIdx = (currIdx + 1) % CAPTURE_RING_COUNT;

        // Non-blocking check on previous ring slot: if ready, we encode immediately (< 0.5 ms)
        std::string resultBase64;
        VkResult prevStatus = vkGetFenceStatus(m_context->getDevice(), m_captureFenceRing[prevIdx]);
        if (prevStatus == VK_SUCCESS && m_captureMappedPtrRing[prevIdx]) {
            const uint8_t* srcPixels = static_cast<const uint8_t*>(m_captureMappedPtrRing[prevIdx]);
            std::vector<uint8_t> jpegBytes;
            jpegBytes.reserve(256 * 1024);
            stbi_write_jpg_to_func(writeJpegBuffer, &jpegBytes, static_cast<int>(m_width), static_cast<int>(m_height), 4, srcPixels, 82);
            resultBase64 = "data:image/jpeg;base64," + encodeBase64Data(jpegBytes.data(), jpegBytes.size());
        }

        // Wait with a small bounded timeout on curr fence if in flight, then record next copy
        vkWaitForFences(m_context->getDevice(), 1, &m_captureFenceRing[currIdx], VK_TRUE, 2000000ULL);
        vkResetFences(m_context->getDevice(), 1, &m_captureFenceRing[currIdx]);
        vkResetCommandBuffer(m_captureCommandBuffer, 0);

        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(m_captureCommandBuffer, &beginInfo);

        transitionImage(m_captureCommandBuffer, srcImage,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        aspect, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

        VkBufferImageCopy region{};
        region.imageSubresource = { aspect, 0, 0, 1 };
        region.imageExtent = { srcWidth, srcHeight, 1 };
        vkCmdCopyImageToBuffer(m_captureCommandBuffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_captureStagingRing[currIdx], 1, &region);

        transitionImage(m_captureCommandBuffer, srcImage,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        aspect, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        vkEndCommandBuffer(m_captureCommandBuffer);

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_captureCommandBuffer;
        vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, m_captureFenceRing[currIdx]);

        m_captureRingIndex = prevIdx;

        // If result was not ready from previous ring slot (e.g. bootstrap/first call), wait on curr and return
        if (resultBase64.empty()) {
            vkWaitForFences(m_context->getDevice(), 1, &m_captureFenceRing[currIdx], VK_TRUE, UINT64_MAX);
            const uint8_t* srcPixels = static_cast<const uint8_t*>(m_captureMappedPtrRing[currIdx]);
            std::vector<uint8_t> jpegBytes;
            jpegBytes.reserve(256 * 1024);
            stbi_write_jpg_to_func(writeJpegBuffer, &jpegBytes, static_cast<int>(m_width), static_cast<int>(m_height), 4, srcPixels, 82);
            resultBase64 = "data:image/jpeg;base64," + encodeBase64Data(jpegBytes.data(), jpegBytes.size());
        }

        return resultBase64;
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
