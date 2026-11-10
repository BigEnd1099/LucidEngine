#include "core/AssetManager.hpp"
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <limits>
#include <functional>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include "core/Logger.hpp"
#include "utils/FileIO.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "renderer/stb_image.h"

namespace engine::core {
    AssetManager::AssetManager(renderer::VulkanContext* context) : m_context(context) {
        createFallbackMeshes();}

    AssetManager::~AssetManager() {
        for (auto& tex : m_textures) {
            if (tex->imageView) vkDestroyImageView(m_context->getDevice(), tex->imageView, nullptr);
            if (tex->image) vmaDestroyImage(m_context->getAllocator(), tex->image, tex->allocation);
        }
        for (auto& mesh : m_meshes) {
            if (mesh->vertexBuffer) vmaDestroyBuffer(m_context->getAllocator(), mesh->vertexBuffer, mesh->vertexAllocation);
            if (mesh->indexBuffer) vmaDestroyBuffer(m_context->getAllocator(), mesh->indexBuffer, mesh->indexAllocation);
        }
    }

    TextureAsset* AssetManager::getTexture(uint32_t id) {
        for (auto& tex : m_textures) {
            if (tex->id == id) return tex.get();
        }
        return nullptr;
    }

    uint32_t AssetManager::loadGLTF(const std::string& path) {
        m_lastImportedInstances.clear();
        m_lastImportedNodes.clear();
        try {
            fastgltf::Parser parser;
            auto buffer = fastgltf::GltfDataBuffer::FromPath(path);
            if (!buffer) {
                core::Logger::error("AssetManager: unable to read glTF file: " + path);
                return 0;
            }

            const auto directory = std::filesystem::path(path).parent_path();
            const auto options = fastgltf::Options::DecomposeNodeMatrices |
                                 fastgltf::Options::LoadExternalBuffers |
                                 fastgltf::Options::LoadExternalImages |
                                 fastgltf::Options::GenerateMeshIndices;
            auto asset = parser.loadGltf(buffer.get(), directory, options);
            if (asset.error() != fastgltf::Error::None) {
                core::Logger::error("AssetManager: fastgltf failed to parse " + path);
                return 0;
            }
            return loadGLTFAsset(asset.get());
        } catch (const std::exception& e) {
            core::Logger::error("AssetManager::loadGLTF failed for " + path + ": " + e.what());
            return 0;
        } catch (...) {
            core::Logger::error("AssetManager::loadGLTF failed with an unknown exception: " + path);
            return 0;
        }
    }

    void AssetManager::uploadTexture(VkImage& dstImage, VmaAllocation& dstAlloc, VkImageView& dstView,
                                     const void* pixels, uint32_t width, uint32_t height, VkFormat format, bool generateMips) {
        if (!pixels || width == 0 || height == 0) return;

        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;
        const uint32_t mipLevels = generateMips
            ? (static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1u)
            : 1u;

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingAlloc = VK_NULL_HANDLE;
        VkBufferCreateInfo bufInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufInfo.size = imageSize;
        bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo allocCreateInfo{};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        if (vmaCreateBuffer(m_context->getAllocator(), &bufInfo, &allocCreateInfo, &stagingBuffer, &stagingAlloc, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create texture staging buffer");
        }

        void* mapped = nullptr;
        vmaMapMemory(m_context->getAllocator(), stagingAlloc, &mapped);
        std::memcpy(mapped, pixels, static_cast<size_t>(imageSize));
        vmaUnmapMemory(m_context->getAllocator(), stagingAlloc);

        VkImageCreateInfo imgInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
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
        if (vmaCreateImage(m_context->getAllocator(), &imgInfo, &imgAllocInfo, &dstImage, &dstAlloc, nullptr) != VK_SUCCESS) {
            vmaDestroyBuffer(m_context->getAllocator(), stagingBuffer, stagingAlloc);
            throw std::runtime_error("Failed to create destination texture image");
        }

        VkCommandBufferAllocateInfo cmdAllocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandPool = m_context->getCommandPool();
        cmdAllocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(m_context->getDevice(), &cmdAllocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        // Transition image to transfer dst
        VkImageMemoryBarrier2 barrierToDst{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrierToDst.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrierToDst.srcAccessMask = 0;
        barrierToDst.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrierToDst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrierToDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrierToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrierToDst.image = dstImage;
        barrierToDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };

        VkDependencyInfo depToDst{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        depToDst.imageMemoryBarrierCount = 1;
        depToDst.pImageMemoryBarriers = &barrierToDst;
        vkCmdPipelineBarrier2(cmd, &depToDst);

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
                VkImageMemoryBarrier2 barrierSrc{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                barrierSrc.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                barrierSrc.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                barrierSrc.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                barrierSrc.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                barrierSrc.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrierSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrierSrc.image = dstImage;
                barrierSrc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1 };

                VkDependencyInfo depSrc{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
                depSrc.imageMemoryBarrierCount = 1;
                depSrc.pImageMemoryBarriers = &barrierSrc;
                vkCmdPipelineBarrier2(cmd, &depSrc);

                VkImageBlit blit{};
                blit.srcOffsets[1] = { mipW, mipH, 1 };
                blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 };
                blit.dstOffsets[1] = { std::max(1, mipW / 2), std::max(1, mipH / 2), 1 };
                blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 };
                vkCmdBlitImage(cmd, dstImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

                VkImageMemoryBarrier2 barrierRead{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                barrierRead.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                barrierRead.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                barrierRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                barrierRead.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
                barrierRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrierRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrierRead.image = dstImage;
                barrierRead.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1 };

                VkDependencyInfo depRead{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
                depRead.imageMemoryBarrierCount = 1;
                depRead.pImageMemoryBarriers = &barrierRead;
                vkCmdPipelineBarrier2(cmd, &depRead);

                mipW = std::max(1, mipW / 2);
                mipH = std::max(1, mipH / 2);
            }

            VkImageMemoryBarrier2 barrierLast{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
            barrierLast.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrierLast.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrierLast.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrierLast.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            barrierLast.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrierLast.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrierLast.image = dstImage;
            barrierLast.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, mipLevels - 1, 1, 0, 1 };

            VkDependencyInfo depLast{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            depLast.imageMemoryBarrierCount = 1;
            depLast.pImageMemoryBarriers = &barrierLast;
            vkCmdPipelineBarrier2(cmd, &depLast);
        } else {
            VkImageMemoryBarrier2 barrierFinal{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
            barrierFinal.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrierFinal.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrierFinal.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrierFinal.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            barrierFinal.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrierFinal.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrierFinal.image = dstImage;
            barrierFinal.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

            VkDependencyInfo depFinal{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            depFinal.imageMemoryBarrierCount = 1;
            depFinal.pImageMemoryBarriers = &barrierFinal;
            vkCmdPipelineBarrier2(cmd, &depFinal);
        }

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        VkFence transferFence = VK_NULL_HANDLE;
        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr, &transferFence);
        vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, transferFence);
        vkWaitForFences(m_context->getDevice(), 1, &transferFence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(m_context->getDevice(), transferFence, nullptr);

        vkFreeCommandBuffers(m_context->getDevice(), m_context->getCommandPool(), 1, &cmd);
        vmaDestroyBuffer(m_context->getAllocator(), stagingBuffer, stagingAlloc);

        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = dstImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };
        vkCreateImageView(m_context->getDevice(), &viewInfo, nullptr, &dstView);
    }

    void AssetManager::uploadBuffer(VkBuffer& dstBuffer, VmaAllocation& dstAlloc, const void* data, size_t size, VkBufferUsageFlags usage) {
        VkBufferCreateInfo stagingBufferInfo{};
        stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingBufferInfo.size = size;
        stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        VkBuffer stagingBuffer;
        VmaAllocation stagingAllocation;
        if (vmaCreateBuffer(m_context->getAllocator(), &stagingBufferInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create staging buffer");
        }

        void* mappedData;
        vmaMapMemory(m_context->getAllocator(), stagingAllocation, &mappedData);
        memcpy(mappedData, data, size);
        vmaUnmapMemory(m_context->getAllocator(), stagingAllocation);

        VkBufferCreateInfo dstBufferInfo{};
        dstBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        dstBufferInfo.size = size;
        dstBufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (m_context->isRayTracingSupported() &&
            (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT))) {
            dstBufferInfo.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        }

        VmaAllocationCreateInfo dstAllocInfo{};
        dstAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateBuffer(m_context->getAllocator(), &dstBufferInfo, &dstAllocInfo, &dstBuffer, &dstAlloc, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create destination buffer");
        }

        // Temporary command buffer for copy
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_context->getCommandPool();
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(m_context->getDevice(), &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, stagingBuffer, dstBuffer, 1, &copyRegion);
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VkFence transferFence = VK_NULL_HANDLE;
        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr, &transferFence);
        vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, transferFence);
        vkWaitForFences(m_context->getDevice(), 1, &transferFence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(m_context->getDevice(), transferFence, nullptr);

        vkFreeCommandBuffers(m_context->getDevice(), m_context->getCommandPool(), 1, &commandBuffer);
        vmaDestroyBuffer(m_context->getAllocator(), stagingBuffer, stagingAllocation);
    }

    uint32_t AssetManager::loadGLTFFromMemory(const std::vector<uint8_t>& data) {
        m_lastImportedInstances.clear();
        if (data.empty()) return 0;
        try {
            fastgltf::Parser parser;
            auto buffer = fastgltf::GltfDataBuffer::FromBytes(reinterpret_cast<const std::byte*>(data.data()), data.size());
            if (!buffer) return 0;
            const auto options = fastgltf::Options::DecomposeNodeMatrices |
                                 fastgltf::Options::GenerateMeshIndices;
            auto asset = parser.loadGltf(buffer.get(), "", options);
            if (asset.error() != fastgltf::Error::None) {
                core::Logger::error("fastgltf failed to parse in-memory glTF data");
                return 0;
            }
            return loadGLTFAsset(asset.get());
        } catch (const std::exception& e) {
            core::Logger::error(std::string("GLTF parsing exception: ") + e.what());
            return 0;
        } catch (...) {
            core::Logger::error("Unknown GLTF parsing exception");
            return 0;
        }
    }

    uint32_t AssetManager::loadGLTFAsset(fastgltf::Asset& asset) {
        const size_t meshCheckpoint = m_meshes.size();
        const size_t materialCheckpoint = m_materials.size();
        const size_t textureCheckpoint = m_textures.size();
        const uint32_t nextMeshCheckpoint = m_nextMeshId;
        const uint32_t nextMaterialCheckpoint = m_nextMaterialId;
        const uint32_t nextTextureCheckpoint = m_nextTextureId;
        auto rollbackImport = [&]() {
            for (size_t i = textureCheckpoint; i < m_textures.size(); ++i) {
                auto& texture = m_textures[i];
                if (texture->imageView) vkDestroyImageView(m_context->getDevice(), texture->imageView, nullptr);
                if (texture->image) vmaDestroyImage(m_context->getAllocator(), texture->image, texture->allocation);
            }
            for (size_t i = meshCheckpoint; i < m_meshes.size(); ++i) {
                auto& mesh = m_meshes[i];
                if (mesh->vertexBuffer) vmaDestroyBuffer(m_context->getAllocator(), mesh->vertexBuffer, mesh->vertexAllocation);
                if (mesh->indexBuffer) vmaDestroyBuffer(m_context->getAllocator(), mesh->indexBuffer, mesh->indexAllocation);
            }
            m_textures.resize(textureCheckpoint);
            m_meshes.resize(meshCheckpoint);
            m_materials.resize(materialCheckpoint);
            m_nextMeshId = nextMeshCheckpoint;
            m_nextMaterialId = nextMaterialCheckpoint;
            m_nextTextureId = nextTextureCheckpoint;
            m_lastImportedInstances.clear();
            m_lastImportedNodes.clear();
        };
        try {
            uint32_t rootMeshId = 0;
            
            if (asset.meshes.empty()) return 0;
            // Decode/upload glTF images lazily and cache by source image + color space.
            // Base-color/emissive data is sRGB; normal/metallic-roughness/occlusion is linear.
            std::unordered_map<uint64_t, uint32_t> gltfTextureCache;
            auto getGltfTextureId = [&](size_t textureIndex, bool srgb) -> uint32_t {
                if (textureIndex >= asset.textures.size()) return 0;
                const auto& gltfTexture = asset.textures[textureIndex];
                if (!gltfTexture.imageIndex.has_value() || gltfTexture.imageIndex.value() >= asset.images.size()) return 0;
                const size_t imageIndex = gltfTexture.imageIndex.value();
                const uint64_t cacheKey = (static_cast<uint64_t>(imageIndex) << 1u) | (srgb ? 1ull : 0ull);
                if (const auto cached = gltfTextureCache.find(cacheKey); cached != gltfTextureCache.end())
                    return cached->second;

                const auto& image = asset.images[imageIndex];
                const uint8_t* rawBytes = nullptr;
                size_t rawSize = 0;
                if (const auto* bufferViewSource = std::get_if<fastgltf::sources::BufferView>(&image.data)) {
                    if (bufferViewSource->bufferViewIndex < asset.bufferViews.size()) {
                        const auto& bufferView = asset.bufferViews[bufferViewSource->bufferViewIndex];
                        if (bufferView.bufferIndex < asset.buffers.size()) {
                            const auto& buffer = asset.buffers[bufferView.bufferIndex];
                            const uint8_t* bufferStart = nullptr;
                            size_t bufferSize = 0;
                            if (const auto* vectorSource = std::get_if<fastgltf::sources::Vector>(&buffer.data)) {
                                bufferStart = reinterpret_cast<const uint8_t*>(vectorSource->bytes.data());
                                bufferSize = vectorSource->bytes.size();
                            } else if (const auto* arraySource = std::get_if<fastgltf::sources::Array>(&buffer.data)) {
                                bufferStart = reinterpret_cast<const uint8_t*>(arraySource->bytes.data());
                                bufferSize = arraySource->bytes.size();
                            } else if (const auto* byteViewSource = std::get_if<fastgltf::sources::ByteView>(&buffer.data)) {
                                bufferStart = reinterpret_cast<const uint8_t*>(byteViewSource->bytes.data());
                                bufferSize = byteViewSource->bytes.size();
                            }
                            if (bufferStart && bufferView.byteOffset <= bufferSize &&
                                bufferView.byteLength <= bufferSize - bufferView.byteOffset) {
                                rawBytes = bufferStart + bufferView.byteOffset;
                                rawSize = bufferView.byteLength;
                            }
                        }
                    }
                } else if (const auto* vectorSource = std::get_if<fastgltf::sources::Vector>(&image.data)) {
                    rawBytes = reinterpret_cast<const uint8_t*>(vectorSource->bytes.data());
                    rawSize = vectorSource->bytes.size();
                } else if (const auto* byteViewSource = std::get_if<fastgltf::sources::ByteView>(&image.data)) {
                    rawBytes = reinterpret_cast<const uint8_t*>(byteViewSource->bytes.data());
                    rawSize = byteViewSource->bytes.size();
                } else if (const auto* arraySource = std::get_if<fastgltf::sources::Array>(&image.data)) {
                    rawBytes = reinterpret_cast<const uint8_t*>(arraySource->bytes.data());
                    rawSize = arraySource->bytes.size();
                }

                if (!rawBytes || rawSize == 0 || rawSize > static_cast<size_t>(std::numeric_limits<int>::max()))
                    return 0;
                int width = 0, height = 0, channels = 0;
                stbi_uc* decoded = stbi_load_from_memory(rawBytes, static_cast<int>(rawSize), &width, &height, &channels, 4);
                if (!decoded || width <= 0 || height <= 0) {
                    if (decoded) stbi_image_free(decoded);
                    core::Logger::warn("AssetManager: failed to decode glTF image " + std::to_string(imageIndex));
                    return 0;
                }
                std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(decoded, &stbi_image_free);
                auto textureAsset = std::make_unique<TextureAsset>();
                textureAsset->id = m_nextTextureId++;
                textureAsset->width = static_cast<uint32_t>(width);
                textureAsset->height = static_cast<uint32_t>(height);
                try {
                    uploadTexture(textureAsset->image, textureAsset->allocation, textureAsset->imageView,
                        pixels.get(), textureAsset->width, textureAsset->height,
                        srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM, true);
                } catch (...) {
                    if (textureAsset->imageView) vkDestroyImageView(m_context->getDevice(), textureAsset->imageView, nullptr);
                    if (textureAsset->image) vmaDestroyImage(m_context->getAllocator(), textureAsset->image, textureAsset->allocation);
                    throw;
                }
                const uint32_t id = textureAsset->id;
                m_textures.push_back(std::move(textureAsset));
                gltfTextureCache.emplace(cacheKey, id);
                core::Logger::info("Uploaded cached glTF image " + std::to_string(imageIndex) +
                    (srgb ? " (sRGB)" : " (linear)"));
                return id;
            };

            // Import every mesh primitive. glTF meshes may contain multiple primitives,
            // each with its own material and index/vertex streams.
            std::vector<std::vector<uint32_t>> importedMeshIds(asset.meshes.size());
            for (size_t meshIndex = 0; meshIndex < asset.meshes.size(); ++meshIndex) {
              const auto& gltfMesh = asset.meshes[meshIndex];
              for (const auto& primitive : gltfMesh.primitives) {
            if (primitive.type != fastgltf::PrimitiveType::Triangles) {
                core::Logger::warn("AssetManager: skipping non-triangle glTF primitive");
                continue;
            }
            
            auto posIt = primitive.findAttribute("POSITION");
            if (posIt == primitive.attributes.end()) {
                core::Logger::warn("AssetManager: skipping primitive without POSITION attribute");
                continue;
            }

            if (posIt->accessorIndex >= asset.accessors.size()) continue;
            auto& positionAccessor = asset.accessors[posIt->accessorIndex];
            if (positionAccessor.count == 0) continue;

            std::vector<renderer::Vertex> vertices(positionAccessor.count);
            std::vector<uint32_t> indices;

            // fastgltf's typed iterator handles accessor byteStride, sparse overrides,
            // component conversion and normalized integer attributes.
            size_t positionIndex = 0;
            fastgltf::iterateAccessor<fastgltf::math::fvec3>(asset, positionAccessor,
                [&](const fastgltf::math::fvec3& value) {
                    if (positionIndex >= vertices.size()) return;
                    vertices[positionIndex].position = glm::vec3(
                        static_cast<float>(value[0]), static_cast<float>(value[1]), static_cast<float>(value[2]));
                    vertices[positionIndex].normal = glm::vec3(0.0f);
                    vertices[positionIndex].texCoord = glm::vec2(0.0f);
                    vertices[positionIndex].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                    ++positionIndex;
                });

            bool hasNormals = false;
            if (auto it = primitive.findAttribute("NORMAL"); it != primitive.attributes.end() &&
                it->accessorIndex < asset.accessors.size()) {
                hasNormals = true;
                size_t index = 0;
                fastgltf::iterateAccessor<fastgltf::math::fvec3>(asset, asset.accessors[it->accessorIndex],
                    [&](const fastgltf::math::fvec3& value) {
                        if (index < vertices.size()) {
                            vertices[index].normal = glm::vec3(
                                static_cast<float>(value[0]), static_cast<float>(value[1]), static_cast<float>(value[2]));
                        }
                        ++index;
                    });
            }

            if (auto it = primitive.findAttribute("TEXCOORD_0"); it != primitive.attributes.end() &&
                it->accessorIndex < asset.accessors.size()) {
                size_t index = 0;
                fastgltf::iterateAccessor<fastgltf::math::fvec2>(asset, asset.accessors[it->accessorIndex],
                    [&](const fastgltf::math::fvec2& value) {
                        if (index < vertices.size()) {
                            vertices[index].texCoord = glm::vec2(
                                static_cast<float>(value[0]), static_cast<float>(value[1]));
                        }
                        ++index;
                    });
            }

            if (auto it = primitive.findAttribute("TANGENT"); it != primitive.attributes.end() &&
                it->accessorIndex < asset.accessors.size()) {
                size_t index = 0;
                fastgltf::iterateAccessor<fastgltf::math::fvec4>(asset, asset.accessors[it->accessorIndex],
                    [&](const fastgltf::math::fvec4& value) {
                        if (index < vertices.size()) {
                            vertices[index].tangent = glm::vec4(
                                static_cast<float>(value[0]), static_cast<float>(value[1]),
                                static_cast<float>(value[2]), static_cast<float>(value[3]));
                        }
                        ++index;
                    });
            }

            if (primitive.indicesAccessor.has_value() && primitive.indicesAccessor.value() < asset.accessors.size()) {
                fastgltf::iterateAccessor<uint32_t>(asset, asset.accessors[primitive.indicesAccessor.value()],
                    [&](uint32_t index) { indices.push_back(index); });
            } else {
                for (size_t i = 0; i < vertices.size(); ++i) indices.push_back(static_cast<uint32_t>(i));
            }

            if (vertices.empty() || indices.empty()) continue;
            if (std::any_of(indices.begin(), indices.end(), [&](uint32_t index) { return index >= vertices.size(); })) {
                core::Logger::warn("AssetManager: skipping primitive with out-of-range indices");
                continue;
            }
            if (indices.size() % 3 != 0) {
                core::Logger::warn("AssetManager: skipping triangle primitive with an invalid index count");
                continue;
            }
            if (!hasNormals) {
                for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                    const uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
                    const glm::vec3 edgeA = vertices[i1].position - vertices[i0].position;
                    const glm::vec3 edgeB = vertices[i2].position - vertices[i0].position;
                    const glm::vec3 faceNormal = glm::cross(edgeA, edgeB);
                    vertices[i0].normal += faceNormal;
                    vertices[i1].normal += faceNormal;
                    vertices[i2].normal += faceNormal;
                }
                for (auto& vertex : vertices) {
                    vertex.normal = glm::length(vertex.normal) > 1e-6f
                        ? glm::normalize(vertex.normal) : glm::vec3(0.0f, 1.0f, 0.0f);
                }
            }

            auto meshAsset = std::make_unique<MeshAsset>();
            meshAsset->id = m_nextMeshId++;
            meshAsset->vertexCount = vertices.size();
            meshAsset->indexCount = indices.size();
            
            try {
                uploadBuffer(meshAsset->vertexBuffer, meshAsset->vertexAllocation, vertices.data(),
                    vertices.size() * sizeof(renderer::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                uploadBuffer(meshAsset->indexBuffer, meshAsset->indexAllocation, indices.data(),
                    indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
            } catch (...) {
                if (meshAsset->vertexBuffer) vmaDestroyBuffer(m_context->getAllocator(), meshAsset->vertexBuffer, meshAsset->vertexAllocation);
                if (meshAsset->indexBuffer) vmaDestroyBuffer(m_context->getAllocator(), meshAsset->indexBuffer, meshAsset->indexAllocation);
                throw;
            }

            if (rootMeshId == 0) rootMeshId = meshAsset->id;
            
            if (primitive.materialIndex.has_value() && primitive.materialIndex.value() < asset.materials.size()) {
                auto& mat = asset.materials[primitive.materialIndex.value()];
                auto materialAsset = std::make_unique<MaterialAsset>();
                materialAsset->id = m_nextMaterialId++;
                materialAsset->baseColorFactor = glm::vec4(
                    mat.pbrData.baseColorFactor[0],
                    mat.pbrData.baseColorFactor[1],
                    mat.pbrData.baseColorFactor[2],
                    mat.pbrData.baseColorFactor[3]
                );
                materialAsset->metallicFactor = mat.pbrData.metallicFactor;
                materialAsset->roughnessFactor = mat.pbrData.roughnessFactor;
                materialAsset->emissiveFactor = glm::vec3(
                    static_cast<float>(mat.emissiveFactor[0]),
                    static_cast<float>(mat.emissiveFactor[1]),
                    static_cast<float>(mat.emissiveFactor[2]));
                materialAsset->emissiveStrength = static_cast<float>(mat.emissiveStrength);
                materialAsset->alphaCutoff = static_cast<float>(mat.alphaCutoff);
                materialAsset->alphaMode = static_cast<uint32_t>(mat.alphaMode);
                materialAsset->doubleSided = mat.doubleSided;
                materialAsset->unlit = mat.unlit;
                if (mat.normalTexture.has_value())
                    materialAsset->normalScale = static_cast<float>(mat.normalTexture->scale);
                if (mat.occlusionTexture.has_value())
                    materialAsset->occlusionStrength = static_cast<float>(mat.occlusionTexture->strength);

                if (mat.pbrData.baseColorTexture.has_value())
                    materialAsset->albedoTextureId = getGltfTextureId(mat.pbrData.baseColorTexture->textureIndex, true);
                if (mat.normalTexture.has_value())
                    materialAsset->normalTextureId = getGltfTextureId(mat.normalTexture->textureIndex, false);
                if (mat.pbrData.metallicRoughnessTexture.has_value())
                    materialAsset->ormTextureId = getGltfTextureId(mat.pbrData.metallicRoughnessTexture->textureIndex, false);
                if (mat.occlusionTexture.has_value())
                    materialAsset->occlusionTextureId = getGltfTextureId(mat.occlusionTexture->textureIndex, false);
                if (mat.emissiveTexture.has_value())
                    materialAsset->emissiveTextureId = getGltfTextureId(mat.emissiveTexture->textureIndex, true);
                meshAsset->materialId = materialAsset->id;
                m_materials.push_back(std::move(materialAsset));
            }
            // Keep the mesh alive until its material assignment is complete.
            importedMeshIds[meshIndex].push_back(meshAsset->id);
            m_meshes.push_back(std::move(meshAsset));
              }
            }
            // Instantiate only meshes referenced by nodes in the active scene when available.
            // Node TRS is preserved; assets without nodes fall back to one identity instance per primitive.
            // Preserve the authored glTF node tree. Each glTF node becomes an
            // ImportedSceneNode, including transform-only grouping nodes.
            std::function<void(size_t, int64_t, const glm::mat4&)> visitNode =
                [&](size_t nodeIndex, int64_t parentNodeIndex, const glm::mat4& parentTransform) {
                    if (nodeIndex >= asset.nodes.size()) return;
                    const auto& node = asset.nodes[nodeIndex];
                    glm::mat4 local(1.0f);
                    if (const auto* trs = std::get_if<fastgltf::TRS>(&node.transform)) {
                        const glm::vec3 translation(
                            static_cast<float>(trs->translation[0]),
                            static_cast<float>(trs->translation[1]),
                            static_cast<float>(trs->translation[2]));
                        const glm::vec3 scale(
                            static_cast<float>(trs->scale[0]),
                            static_cast<float>(trs->scale[1]),
                            static_cast<float>(trs->scale[2]));
                        const glm::quat rotation(
                            static_cast<float>(trs->rotation[3]),
                            static_cast<float>(trs->rotation[0]),
                            static_cast<float>(trs->rotation[1]),
                            static_cast<float>(trs->rotation[2]));
                        local = glm::translate(glm::mat4(1.0f), translation) *
                                glm::mat4_cast(rotation) *
                                glm::scale(glm::mat4(1.0f), scale);
                    } else if (const auto* matrix = std::get_if<fastgltf::math::fmat4x4>(&node.transform)) {
                        for (int column = 0; column < 4; ++column)
                            for (int row = 0; row < 4; ++row)
                                local[column][row] = static_cast<float>((*matrix)[column][row]);
                    }

                    ImportedSceneNode importedNode;
                    importedNode.nodeIndex = nodeIndex;
                    const size_t importedNodePosition = m_lastImportedNodes.size();
                    importedNode.parentNodeIndex = parentNodeIndex;
                    importedNode.name = node.name.empty() ? ("Node_" + std::to_string(nodeIndex)) : std::string(node.name);
                    if (node.meshIndex.has_value() && node.meshIndex.value() < importedMeshIds.size())
                        importedNode.meshIds = importedMeshIds[node.meshIndex.value()];

                    glm::vec3 skew(0.0f), localTranslation(0.0f), localScale(1.0f);
                    glm::vec4 perspective(0.0f);
                    glm::quat localRotation(1.0f, 0.0f, 0.0f, 0.0f);
                    if (glm::decompose(local, localScale, localRotation, localTranslation, skew, perspective)) {
                        importedNode.translation = localTranslation;
                        importedNode.rotation = glm::normalize(localRotation);
                        importedNode.scale = localScale;
                    }
                    m_lastImportedNodes.push_back(std::move(importedNode));

                    const glm::mat4 world = parentTransform * local;
                    if (node.meshIndex.has_value() && node.meshIndex.value() < importedMeshIds.size()) {
                        glm::vec3 worldPosition(world[3]);
                        glm::vec3 worldScale(
                            glm::length(glm::vec3(world[0])),
                            glm::length(glm::vec3(world[1])),
                            glm::length(glm::vec3(world[2])));
                        glm::mat3 rotationMatrix(world);
                        for (int axis = 0; axis < 3; ++axis)
                            if (worldScale[axis] > 1e-6f) rotationMatrix[axis] /= worldScale[axis];
                        const glm::quat worldRotation = glm::normalize(glm::quat_cast(rotationMatrix));
                        for (uint32_t id : importedNode.meshIds) {
                            ImportedMeshInstance instance;
                            instance.meshId = id;
                            instance.name = importedNode.name;
                            instance.translation = worldPosition;
                            instance.rotation = worldRotation;
                            instance.scale = worldScale;
                            m_lastImportedInstances.push_back(std::move(instance));
                        }
                    }
                    for (size_t childIndex : node.children)
                        visitNode(childIndex, static_cast<int64_t>(importedNodePosition), world);
                };

            if (!asset.scenes.empty()) {
                size_t sceneIndex = asset.defaultScene.value_or(0);
                if (sceneIndex >= asset.scenes.size()) sceneIndex = 0;
                for (size_t nodeIndex : asset.scenes[sceneIndex].nodeIndices)
                    visitNode(nodeIndex, -1, glm::mat4(1.0f));
            } else {
                std::vector<bool> isChild(asset.nodes.size(), false);
                for (const auto& node : asset.nodes)
                    for (size_t childIndex : node.children)
                        if (childIndex < isChild.size()) isChild[childIndex] = true;
                for (size_t i = 0; i < asset.nodes.size(); ++i)
                    if (!isChild[i]) visitNode(i, -1, glm::mat4(1.0f));
            }
            if (m_lastImportedNodes.empty()) {
                for (size_t meshIndex = 0; meshIndex < importedMeshIds.size(); ++meshIndex) {
                    ImportedSceneNode node;
                    node.nodeIndex = meshIndex;
                    node.name = "Mesh_" + std::to_string(meshIndex);
                    node.meshIds = importedMeshIds[meshIndex];
                    m_lastImportedNodes.push_back(std::move(node));
                    for (uint32_t id : importedMeshIds[meshIndex]) {
                        ImportedMeshInstance instance;
                        instance.meshId = id;
                        instance.name = node.name;
                        m_lastImportedInstances.push_back(std::move(instance));
                    }
                }
            }
            if (rootMeshId == 0) {
                core::Logger::error("AssetManager: glTF contains no supported triangle primitives");
            }
            return rootMeshId;
        } catch (const std::exception& e) {
            rollbackImport();
            core::Logger::error(std::string("GLTF asset import rolled back: ") + e.what());
            return 0;
        } catch (...) {
            rollbackImport();
            core::Logger::error("Unknown GLTF asset import exception; import rolled back");
            return 0;
        }
    }

    MeshAsset* AssetManager::getMesh(uint32_t id) {
        for (auto& mesh : m_meshes) {
            if (mesh->id == id) return mesh.get();
        }
        return nullptr;
    }

    MaterialAsset* AssetManager::getMaterial(uint32_t id) {
        for (auto& mat : m_materials) {
            if (mat->id == id) return mat.get();
        }
        return nullptr;
    }

    uint32_t AssetManager::loadTextureFromFile(const std::string& path, bool generateMips) {
        int w = 0, h = 0, comp = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
        if (!pixels) {
            core::Logger::warn("AssetManager: failed to load image file from: " + path);
            return 0;
        }

        auto tex = std::make_unique<TextureAsset>();
        tex->id = m_nextTextureId++;
        tex->width = static_cast<uint32_t>(w);
        tex->height = static_cast<uint32_t>(h);
        uploadTexture(tex->image, tex->allocation, tex->imageView, pixels, tex->width, tex->height, VK_FORMAT_R8G8B8A8_UNORM, generateMips);
        stbi_image_free(pixels);

        uint32_t id = tex->id;
        m_textures.push_back(std::move(tex));
        core::Logger::info("AssetManager: Loaded texture #" + std::to_string(id) + " (" + std::to_string(w) + "x" + std::to_string(h) + ") from " + path);
        return id;
    }

    uint32_t AssetManager::createMaterial(const MaterialAsset& mat) {
        auto m = std::make_unique<MaterialAsset>(mat);
        m->id = m_nextMaterialId++;
        uint32_t id = m->id;
        m_materials.push_back(std::move(m));
        return id;
    }

    void AssetManager::updateMaterial(uint32_t materialId, const MaterialAsset& mat) {
        for (auto& m : m_materials) {
            if (m->id == materialId) {
                *m = mat;
                m->id = materialId;
                return;
            }
        }
        auto m = std::make_unique<MaterialAsset>(mat);
        m->id = materialId;
        m_materials.push_back(std::move(m));
    }

    uint32_t AssetManager::createProceduralTexture(const std::string& presetName, const std::string& mapType) {
        const uint32_t size = 512;
        std::vector<uint8_t> pixels(size * size * 4, 255);

        auto hash = [](int x, int y, int seed) -> float {
            int n = x + y * 57 + seed * 131;
            n = (n << 13) ^ n;
            return (1.0f - static_cast<float>((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f) * 0.5f + 0.5f;
        };

        if (presetName == "Bricks") {
            const int brickW = 64;
            const int brickH = 32;
            const int mortarSize = 3;

            for (uint32_t y = 0; y < size; ++y) {
                int row = static_cast<int>(y) / brickH;
                int rowY = static_cast<int>(y) % brickH;
                int xOffset = (row % 2 != 0) ? (brickW / 2) : 0;

                for (uint32_t x = 0; x < size; ++x) {
                    int shiftedX = (static_cast<int>(x) + xOffset) % size;
                    int col = shiftedX / brickW;
                    int colX = shiftedX % brickW;

                    bool isMortar = (rowY < mortarSize || rowY >= brickH - mortarSize ||
                                     colX < mortarSize || colX >= brickW - mortarSize);

                    size_t idx = (y * size + x) * 4;
                    if (mapType == "albedo") {
                        if (isMortar) {
                            pixels[idx + 0] = 185;
                            pixels[idx + 1] = 180;
                            pixels[idx + 2] = 175;
                        } else {
                            float brickNoise = hash(col, row, 42);
                            float grain = hash(static_cast<int>(x), static_cast<int>(y), 11) * 0.15f - 0.075f;
                            float r = std::clamp(0.68f + brickNoise * 0.12f + grain, 0.0f, 1.0f);
                            float g = std::clamp(0.24f + brickNoise * 0.08f + grain, 0.0f, 1.0f);
                            float b = std::clamp(0.15f + brickNoise * 0.05f + grain, 0.0f, 1.0f);
                            pixels[idx + 0] = static_cast<uint8_t>(r * 255.0f);
                            pixels[idx + 1] = static_cast<uint8_t>(g * 255.0f);
                            pixels[idx + 2] = static_cast<uint8_t>(b * 255.0f);
                        }
                        pixels[idx + 3] = 255;
                    } else if (mapType == "normal") {
                        if (isMortar) {
                            pixels[idx + 0] = 128;
                            pixels[idx + 1] = 128;
                            pixels[idx + 2] = 255;
                        } else {
                            float distLeft = static_cast<float>(colX - mortarSize);
                            float distRight = static_cast<float>((brickW - mortarSize) - colX);
                            float distTop = static_cast<float>(rowY - mortarSize);
                            float distBottom = static_cast<float>((brickH - mortarSize) - rowY);
                            float bevel = 4.0f;

                            float nx = 0.0f;
                            float ny = 0.0f;
                            if (distLeft < bevel) nx = -1.0f * (1.0f - distLeft / bevel);
                            else if (distRight < bevel) nx = 1.0f * (1.0f - distRight / bevel);
                            if (distTop < bevel) ny = -1.0f * (1.0f - distTop / bevel);
                            else if (distBottom < bevel) ny = 1.0f * (1.0f - distBottom / bevel);

                            glm::vec3 n = glm::normalize(glm::vec3(nx * 0.6f, ny * 0.6f, 1.0f));
                            pixels[idx + 0] = static_cast<uint8_t>((n.x * 0.5f + 0.5f) * 255.0f);
                            pixels[idx + 1] = static_cast<uint8_t>((n.y * 0.5f + 0.5f) * 255.0f);
                            pixels[idx + 2] = static_cast<uint8_t>((n.z * 0.5f + 0.5f) * 255.0f);
                        }
                        pixels[idx + 3] = 255;
                    } else { // orm: R = AO, G = Roughness, B = Metallic
                        if (isMortar) {
                            pixels[idx + 0] = 50;  // Strong AO in mortar
                            pixels[idx + 1] = 245; // Very rough
                            pixels[idx + 2] = 0;   // Dielectric
                        } else {
                            pixels[idx + 0] = 255; // Full ambient lighting
                            pixels[idx + 1] = 195; // Roughness ~0.76
                            pixels[idx + 2] = 0;   // Dielectric
                        }
                        pixels[idx + 3] = 255;
                    }
                }
            }
        } else if (presetName == "Tiles") {
            const int tileSize = 64;
            const int groutSize = 2;

            for (uint32_t y = 0; y < size; ++y) {
                int ty = static_cast<int>(y) % tileSize;
                int row = static_cast<int>(y) / tileSize;
                for (uint32_t x = 0; x < size; ++x) {
                    int tx = static_cast<int>(x) % tileSize;
                    int col = static_cast<int>(x) / tileSize;
                    bool isGrout = (tx < groutSize || tx >= tileSize - groutSize ||
                                    ty < groutSize || ty >= tileSize - groutSize);
                    size_t idx = (y * size + x) * 4;

                    if (mapType == "albedo") {
                        if (isGrout) {
                            pixels[idx + 0] = 40;
                            pixels[idx + 1] = 42;
                            pixels[idx + 2] = 45;
                        } else {
                            bool checker = ((row + col) % 2 == 0);
                            float tint = checker ? 0.95f : 0.82f;
                            pixels[idx + 0] = static_cast<uint8_t>(235.0f * tint);
                            pixels[idx + 1] = static_cast<uint8_t>(240.0f * tint);
                            pixels[idx + 2] = static_cast<uint8_t>(245.0f * tint);
                        }
                        pixels[idx + 3] = 255;
                    } else if (mapType == "normal") {
                        if (isGrout) {
                            pixels[idx + 0] = 128; pixels[idx + 1] = 128; pixels[idx + 2] = 255;
                        } else {
                            float dL = static_cast<float>(tx - groutSize);
                            float dR = static_cast<float>((tileSize - groutSize) - tx);
                            float dT = static_cast<float>(ty - groutSize);
                            float dB = static_cast<float>((tileSize - groutSize) - ty);
                            float bevel = 3.0f;
                            float nx = 0.0f, ny = 0.0f;
                            if (dL < bevel) nx = -(1.0f - dL / bevel);
                            else if (dR < bevel) nx = (1.0f - dR / bevel);
                            if (dT < bevel) ny = -(1.0f - dT / bevel);
                            else if (dB < bevel) ny = (1.0f - dB / bevel);

                            glm::vec3 n = glm::normalize(glm::vec3(nx * 0.8f, ny * 0.8f, 1.0f));
                            pixels[idx + 0] = static_cast<uint8_t>((n.x * 0.5f + 0.5f) * 255.0f);
                            pixels[idx + 1] = static_cast<uint8_t>((n.y * 0.5f + 0.5f) * 255.0f);
                            pixels[idx + 2] = static_cast<uint8_t>((n.z * 0.5f + 0.5f) * 255.0f);
                        }
                        pixels[idx + 3] = 255;
                    } else { // orm
                        if (isGrout) {
                            pixels[idx + 0] = 80;  // AO
                            pixels[idx + 1] = 230; // Rough grout
                            pixels[idx + 2] = 0;
                        } else {
                            pixels[idx + 0] = 255;
                            pixels[idx + 1] = 30;  // Glossy tile! (~0.12 roughness)
                            pixels[idx + 2] = 0;
                        }
                        pixels[idx + 3] = 255;
                    }
                }
            }
        } else if (presetName == "Wood") {
            const int plankW = 128;
            for (uint32_t y = 0; y < size; ++y) {
                for (uint32_t x = 0; x < size; ++x) {
                    int plankCol = static_cast<int>(x) / plankW;
                    int px = static_cast<int>(x) % plankW;
                    bool isSeam = (px < 2 || px >= plankW - 2);

                    float grain = std::sin(static_cast<float>(x) * 0.15f + hash(static_cast<int>(x) / 4, static_cast<int>(y) / 24, 7) * 4.0f) * 0.5f + 0.5f;
                    float fineGrain = hash(static_cast<int>(x), static_cast<int>(y) / 2, 33) * 0.1f;
                    size_t idx = (y * size + x) * 4;

                    if (mapType == "albedo") {
                        if (isSeam) {
                            pixels[idx + 0] = 45; pixels[idx + 1] = 25; pixels[idx + 2] = 10;
                        } else {
                            float plankTint = 0.9f + hash(plankCol, 0, 99) * 0.2f;
                            float blend = std::clamp(grain * 0.4f + fineGrain, 0.0f, 1.0f);
                            glm::vec3 woodDark(0.45f, 0.25f, 0.10f);
                            glm::vec3 woodLight(0.68f, 0.45f, 0.22f);
                            glm::vec3 col = glm::mix(woodLight, woodDark, blend) * plankTint;
                            pixels[idx + 0] = static_cast<uint8_t>(std::clamp(col.r * 255.0f, 0.0f, 255.0f));
                            pixels[idx + 1] = static_cast<uint8_t>(std::clamp(col.g * 255.0f, 0.0f, 255.0f));
                            pixels[idx + 2] = static_cast<uint8_t>(std::clamp(col.b * 255.0f, 0.0f, 255.0f));
                        }
                        pixels[idx + 3] = 255;
                    } else if (mapType == "normal") {
                        float bump = (grain - 0.5f) * 0.25f;
                        glm::vec3 n = glm::normalize(glm::vec3(bump, 0.0f, 1.0f));
                        pixels[idx + 0] = static_cast<uint8_t>((n.x * 0.5f + 0.5f) * 255.0f);
                        pixels[idx + 1] = static_cast<uint8_t>((n.y * 0.5f + 0.5f) * 255.0f);
                        pixels[idx + 2] = static_cast<uint8_t>((n.z * 0.5f + 0.5f) * 255.0f);
                        pixels[idx + 3] = 255;
                    } else { // orm
                        pixels[idx + 0] = isSeam ? 100 : 255;
                        pixels[idx + 1] = isSeam ? 240 : static_cast<uint8_t>(90 + grain * 40); // Satin wood roughness (~0.4)
                        pixels[idx + 2] = 0;
                        pixels[idx + 3] = 255;
                    }
                }
            }
        } else if (presetName == "MetalGrid") {
            const int cellSize = 32;
            for (uint32_t y = 0; y < size; ++y) {
                int cy = static_cast<int>(y) % cellSize - cellSize / 2;
                for (uint32_t x = 0; x < size; ++x) {
                    int cx = static_cast<int>(x) % cellSize - cellSize / 2;
                    float dist = std::sqrt(static_cast<float>(cx * cx + cy * cy));
                    bool inHole = dist < (static_cast<float>(cellSize) * 0.30f);
                    bool inRidge = std::abs(dist - (static_cast<float>(cellSize) * 0.32f)) < 2.0f;
                    size_t idx = (y * size + x) * 4;

                    if (mapType == "albedo") {
                        if (inHole) {
                            pixels[idx + 0] = 30; pixels[idx + 1] = 32; pixels[idx + 2] = 35;
                        } else if (inRidge) {
                            pixels[idx + 0] = 220; pixels[idx + 1] = 225; pixels[idx + 2] = 230;
                        } else {
                            pixels[idx + 0] = 160; pixels[idx + 1] = 165; pixels[idx + 2] = 170;
                        }
                        pixels[idx + 3] = 255;
                    } else if (mapType == "normal") {
                        if (inHole || dist > static_cast<float>(cellSize) * 0.45f) {
                            pixels[idx + 0] = 128; pixels[idx + 1] = 128; pixels[idx + 2] = 255;
                        } else {
                            float nx = (static_cast<float>(cx) / std::max(dist, 1.0f)) * 0.5f;
                            float ny = (static_cast<float>(cy) / std::max(dist, 1.0f)) * 0.5f;
                            glm::vec3 n = glm::normalize(glm::vec3(nx, ny, 1.0f));
                            pixels[idx + 0] = static_cast<uint8_t>((n.x * 0.5f + 0.5f) * 255.0f);
                            pixels[idx + 1] = static_cast<uint8_t>((n.y * 0.5f + 0.5f) * 255.0f);
                            pixels[idx + 2] = static_cast<uint8_t>((n.z * 0.5f + 0.5f) * 255.0f);
                        }
                        pixels[idx + 3] = 255;
                    } else { // orm
                        if (inHole) {
                            pixels[idx + 0] = 40;  // Deep hole AO
                            pixels[idx + 1] = 160; // Moderate roughness
                            pixels[idx + 2] = 80;  // Partial metal
                        } else {
                            pixels[idx + 0] = 255;
                            pixels[idx + 1] = 55;  // Polished metal (~0.22 roughness)
                            pixels[idx + 2] = 250; // Pure metal! (~0.98 metallic)
                        }
                        pixels[idx + 3] = 255;
                    }
                }
            }
        } else { // Checkerboard or fallback
            const int checkSize = 64;
            for (uint32_t y = 0; y < size; ++y) {
                int cy = static_cast<int>(y) / checkSize;
                for (uint32_t x = 0; x < size; ++x) {
                    int cx = static_cast<int>(x) / checkSize;
                    bool white = ((cx + cy) % 2 == 0);
                    size_t idx = (y * size + x) * 4;

                    if (mapType == "albedo") {
                        uint8_t val = white ? 240 : 25;
                        pixels[idx + 0] = val; pixels[idx + 1] = val; pixels[idx + 2] = val; pixels[idx + 3] = 255;
                    } else if (mapType == "normal") {
                        pixels[idx + 0] = 128; pixels[idx + 1] = 128; pixels[idx + 2] = 255; pixels[idx + 3] = 255;
                    } else { // orm
                        pixels[idx + 0] = 255;
                        pixels[idx + 1] = white ? 60 : 120; // 0.24 - 0.47 roughness
                        pixels[idx + 2] = 0;
                        pixels[idx + 3] = 255;
                    }
                }
            }
        }

        auto tex = std::make_unique<TextureAsset>();
        tex->id = m_nextTextureId++;
        tex->width = size;
        tex->height = size;
        uploadTexture(tex->image, tex->allocation, tex->imageView, pixels.data(), size, size, VK_FORMAT_R8G8B8A8_UNORM, true);

        uint32_t id = tex->id;
        m_textures.push_back(std::move(tex));
        core::Logger::info("AssetManager: Created procedural " + presetName + " (" + mapType + ") texture #" + std::to_string(id));
        return id;
    }

    uint32_t AssetManager::applyPbrPreset(const std::string& presetName) {
        uint32_t albedoId = createProceduralTexture(presetName, "albedo");
        uint32_t normalId = createProceduralTexture(presetName, "normal");
        uint32_t ormId    = createProceduralTexture(presetName, "orm");

        MaterialAsset mat{};
        mat.baseColorFactor = glm::vec4(1.0f);
        mat.metallicFactor = 1.0f;
        mat.roughnessFactor = 1.0f;
        mat.albedoTextureId = albedoId;
        mat.normalTextureId = normalId;
        mat.ormTextureId = ormId;

        return createMaterial(mat);
    }

    void AssetManager::createFallbackMeshes() {
        // --- 1. MESH 1: GROUND BOX / PLANE (Large horizontal surface) ---
        std::vector<engine::renderer::Vertex> planeVertices = {
            // Front
            { {-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f} },
            { { 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f} },
            { { 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f} },
            { {-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f} },
            // Back
            { {-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f} },
            { {-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f} },
            { { 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f} },
            { { 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f} },
            // Top (Normal 0, 1, 0)
            { {-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 10.0f} },
            { {-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },
            { { 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {10.0f, 0.0f} },
            { { 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {10.0f, 10.0f} },
            // Bottom
            { {-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f} },
            { { 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f} },
            { { 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f} },
            { {-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f} },
            // Right
            { { 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f} },
            { { 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f} },
            { { 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f} },
            { { 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f} },
            // Left
            { {-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f} },
            { {-0.5f, -0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f} },
            { {-0.5f,  0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f} },
            { {-0.5f,  0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f} }
        };
        std::vector<uint32_t> cubeIndices = {
            0,  1,  2,  2,  3,  0,
            4,  5,  6,  6,  7,  4,
            8,  9, 10, 10, 11,  8,
            12, 13, 14, 14, 15, 12,
            16, 17, 18, 18, 19, 16,
            20, 21, 22, 22, 23, 20
        };

        auto plane = std::make_unique<MeshAsset>();
        plane->id = m_nextMeshId++; // 1
        plane->vertexCount = planeVertices.size();
        plane->indexCount = cubeIndices.size();
        uploadBuffer(plane->vertexBuffer, plane->vertexAllocation, planeVertices.data(), planeVertices.size() * sizeof(engine::renderer::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        uploadBuffer(plane->indexBuffer, plane->indexAllocation, cubeIndices.data(), cubeIndices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        m_meshes.push_back(std::move(plane));

        // --- 2. MESH 2: STANDARD CUBE ---
        auto cube = std::make_unique<MeshAsset>();
        cube->id = m_nextMeshId++; // 2
        cube->vertexCount = planeVertices.size();
        cube->indexCount = cubeIndices.size();
        uploadBuffer(cube->vertexBuffer, cube->vertexAllocation, planeVertices.data(), planeVertices.size() * sizeof(engine::renderer::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        uploadBuffer(cube->indexBuffer, cube->indexAllocation, cubeIndices.data(), cubeIndices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        m_meshes.push_back(std::move(cube));

        // --- 3. MESH 3: UV SPHERE (Smooth Normals & Proper UVs) ---
        std::vector<engine::renderer::Vertex> sphereVertices;
        std::vector<uint32_t> sphereIndices;
        int const rings = 24;
        int const sectors = 32;
        float const radius = 0.5f;
        float const R = 1.0f / (float)(rings - 1);
        float const S = 1.0f / (float)(sectors - 1);

        for (int r = 0; r < rings; ++r) {
            for (int s = 0; s < sectors; ++s) {
                float const y = sin(-glm::half_pi<float>() + glm::pi<float>() * r * R);
                float const x = cos(2.0f * glm::pi<float>() * s * S) * sin(glm::pi<float>() * r * R);
                float const z = sin(2.0f * glm::pi<float>() * s * S) * sin(glm::pi<float>() * r * R);

                engine::renderer::Vertex v{};
                v.position = glm::vec3(x * radius, y * radius, z * radius);
                v.normal = glm::normalize(glm::vec3(x, y, z));
                v.texCoord = glm::vec2(s * S, r * R);
                v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                sphereVertices.push_back(v);
            }
        }

        for (int r = 0; r < rings - 1; ++r) {
            for (int s = 0; s < sectors - 1; ++s) {
                uint32_t cur = r * sectors + s;
                uint32_t next = (r + 1) * sectors + s;
                sphereIndices.push_back(cur);
                sphereIndices.push_back(next);
                sphereIndices.push_back(next + 1);

                sphereIndices.push_back(cur);
                sphereIndices.push_back(next + 1);
                sphereIndices.push_back(cur + 1);
            }
        }

        auto sphere = std::make_unique<MeshAsset>();
        sphere->id = m_nextMeshId++; // 3
        sphere->vertexCount = sphereVertices.size();
        sphere->indexCount = sphereIndices.size();
        uploadBuffer(sphere->vertexBuffer, sphere->vertexAllocation, sphereVertices.data(), sphereVertices.size() * sizeof(engine::renderer::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        uploadBuffer(sphere->indexBuffer, sphere->indexAllocation, sphereIndices.data(), sphereIndices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        m_meshes.push_back(std::move(sphere));

        // --- 4. MESH 4: CYLINDER ---
        std::vector<engine::renderer::Vertex> cylVertices;
        std::vector<uint32_t> cylIndices;
        int const cylSectors = 24;
        float const cylRad = 0.5f;
        float const cylHalfH = 0.5f;

        // Side vertices
        for (int i = 0; i <= cylSectors; ++i) {
            float angle = (float)i / (float)cylSectors * glm::two_pi<float>();
            float x = cos(angle);
            float z = sin(angle);
            float u = (float)i / (float)cylSectors;

            // Top rim
            cylVertices.push_back({ {x * cylRad, cylHalfH, z * cylRad}, {x, 0.0f, z}, {u, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f} });
            // Bottom rim
            cylVertices.push_back({ {x * cylRad, -cylHalfH, z * cylRad}, {x, 0.0f, z}, {u, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f} });
        }

        for (int i = 0; i < cylSectors; ++i) {
            uint32_t topCur = i * 2;
            uint32_t botCur = i * 2 + 1;
            uint32_t topNext = (i + 1) * 2;
            uint32_t botNext = (i + 1) * 2 + 1;

            cylIndices.push_back(topCur);
            cylIndices.push_back(botCur);
            cylIndices.push_back(botNext);

            cylIndices.push_back(topCur);
            cylIndices.push_back(botNext);
            cylIndices.push_back(topNext);
        }

        auto cylinder = std::make_unique<MeshAsset>();
        cylinder->id = m_nextMeshId++; // 4
        cylinder->vertexCount = cylVertices.size();
        cylinder->indexCount = cylIndices.size();
        uploadBuffer(cylinder->vertexBuffer, cylinder->vertexAllocation, cylVertices.data(), cylVertices.size() * sizeof(engine::renderer::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        uploadBuffer(cylinder->indexBuffer, cylinder->indexAllocation, cylIndices.data(), cylIndices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        m_meshes.push_back(std::move(cylinder));

        // --- 5. MESH 5: CAPSULE ---
        std::vector<engine::renderer::Vertex> capVertices;
        std::vector<uint32_t> capIndices;
        int const capRings = 16;
        int const capSectors = 24;
        float const capRadius = 0.5f;
        float const capCylHalfH = 0.5f;

        for (int r = 0; r < capRings; ++r) {
            float phi = glm::pi<float>() * (float)r / (float)(capRings - 1);
            float yOffset = (r < capRings / 2) ? capCylHalfH : -capCylHalfH;
            for (int s = 0; s < capSectors; ++s) {
                float theta = glm::two_pi<float>() * (float)s / (float)(capSectors - 1);
                float x = sin(phi) * cos(theta);
                float y = cos(phi);
                float z = sin(phi) * sin(theta);

                engine::renderer::Vertex v{};
                v.position = glm::vec3(x * capRadius, y * capRadius + yOffset, z * capRadius);
                v.normal = glm::normalize(glm::vec3(x, y, z));
                v.texCoord = glm::vec2((float)s / (float)(capSectors - 1), (float)r / (float)(capRings - 1));
                v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                capVertices.push_back(v);
            }
        }

        for (int r = 0; r < capRings - 1; ++r) {
            for (int s = 0; s < capSectors - 1; ++s) {
                uint32_t cur = r * capSectors + s;
                uint32_t next = (r + 1) * capSectors + s;
                capIndices.push_back(cur);
                capIndices.push_back(next);
                capIndices.push_back(next + 1);

                capIndices.push_back(cur);
                capIndices.push_back(next + 1);
                capIndices.push_back(cur + 1);
            }
        }

        auto capsule = std::make_unique<MeshAsset>();
        capsule->id = m_nextMeshId++; // 5
        capsule->vertexCount = capVertices.size();
        capsule->indexCount = capIndices.size();
        uploadBuffer(capsule->vertexBuffer, capsule->vertexAllocation, capVertices.data(), capVertices.size() * sizeof(engine::renderer::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        uploadBuffer(capsule->indexBuffer, capsule->indexAllocation, capIndices.data(), capIndices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        m_meshes.push_back(std::move(capsule));

        core::Logger::info("Created 5 high-fidelity Unreal-grade fallback primitives (Plane, Cube, Sphere, Cylinder, Capsule).");
    }
}
