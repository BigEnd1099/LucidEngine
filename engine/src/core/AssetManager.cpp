#include "core/AssetManager.hpp"
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <stdexcept>
#include "core/Logger.hpp"

namespace engine::core {
    AssetManager::AssetManager(renderer::VulkanContext* context) : m_context(context) {
        createFallbackMeshes();}

    AssetManager::~AssetManager() {
        for (auto& mesh : m_meshes) {
            if (mesh->vertexBuffer) vmaDestroyBuffer(m_context->getAllocator(), mesh->vertexBuffer, mesh->vertexAllocation);
            if (mesh->indexBuffer) vmaDestroyBuffer(m_context->getAllocator(), mesh->indexBuffer, mesh->indexAllocation);
        }
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

        vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_context->getGraphicsQueue());

        vkFreeCommandBuffers(m_context->getDevice(), m_context->getCommandPool(), 1, &commandBuffer);
        vmaDestroyBuffer(m_context->getAllocator(), stagingBuffer, stagingAllocation);
    }

    uint32_t AssetManager::loadGLTFFromMemory(const std::vector<uint8_t>& data) {
        if (data.empty()) return 0;
        try {
            fastgltf::Parser parser;
            auto buffer = fastgltf::GltfDataBuffer::FromBytes(reinterpret_cast<const std::byte*>(data.data()), data.size());
            if (!buffer) return 0;

            auto asset = parser.loadGltfBinary(buffer.get(), "", fastgltf::Options::DecomposeNodeMatrices);
            
            if (asset.error() != fastgltf::Error::None) {
                core::Logger::error("fastgltf error while parsing GLB");
                return 0;
            }

            uint32_t rootMeshId = 0;
            
            if (asset.get().meshes.empty()) return 0;
            const auto& gltfMesh = asset.get().meshes[0];
            if (gltfMesh.primitives.empty()) return 0;
            const auto& primitive = gltfMesh.primitives[0];
            
            auto posIt = primitive.findAttribute("POSITION");
            if (posIt == primitive.attributes.end()) {
                core::Logger::error("GLB primitive has no POSITION attribute");
                return 0;
            }

            if (posIt->accessorIndex >= asset.get().accessors.size()) return 0;
            auto& positionAccessor = asset.get().accessors[posIt->accessorIndex];
            if (positionAccessor.count == 0) return 0;

            std::vector<renderer::Vertex> vertices(positionAccessor.count);
            std::vector<uint32_t> indices;

            auto extractData = [&](const fastgltf::Accessor& accessor, auto&& callback) {
                if (!accessor.bufferViewIndex.has_value()) return;
                size_t bvIdx = accessor.bufferViewIndex.value();
                if (bvIdx >= asset.get().bufferViews.size()) return;
                auto& bufferView = asset.get().bufferViews[bvIdx];
                if (bufferView.bufferIndex >= asset.get().buffers.size()) return;
                auto& buf = asset.get().buffers[bufferView.bufferIndex];
                
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
                if (it->accessorIndex < asset.get().accessors.size()) {
                    auto& normalAccessor = asset.get().accessors[it->accessorIndex];
                    extractData(normalAccessor, [&](const uint8_t* d, size_t count) {
                        const float* f = reinterpret_cast<const float*>(d);
                        for (size_t i = 0; i < std::min(count, vertices.size()); ++i) {
                            vertices[i].normal = glm::vec3(f[i * 3 + 0], f[i * 3 + 1], f[i * 3 + 2]);
                        }
                    });
                }
            }
            
            if (auto it = primitive.findAttribute("TEXCOORD_0"); it != primitive.attributes.end()) {
                if (it->accessorIndex < asset.get().accessors.size()) {
                    auto& uvAccessor = asset.get().accessors[it->accessorIndex];
                    extractData(uvAccessor, [&](const uint8_t* d, size_t count) {
                        const float* f = reinterpret_cast<const float*>(d);
                        for (size_t i = 0; i < std::min(count, vertices.size()); ++i) {
                            vertices[i].texCoord = glm::vec2(f[i * 2 + 0], f[i * 2 + 1]);
                        }
                    });
                }
            }

            if (primitive.indicesAccessor.has_value() && primitive.indicesAccessor.value() < asset.get().accessors.size()) {
                auto& idxAccessor = asset.get().accessors[primitive.indicesAccessor.value()];
                extractData(idxAccessor, [&](const uint8_t* d, size_t count) {
                    if (idxAccessor.componentType == fastgltf::ComponentType::UnsignedShort) {
                         const uint16_t* u16 = reinterpret_cast<const uint16_t*>(d);
                         for (size_t i = 0; i < count; ++i) indices.push_back(u16[i]);
                    } else if (idxAccessor.componentType == fastgltf::ComponentType::UnsignedInt) {
                         const uint32_t* u32 = reinterpret_cast<const uint32_t*>(d);
                         for (size_t i = 0; i < count; ++i) indices.push_back(u32[i]);
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

            m_meshes.push_back(std::move(meshAsset));
            rootMeshId = m_nextMeshId - 1;
            
            if (primitive.materialIndex.has_value() && primitive.materialIndex.value() < asset.get().materials.size()) {
                auto& mat = asset.get().materials[primitive.materialIndex.value()];
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
                m_materials.push_back(std::move(materialAsset));
            }
            return rootMeshId;
        } catch (const std::exception& e) {
            core::Logger::error(std::string("GLTF parsing exception: ") + e.what());
            return 0;
        } catch (...) {
            core::Logger::error("Unknown GLTF parsing exception");
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
        std::vector<engine::renderer::Vertex> vertices = {
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
            // Top
            { {-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f} },
            { {-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },
            { { 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} },
            { { 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f} },
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
        std::vector<uint32_t> indices = {
            0,  1,  2,  2,  3,  0,
            4,  5,  6,  6,  7,  4,
            8,  9, 10, 10, 11,  8,
            12, 13, 14, 14, 15, 12,
            16, 17, 18, 18, 19, 16,
            20, 21, 22, 22, 23, 20
        };
        
        auto plane = std::make_unique<MeshAsset>();
        plane->id = m_nextMeshId++; // 1
        plane->vertexCount = vertices.size();
        plane->indexCount = indices.size();
        uploadBuffer(plane->vertexBuffer, plane->vertexAllocation, vertices.data(), vertices.size() * sizeof(engine::renderer::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        uploadBuffer(plane->indexBuffer, plane->indexAllocation, indices.data(), indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        m_meshes.push_back(std::move(plane));

        auto cube = std::make_unique<MeshAsset>();
        cube->id = m_nextMeshId++; // 2
        cube->vertexCount = vertices.size();
        cube->indexCount = indices.size();
        uploadBuffer(cube->vertexBuffer, cube->vertexAllocation, vertices.data(), vertices.size() * sizeof(engine::renderer::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        uploadBuffer(cube->indexBuffer, cube->indexAllocation, indices.data(), indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        m_meshes.push_back(std::move(cube));
    }
}
