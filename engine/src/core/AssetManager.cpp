#include "core/AssetManager.hpp"
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <glm/gtc/constants.hpp>
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
        try {
            uint32_t rootMeshId = 0;
            
            if (asset.meshes.empty()) return 0;
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
                core::Logger::error("GLB primitive has no POSITION attribute");
                return 0;
            }

            if (posIt->accessorIndex >= asset.accessors.size()) return 0;
            auto& positionAccessor = asset.accessors[posIt->accessorIndex];
            if (positionAccessor.count == 0) return 0;

            std::vector<renderer::Vertex> vertices(positionAccessor.count);
            std::vector<uint32_t> indices;

            auto extractData = [&](const fastgltf::Accessor& accessor, auto&& callback) {
                if (!accessor.bufferViewIndex.has_value()) return;
                size_t bvIdx = accessor.bufferViewIndex.value();
                if (bvIdx >= asset.bufferViews.size()) return;
                auto& bufferView = asset.bufferViews[bvIdx];
                if (bufferView.bufferIndex >= asset.buffers.size()) return;
                auto& buf = asset.buffers[bufferView.bufferIndex];
                
                const uint8_t* bufferData = nullptr;
                if (auto* vectorBuffer = std::get_if<fastgltf::sources::Vector>(&buf.data)) {
                     bufferData = reinterpret_cast<const uint8_t*>(vectorBuffer->bytes.data());
                } else if (auto* arrayBuffer = std::get_if<fastgltf::sources::Array>(&buf.data)) {
                     bufferData = reinterpret_cast<const uint8_t*>(arrayBuffer->bytes.data());
                } else if (auto* byteViewBuffer = std::get_if<fastgltf::sources::ByteView>(&buf.data)) {
                     bufferData = reinterpret_cast<const uint8_t*>(byteViewBuffer->bytes.data());
                }
                if (bufferData) {
                     const uint8_t* accessorData = bufferData + bufferView.byteOffset + accessor.byteOffset;
                     callback(accessorData, accessor.count);
                }
            };

            extractData(positionAccessor, [&](const uint8_t* d, size_t count) {
                const float* f = reinterpret_cast<const float*>(d);
                for (size_t i = 0; i < count; ++i) {
                    vertices[i].position = glm::vec3(f[i * 3 + 0], f[i * 3 + 1], f[i * 3 + 2]);
                    vertices[i].normal = glm::vec3(0, 1, 0); // default
                    vertices[i].texCoord = glm::vec2(0, 0);
                }
            });

            if (auto it = primitive.findAttribute("NORMAL"); it != primitive.attributes.end()) {
                if (it->accessorIndex < asset.accessors.size()) {
                    auto& normalAccessor = asset.accessors[it->accessorIndex];
                    extractData(normalAccessor, [&](const uint8_t* d, size_t count) {
                        const float* f = reinterpret_cast<const float*>(d);
                        for (size_t i = 0; i < std::min(count, vertices.size()); ++i) {
                            vertices[i].normal = glm::vec3(f[i * 3 + 0], f[i * 3 + 1], f[i * 3 + 2]);
                        }
                    });
                }
            }
            
            if (auto it = primitive.findAttribute("TEXCOORD_0"); it != primitive.attributes.end()) {
                if (it->accessorIndex < asset.accessors.size()) {
                    auto& uvAccessor = asset.accessors[it->accessorIndex];
                    extractData(uvAccessor, [&](const uint8_t* d, size_t count) {
                        const float* f = reinterpret_cast<const float*>(d);
                        for (size_t i = 0; i < std::min(count, vertices.size()); ++i) {
                            vertices[i].texCoord = glm::vec2(f[i * 2 + 0], f[i * 2 + 1]);
                        }
                    });
                }
            }

            if (primitive.indicesAccessor.has_value() && primitive.indicesAccessor.value() < asset.accessors.size()) {
                auto& idxAccessor = asset.accessors[primitive.indicesAccessor.value()];
                extractData(idxAccessor, [&](const uint8_t* d, size_t count) {
                    if (idxAccessor.componentType == fastgltf::ComponentType::UnsignedShort) {
                         const uint16_t* u16 = reinterpret_cast<const uint16_t*>(d);
                         for (size_t i = 0; i < count; ++i) indices.push_back(u16[i]);
                    } else if (idxAccessor.componentType == fastgltf::ComponentType::UnsignedInt) {
                         const uint32_t* u32 = reinterpret_cast<const uint32_t*>(d);
                         for (size_t i = 0; i < count; ++i) indices.push_back(u32[i]);
                    } else if (idxAccessor.componentType == fastgltf::ComponentType::UnsignedByte) {
                         for (size_t i = 0; i < count; ++i) indices.push_back(d[i]);
                    }
                });
            } else {
                for (size_t i = 0; i < vertices.size(); i++) indices.push_back(static_cast<uint32_t>(i));
            }

            if (vertices.empty() || indices.empty()) return 0;

            auto meshAsset = std::make_unique<MeshAsset>();
            meshAsset->id = m_nextMeshId++;
            meshAsset->vertexCount = vertices.size();
            meshAsset->indexCount = indices.size();
            
            uploadBuffer(meshAsset->vertexBuffer, meshAsset->vertexAllocation, vertices.data(), vertices.size() * sizeof(renderer::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            uploadBuffer(meshAsset->indexBuffer, meshAsset->indexAllocation, indices.data(), indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

            if (rootMeshId == 0) rootMeshId = meshAsset->id;
            
            // --- Load Textures from glTF ---
            std::vector<uint32_t> textureIdMap(asset.textures.size(), 0);
            for (size_t t = 0; t < asset.textures.size(); ++t) {
                const auto& gltfTex = asset.textures[t];
                if (!gltfTex.imageIndex.has_value() || gltfTex.imageIndex.value() >= asset.images.size()) continue;

                const auto& image = asset.images[gltfTex.imageIndex.value()];
                const uint8_t* rawBytes = nullptr;
                size_t rawSize = 0;

                if (auto* bv = std::get_if<fastgltf::sources::BufferView>(&image.data)) {
                    if (bv->bufferViewIndex < asset.bufferViews.size()) {
                        const auto& bufferView = asset.bufferViews[bv->bufferViewIndex];
                        if (bufferView.bufferIndex < asset.buffers.size()) {
                            const auto& buf = asset.buffers[bufferView.bufferIndex];
                            const uint8_t* bufferStart = nullptr;
                            if (auto* vb = std::get_if<fastgltf::sources::Vector>(&buf.data)) {
                                bufferStart = reinterpret_cast<const uint8_t*>(vb->bytes.data());
                            } else if (auto* ab = std::get_if<fastgltf::sources::Array>(&buf.data)) {
                                bufferStart = reinterpret_cast<const uint8_t*>(ab->bytes.data());
                            } else if (auto* bvb = std::get_if<fastgltf::sources::ByteView>(&buf.data)) {
                                bufferStart = reinterpret_cast<const uint8_t*>(bvb->bytes.data());
                            }
                            if (bufferStart) {
                                rawBytes = bufferStart + bufferView.byteOffset;
                                rawSize = bufferView.byteLength;
                            }
                        }
                    }
                } else if (auto* vb = std::get_if<fastgltf::sources::Vector>(&image.data)) {
                    rawBytes = reinterpret_cast<const uint8_t*>(vb->bytes.data());
                    rawSize = vb->bytes.size();
                } else if (auto* bvb = std::get_if<fastgltf::sources::ByteView>(&image.data)) {
                    rawBytes = reinterpret_cast<const uint8_t*>(bvb->bytes.data());
                    rawSize = bvb->bytes.size();
                } else if (auto* ab = std::get_if<fastgltf::sources::Array>(&image.data)) {
                    rawBytes = reinterpret_cast<const uint8_t*>(ab->bytes.data());
                    rawSize = ab->bytes.size();
                }

                if (rawBytes && rawSize > 0) {
                    int w = 0, h = 0, comp = 0;
                    stbi_uc* pixels = stbi_load_from_memory(rawBytes, static_cast<int>(rawSize), &w, &h, &comp, 4);
                    if (pixels) {
                        auto texAsset = std::make_unique<TextureAsset>();
                        texAsset->id = m_nextTextureId++;
                        texAsset->width = static_cast<uint32_t>(w);
                        texAsset->height = static_cast<uint32_t>(h);
                        uploadTexture(texAsset->image, texAsset->allocation, texAsset->imageView, pixels, texAsset->width, texAsset->height, VK_FORMAT_R8G8B8A8_UNORM, true);
                        stbi_image_free(pixels);
                        textureIdMap[t] = texAsset->id;
                        m_textures.push_back(std::move(texAsset));
                        core::Logger::info("Uploaded glTF texture " + std::to_string(t) + " (" + std::to_string(w) + "x" + std::to_string(h) + ")");
                    }
                }
            }

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

                if (mat.pbrData.baseColorTexture.has_value()) {
                    size_t texIdx = mat.pbrData.baseColorTexture.value().textureIndex;
                    if (texIdx < textureIdMap.size()) {
                        materialAsset->albedoTextureId = textureIdMap[texIdx];
                    }
                }
                if (mat.normalTexture.has_value()) {
                    size_t texIdx = mat.normalTexture.value().textureIndex;
                    if (texIdx < textureIdMap.size()) {
                        materialAsset->normalTextureId = textureIdMap[texIdx];
                    }
                }
                if (mat.pbrData.metallicRoughnessTexture.has_value()) {
                    size_t texIdx = mat.pbrData.metallicRoughnessTexture.value().textureIndex;
                    if (texIdx < textureIdMap.size()) {
                        materialAsset->ormTextureId = textureIdMap[texIdx];
                    }
                }
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
            auto appendNodeInstances = [&](const fastgltf::Node& node) {
                if (!node.meshIndex.has_value() || node.meshIndex.value() >= importedMeshIds.size()) return;
                glm::vec3 translation(0.0f), scale(1.0f);
                glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
                if (const auto* trs = std::get_if<fastgltf::TRS>(&node.transform)) {
                    translation = glm::vec3(static_cast<float>(trs->translation[0]), static_cast<float>(trs->translation[1]), static_cast<float>(trs->translation[2]));
                    scale = glm::vec3(static_cast<float>(trs->scale[0]), static_cast<float>(trs->scale[1]), static_cast<float>(trs->scale[2]));
                    rotation = glm::quat(static_cast<float>(trs->rotation[3]), static_cast<float>(trs->rotation[0]), static_cast<float>(trs->rotation[1]), static_cast<float>(trs->rotation[2]));
                }
                for (uint32_t id : importedMeshIds[node.meshIndex.value()]) {
                    ImportedMeshInstance instance;
                    instance.meshId = id;
                    instance.name = node.name.empty() ? std::string("Mesh") : std::string(node.name);
                    instance.translation = translation;
                    instance.rotation = rotation;
                    instance.scale = scale;
                    m_lastImportedInstances.push_back(std::move(instance));
                }
            };
            std::function<void(size_t)> visitNode = [&](size_t nodeIndex) {
                if (nodeIndex >= asset.nodes.size()) return;
                const auto& node = asset.nodes[nodeIndex];
                appendNodeInstances(node);
                for (size_t childIndex : node.children) visitNode(childIndex);
            };
            if (!asset.scenes.empty()) {
                size_t sceneIndex = asset.defaultScene.value_or(0);
                if (sceneIndex >= asset.scenes.size()) sceneIndex = 0;
                for (size_t nodeIndex : asset.scenes[sceneIndex].nodeIndices) visitNode(nodeIndex);
            } else {
                // Some exporters omit scene declarations; visit only hierarchy roots.
                std::vector<bool> isChild(asset.nodes.size(), false);
                for (const auto& node : asset.nodes) {
                    for (size_t childIndex : node.children) if (childIndex < isChild.size()) isChild[childIndex] = true;
                }
                for (size_t i = 0; i < asset.nodes.size(); ++i) if (!isChild[i]) visitNode(i);
            }
            if (m_lastImportedInstances.empty()) {
                for (const auto& meshIds : importedMeshIds) for (uint32_t id : meshIds) {
                    ImportedMeshInstance instance; instance.meshId = id; instance.name = "Mesh";
                    m_lastImportedInstances.push_back(std::move(instance));
                }
            }
            if (rootMeshId == 0) {
                core::Logger::error("AssetManager: glTF contains no supported triangle primitives");
            }
            return rootMeshId;
        } catch (const std::exception& e) {
            core::Logger::error(std::string("GLTF asset import exception: ") + e.what());
            return 0;
        } catch (...) {
            core::Logger::error("Unknown GLTF asset import exception");
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
