#pragma once
#include <string>
#include <vector>
#include <memory>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "renderer/Vertex.hpp"
#include "renderer/VulkanContext.hpp"

namespace engine::core {
    
    struct TextureAsset {
        uint32_t id{0};
        VkImage image{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        VkImageView imageView{VK_NULL_HANDLE};
        uint32_t width{0};
        uint32_t height{0};
    };

    struct MaterialAsset {
        uint32_t id{0};
        glm::vec4 baseColorFactor{1.0f};
        float metallicFactor{1.0f};
        float roughnessFactor{1.0f};
        uint32_t albedoTextureId{0};
        uint32_t normalTextureId{0};
        uint32_t ormTextureId{0};
    };

    struct MeshAsset {
        uint32_t id{0};
        VkBuffer vertexBuffer{VK_NULL_HANDLE};
        VmaAllocation vertexAllocation{VK_NULL_HANDLE};
        VkBuffer indexBuffer{VK_NULL_HANDLE};
        VmaAllocation indexAllocation{VK_NULL_HANDLE};
        uint32_t indexCount{0};
        uint32_t vertexCount{0};
        uint32_t materialId{0};
    };

    class AssetManager {
    public:
        AssetManager(renderer::VulkanContext* context);
        void createFallbackMeshes();
        ~AssetManager();

        uint32_t loadGLTF(const std::string& path);
        uint32_t loadGLTFFromMemory(const std::vector<uint8_t>& data);
        
        uint32_t loadTextureFromFile(const std::string& path, bool generateMips = true);
        uint32_t createProceduralTexture(const std::string& presetName, const std::string& mapType);
        uint32_t createMaterial(const MaterialAsset& mat);
        void updateMaterial(uint32_t materialId, const MaterialAsset& mat);
        uint32_t applyPbrPreset(const std::string& presetName);
        
        MeshAsset* getMesh(uint32_t id);
        MaterialAsset* getMaterial(uint32_t id);
        TextureAsset* getTexture(uint32_t id);
        uint32_t getTextureCount() const { return m_nextTextureId - 1; }

        // Uploads buffer data to GPU (creates staging buffer, copies, then destroys staging)
        void uploadBuffer(VkBuffer& dstBuffer, VmaAllocation& dstAlloc, const void* data, size_t size, VkBufferUsageFlags usage);
        void uploadTexture(VkImage& dstImage, VmaAllocation& dstAlloc, VkImageView& dstView, const void* pixels, uint32_t width, uint32_t height, VkFormat format, bool generateMips);

    private:
        renderer::VulkanContext* m_context;
        std::vector<std::unique_ptr<MeshAsset>> m_meshes;
        std::vector<std::unique_ptr<MaterialAsset>> m_materials;
        std::vector<std::unique_ptr<TextureAsset>> m_textures;
        uint32_t m_nextMeshId{1};
        uint32_t m_nextMaterialId{1};
        uint32_t m_nextTextureId{1};
    };
}
