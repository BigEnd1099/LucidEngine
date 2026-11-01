#pragma once
#include <string>
#include <vector>
#include <memory>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "renderer/Vertex.hpp"
#include "renderer/VulkanContext.hpp"

namespace engine::core {
    
    struct MeshAsset {
        uint32_t id;
        VkBuffer vertexBuffer{VK_NULL_HANDLE};
        VmaAllocation vertexAllocation{VK_NULL_HANDLE};
        VkBuffer indexBuffer{VK_NULL_HANDLE};
        VmaAllocation indexAllocation{VK_NULL_HANDLE};
        uint32_t indexCount{0};
        uint32_t vertexCount{0};
    };

    struct MaterialAsset {
        uint32_t id;
        glm::vec4 baseColorFactor{1.0f};
        float metallicFactor{1.0f};
        float roughnessFactor{1.0f};
        // We'll add textures (albedo, normal, orm) here later
    };

    class AssetManager {
    public:
        AssetManager(renderer::VulkanContext* context);
        void createFallbackMeshes();
        ~AssetManager();

        uint32_t loadGLTF(const std::string& path);
        uint32_t loadGLTFFromMemory(const std::vector<uint8_t>& data);
        
        MeshAsset* getMesh(uint32_t id);
        MaterialAsset* getMaterial(uint32_t id);

        // Uploads buffer data to GPU (creates staging buffer, copies, then destroys staging)
        void uploadBuffer(VkBuffer& dstBuffer, VmaAllocation& dstAlloc, const void* data, size_t size, VkBufferUsageFlags usage);

    private:
        renderer::VulkanContext* m_context;
        std::vector<std::unique_ptr<MeshAsset>> m_meshes;
        std::vector<std::unique_ptr<MaterialAsset>> m_materials;
        uint32_t m_nextMeshId{1};
        uint32_t m_nextMaterialId{1};
    };
}
