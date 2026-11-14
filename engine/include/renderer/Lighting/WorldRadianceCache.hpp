#pragma once
#include "renderer/VulkanContext.hpp"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace engine::renderer {

    // Exactly 32 bytes aligned to 16 bytes for std430 SSBO layout
    struct alignas(16) WorldRadianceCell {
        glm::vec4 irradiance{0.0f}; // Offset 0..15 (RGB = radiance, A = sampleCount / weight)
        uint32_t cellKey{0};        // Offset 16..19 (full 32-bit spatial key, not slot index)
        uint32_t packedNormal{0};   // Offset 20..23 (Octahedral 16-bit + 16-bit flags)
        uint32_t lastSeenFrame{0};  // Offset 24..27 (TTL / frame index)
        uint32_t lockOrFlags{0};    // Offset 28..31 (atomic writer lock / flags)
    };
    static_assert(sizeof(WorldRadianceCell) == 32, "Std430 alignment mismatch!");
    static_assert(offsetof(WorldRadianceCell, irradiance) == 0, "WorldRadianceCell irradiance offset mismatch!");
    static_assert(offsetof(WorldRadianceCell, cellKey) == 16, "WorldRadianceCell cellKey offset mismatch!");
    static_assert(offsetof(WorldRadianceCell, packedNormal) == 20, "WorldRadianceCell packedNormal offset mismatch!");
    static_assert(offsetof(WorldRadianceCell, lastSeenFrame) == 24, "WorldRadianceCell lastSeenFrame offset mismatch!");
    static_assert(offsetof(WorldRadianceCell, lockOrFlags) == 28, "WorldRadianceCell lockOrFlags offset mismatch!");

    class WorldRadianceCache {
    public:
        static constexpr uint32_t CACHE_CELL_COUNT = 524288; // 2^19 cells = 16 MB buffer
        static constexpr float DEFAULT_CELL_SIZE = 0.65f;    // 0.65 meter world cell grid

        WorldRadianceCache() = default;
        ~WorldRadianceCache();

        void init(VulkanContext* context, VkDescriptorPool descriptorPool, VkDescriptorSetLayout sceneSetLayout = VK_NULL_HANDLE);
        void cleanup();

        // Returns true when the update dispatch was recorded. Optional timestamp
        // queries 11/12 bracket only the compute dispatch, excluding cache clears/barriers.
        bool dispatchUpdate(VkCommandBuffer cmd,
                            VkDescriptorSet sceneSet,
                            VkAccelerationStructureKHR tlas,
                            uint32_t frameCounter,
                            const glm::vec3& cameraPos,
                            const glm::vec3& sunDirection,
                            const glm::vec4& sunColor,
                            VkQueryPool timingQueryPool = VK_NULL_HANDLE,
                            float baseCellSize = DEFAULT_CELL_SIZE,
                            float bounceIntensity = 1.0f,
                            uint32_t cellBatchCount = 128u,
                            uint32_t featureFlags = 0x3Fu);

        void clear(VkCommandBuffer cmd);

        VkBuffer getCellBuffer() const { return m_filteredCellBuffer != VK_NULL_HANDLE ? m_filteredCellBuffer : m_cellBuffer; }
        VkBuffer getUnfilteredCellBuffer() const { return m_cellBuffer; }
        VkDescriptorSet getDescriptorSet() const { return m_descriptorSet; }
        VkDescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }
        bool isInitialized() const { return m_initialized; }

        // CPU evaluation helpers for unit testing and algorithms
        static inline uint32_t getCascadeIndex(const glm::vec3& worldPos, const glm::vec3& cameraPos) {
            float dist = glm::length(worldPos - cameraPos);
            // Cascade thresholds: [0, 2.5m) -> 0 (0.20m), [2.5m, 5.0m) -> 1 (0.40m), [5.0m, 10.0m) -> 2 (0.80m), [10.0m, inf) -> 3 (1.60m)
            if (dist < 2.5f) return 0u;
            return static_cast<uint32_t>(std::clamp(std::floor(std::log2(dist / 2.5f)) + 1.0f, 0.0f, 3.0f));
        }

        static inline float getCascadeCellSize(uint32_t cascade) {
            return 0.20f * static_cast<float>(1u << cascade); // 0.20, 0.40, 0.80, 1.60
        }

        static inline uint32_t computeWorldCellKey(const glm::vec3& pos, const glm::vec3& normal, float cellSize = DEFAULT_CELL_SIZE, uint32_t cascade = 0u) {
            const glm::vec3 n = glm::normalize(normal);
            const glm::vec3 biasedPos = pos + n * (cellSize * 0.25f);
            glm::ivec3 cellCoord = glm::ivec3(glm::floor(biasedPos / cellSize));
            uint32_t normIdx = 0u;
            if (std::abs(normal.x) > std::abs(normal.y) && std::abs(normal.x) > std::abs(normal.z)) {
                normIdx = normal.x > 0.0f ? 0u : 1u;
            } else if (std::abs(normal.y) > std::abs(normal.z)) {
                normIdx = normal.y > 0.0f ? 2u : 3u;
            } else {
                normIdx = normal.z > 0.0f ? 4u : 5u;
            }
            uint32_t h = static_cast<uint32_t>(cellCoord.x) * 73856093u ^
                         static_cast<uint32_t>(cellCoord.y) * 19349663u ^
                         static_cast<uint32_t>(cellCoord.z) * 83492791u ^
                         normIdx * 2654435761u ^
                         (cascade * 1640531527u);
            return h ^ (h >> 16u);
        }

        static uint32_t hashWorldCell(const glm::vec3& pos, const glm::vec3& normal, float cellSize = DEFAULT_CELL_SIZE, uint32_t cascade = 0u);
        static uint32_t packNormalOctahedral(const glm::vec3& n);
        static glm::vec3 unpackNormalOctahedral(uint32_t packedN);

        static inline uint32_t packCellCoord(const glm::ivec3& coord, uint32_t cascade) {
            uint32_t ux = static_cast<uint32_t>(std::clamp(coord.x + 512, 0, 1023));
            uint32_t uz = static_cast<uint32_t>(std::clamp(coord.z + 512, 0, 1023));
            uint32_t uy = static_cast<uint32_t>(std::clamp(coord.y + 256, 0, 511));
            uint32_t uc = cascade & 3u;
            return ux | (uz << 10u) | (uy << 20u) | (uc << 29u);
        }

        static inline void unpackCellCoord(uint32_t packed, glm::ivec3& coord, uint32_t& cascade) {
            int x = static_cast<int>(packed & 1023u) - 512;
            int z = static_cast<int>((packed >> 10u) & 1023u) - 512;
            int y = static_cast<int>((packed >> 20u) & 511u) - 256;
            cascade = (packed >> 29u) & 3u;
            coord = glm::ivec3(x, y, z);
        }

        static inline glm::vec3 getStratifiedHemisphereDirection(const glm::vec3& normal, uint32_t laneIdx, const glm::vec2& jitter = glm::vec2(0.5f, 0.5f)) {
            uint32_t col = laneIdx & 7u;  // 8 azimuth columns
            uint32_t row = laneIdx >> 3u; // 4 elevation rings
            float uElevation = (static_cast<float>(row) + jitter.y) / 4.0f;
            float uAzimuth = (static_cast<float>(col) + jitter.x) / 8.0f;

            float r = std::sqrt(std::clamp(uElevation, 0.0f, 1.0f));
            float phi = 2.0f * 3.14159265358979323846f * uAzimuth;

            glm::vec3 n = glm::normalize(normal);
            float s = n.z >= 0.0f ? 1.0f : -1.0f;
            float a = -1.0f / (s + n.z);
            float c = n.x * n.y * a;
            glm::vec3 t(1.0f + s * n.x * n.x * a, s * c, -s * n.x);
            glm::vec3 b(c, s + n.y * n.y * a, -n.y);

            return glm::normalize(t * (r * std::cos(phi)) + b * (r * std::sin(phi)) + n * std::sqrt(std::max(1.0f - uElevation, 0.0f)));
        }

        static inline float evaluateSpatialFilterWeight(float distanceOnPlane, float sigma = 1.0f) {
            return std::exp(-0.5f * (distanceOnPlane * distanceOnPlane) / (sigma * sigma));
        }

        static inline bool isCoplanarNormal(const glm::vec3& n1, const glm::vec3& n2, float threshold = 0.8f) {
            return glm::dot(glm::normalize(n1), glm::normalize(n2)) > threshold;
        }

    private:
        void createBuffers();
        void createPipeline();

        VulkanContext* m_context{nullptr};
        VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_sceneSetLayout{VK_NULL_HANDLE};
        VkBuffer m_cellBuffer{VK_NULL_HANDLE};
        VmaAllocation m_cellAlloc{VK_NULL_HANDLE};
        VkBuffer m_filteredCellBuffer{VK_NULL_HANDLE};
        VmaAllocation m_filteredCellAlloc{VK_NULL_HANDLE};

        VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
        VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};
        VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_updatePipeline{VK_NULL_HANDLE};
        VkPipeline m_filterPipeline{VK_NULL_HANDLE};
        bool m_initialized{false};
        bool m_needsClear{true};
    };

} // namespace engine::renderer
