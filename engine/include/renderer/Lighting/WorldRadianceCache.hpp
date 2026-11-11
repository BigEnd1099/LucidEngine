#pragma once
#include "renderer/VulkanContext.hpp"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::renderer {

    // Exactly 32 bytes aligned to 16 bytes for std430 SSBO layout
    struct alignas(16) WorldRadianceCell {
        glm::vec4 irradiance{0.0f}; // Offset 0..15 (RGB = radiance, A = sampleCount / weight)
        uint32_t cellKey{0};        // Offset 16..19 (32-bit spatial key for atomicCompSwap in GLSL)
        uint32_t packedNormal{0};   // Offset 20..23 (Octahedral 16-bit + 16-bit flags)
        uint32_t lastSeenFrame{0};  // Offset 24..27 (TTL / frame index)
        uint32_t lockOrFlags{0};    // Offset 28..31 (Std430 alignment to 32 bytes)
    };
    static_assert(sizeof(WorldRadianceCell) == 32, "Std430 alignment mismatch!");

    class WorldRadianceCache {
    public:
        static constexpr uint32_t CACHE_CELL_COUNT = 524288; // 2^19 cells = 16 MB buffer
        static constexpr float DEFAULT_CELL_SIZE = 1.0f;     // 1 meter world cell grid

        WorldRadianceCache() = default;
        ~WorldRadianceCache();

        void init(VulkanContext* context, VkDescriptorPool descriptorPool, VkDescriptorSetLayout sceneSetLayout = VK_NULL_HANDLE);
        void cleanup();

        void dispatchUpdate(VkCommandBuffer cmd,
                            VkAccelerationStructureKHR tlas,
                            uint32_t frameCounter,
                            const glm::vec3& cameraPos,
                            const glm::vec3& sunDirection,
                            const glm::vec4& sunColor);

        void clear(VkCommandBuffer cmd);

        VkBuffer getCellBuffer() const { return m_cellBuffer; }
        VkDescriptorSet getDescriptorSet() const { return m_descriptorSet; }
        VkDescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }
        bool isInitialized() const { return m_initialized; }

        // CPU evaluation helpers for unit testing and algorithms
        static uint32_t hashWorldCell(const glm::vec3& pos, const glm::vec3& normal, float cellSize = DEFAULT_CELL_SIZE);
        static uint32_t packNormalOctahedral(const glm::vec3& n);
        static glm::vec3 unpackNormalOctahedral(uint32_t packedN);

    private:
        void createBuffers();
        void createPipeline();

        VulkanContext* m_context{nullptr};
        VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_sceneSetLayout{VK_NULL_HANDLE};
        VkBuffer m_cellBuffer{VK_NULL_HANDLE};
        VmaAllocation m_cellAlloc{VK_NULL_HANDLE};

        VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
        VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};
        VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
        VkPipeline m_updatePipeline{VK_NULL_HANDLE};
        bool m_initialized{false};
    };

} // namespace engine::renderer
