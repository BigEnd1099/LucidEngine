#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include "renderer/VulkanContext.hpp"

namespace engine::renderer {

    // [CDLOD Terrain Chunk adapted from Unreal Engine 5.8 Landscape System]
    struct TerrainChunk {
        glm::vec2 minXZ{0.0f};
        glm::vec2 maxXZ{64.0f};
        float minHeight{0.0f};
        float maxHeight{100.0f};
        uint32_t lodLevel{0};
        float morphFactor{0.0f}; // [0..1] for smooth, continuous vertex morphing
    };

    class TerrainRenderer {
    public:
        void init(VulkanContext* context);
        void cleanup();
        void renderCDLOD(VkCommandBuffer cmd);

        // CDLOD morph calculation adapted from UE5 Landscape
        static float calculateMorphFactor(float distance, float lodStartDist, float lodEndDist);
        static uint32_t evaluateChunkLod(const TerrainChunk& chunk, const glm::vec3& cameraPos, float lodMultiplier);

    private:
        VulkanContext* m_context{nullptr};
        std::vector<TerrainChunk> m_terrainChunks;
    };
}
