#include "renderer/Terrain/TerrainRenderer.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <cmath>

namespace engine::renderer {

    void TerrainRenderer::init(VulkanContext* context) {
        m_context = context;
        core::Logger::info("TerrainRenderer (UE5-adapted Continuous Distance-Dependent LOD / CDLOD) initialized.");
    }

    void TerrainRenderer::cleanup() {
        m_terrainChunks.clear();
    }

    void TerrainRenderer::renderCDLOD(VkCommandBuffer) {
        // MultiDrawIndirect for quadtree terrain grid chunks
    }

    // [UE5 Landscape CDLOD: Vertex Morph Factor Calculation]
    float TerrainRenderer::calculateMorphFactor(float distance, float lodStartDist, float lodEndDist) {
        if (distance <= lodStartDist) return 0.0f;
        if (distance >= lodEndDist) return 1.0f;
        return (distance - lodStartDist) / std::max(1e-3f, lodEndDist - lodStartDist);
    }

    // [UE5 Landscape: Chunk LOD Evaluation based on distance and camera position]
    uint32_t TerrainRenderer::evaluateChunkLod(const TerrainChunk& chunk, const glm::vec3& cameraPos, float lodMultiplier) {
        glm::vec2 chunkCenter = (chunk.minXZ + chunk.maxXZ) * 0.5f;
        float distToChunk = glm::length(chunkCenter - glm::vec2(cameraPos.x, cameraPos.z));
        
        float baseRange = 64.0f * lodMultiplier;
        uint32_t lod = static_cast<uint32_t>(distToChunk / baseRange);
        return std::min(lod, 6u); // Max 7 LOD levels (0 to 6)
    }
}
