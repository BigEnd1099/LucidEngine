#pragma once
#include <string>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <vulkan/vulkan.h>
#include <glm/gtc/quaternion.hpp>
#include <vk_mem_alloc.h>
#include <fastgltf/core.hpp>
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
        uint32_t occlusionTextureId{0};
        uint32_t emissiveTextureId{0};
        glm::vec3 emissiveFactor{0.0f};
        float emissiveStrength{1.0f};
        float normalScale{1.0f};
        float occlusionStrength{1.0f};
        float alphaCutoff{0.5f};
        uint32_t alphaMode{0}; // 0 = OPAQUE, 1 = MASK, 2 = BLEND
        bool doubleSided{false};
        bool unlit{false};
    };

    struct ImportedMeshInstance {
        uint32_t meshId{0};
        std::string name;
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };

    struct ImportedMorphTarget {
        std::vector<glm::vec3> positionDeltas;
        std::vector<glm::vec3> normalDeltas;
        std::vector<glm::vec3> tangentDeltas;
    };

    struct ImportedSceneNode {
        size_t nodeIndex{0};
        int64_t parentNodeIndex{-1};
        std::string name;
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        uint32_t skinId{0};
        std::vector<float> morphWeights;
        std::vector<uint32_t> meshIds;
    };

    struct SkinAsset {
        uint32_t id{0};
        std::string name;
        int64_t skeletonNodeIndex{-1};
        std::vector<size_t> jointNodeIndices;
        std::vector<glm::mat4> inverseBindMatrices;
    };

    struct ImportedAnimationTrack {
        size_t nodeIndex{0};
        uint8_t path{0};          // fastgltf AnimationPath
        uint8_t interpolation{0}; // fastgltf AnimationInterpolation
        uint32_t componentsPerKey{0};
        std::vector<float> keyframeTimes;
        // Packed component values. CUBICSPLINE keeps in/value/out triplets per key.
        std::vector<float> values;
    };

    struct ImportedAnimationClip {
        std::string name;
        float duration{0.0f};
        std::vector<ImportedAnimationTrack> tracks;
    };

    // Kept in a separate stream so the established Vertex/RT packed layout is unchanged.
    // Each vertex carries up to eight glTF influences (JOINTS_0/1 + WEIGHTS_0/1).
    struct SkinVertexInfluence {
        glm::uvec4 joints0{0u};
        glm::vec4 weights0{1.0f, 0.0f, 0.0f, 0.0f};
        glm::uvec4 joints1{0u};
        glm::vec4 weights1{0.0f};
    };

    struct MeshAsset {
        uint32_t id{0};
        VkBuffer vertexBuffer{VK_NULL_HANDLE};
        VmaAllocation vertexAllocation{VK_NULL_HANDLE};
        VkBuffer indexBuffer{VK_NULL_HANDLE};
        VmaAllocation indexAllocation{VK_NULL_HANDLE};
        VkBuffer skinInfluenceBuffer{VK_NULL_HANDLE};
        VmaAllocation skinInfluenceAllocation{VK_NULL_HANDLE};
        uint32_t indexCount{0};
        uint32_t vertexCount{0};
        uint32_t materialId{0};
        bool hasSkinInfluences{false};
        std::vector<renderer::Vertex> vertices; // CPU base pose used by runtime deformation
        std::vector<SkinVertexInfluence> skinInfluences;
        std::vector<ImportedMorphTarget> morphTargets;
    };

    class AssetManager {
    public:
        AssetManager(renderer::VulkanContext* context);
        void createFallbackMeshes();
        ~AssetManager();

        uint32_t loadGLTF(const std::string& path);
        uint32_t loadGLTFFromMemory(const std::vector<uint8_t>& data);
        
        uint32_t loadTextureFromFile(const std::string& path, bool generateMips = true, bool srgb = false);
        uint32_t createProceduralTexture(const std::string& presetName, const std::string& mapType);
        uint32_t createMaterial(const MaterialAsset& mat);
        void updateMaterial(uint32_t materialId, const MaterialAsset& mat);
        uint32_t applyPbrPreset(const std::string& presetName);
        
        MeshAsset* getMesh(uint32_t id);
        MaterialAsset* getMaterial(uint32_t id);
        TextureAsset* getTexture(uint32_t id);
        uint32_t getTextureCount() const { return m_nextTextureId - 1; }
        const std::vector<ImportedMeshInstance>& getLastImportedInstances() const { return m_lastImportedInstances; }
        const std::vector<ImportedSceneNode>& getLastImportedNodes() const { return m_lastImportedNodes; }
        const std::vector<SkinAsset>& getLastImportedSkins() const { return m_lastImportedSkins; }
        const std::vector<ImportedAnimationClip>& getLastImportedAnimations() const { return m_lastImportedAnimations; }

        // Uploads buffer data to GPU (creates staging buffer, copies, then destroys staging)
        void uploadBuffer(VkBuffer& dstBuffer, VmaAllocation& dstAlloc, const void* data, size_t size, VkBufferUsageFlags usage);
        void uploadTexture(VkImage& dstImage, VmaAllocation& dstAlloc, VkImageView& dstView, const void* pixels, uint32_t width, uint32_t height, VkFormat format, bool generateMips);

    private:
        uint32_t loadGLTFAsset(fastgltf::Asset& asset);
        renderer::VulkanContext* m_context;
        std::vector<std::unique_ptr<MeshAsset>> m_meshes;
        std::vector<std::unique_ptr<MaterialAsset>> m_materials;
        std::vector<std::unique_ptr<TextureAsset>> m_textures;
        std::vector<ImportedMeshInstance> m_lastImportedInstances;
        std::vector<ImportedSceneNode> m_lastImportedNodes;
        std::vector<SkinAsset> m_lastImportedSkins;
        std::vector<ImportedAnimationClip> m_lastImportedAnimations;
        uint32_t m_nextMeshId{1};
        uint32_t m_nextMaterialId{1};
        uint32_t m_nextTextureId{1};
    };
}
