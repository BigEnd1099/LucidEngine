#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

namespace engine::renderer {

    // [UE5 Virtual Shadow Maps Page Table Architecture]
    struct VsmPageTableEntry {
        uint32_t physicalPageIndex : 24; // Index into physical tile pool
        uint32_t isAllocated : 1;
        uint32_t isDirty : 1;
        uint32_t mipLevel : 6;
    };

    struct VsmProjectionData {
        glm::mat4 lightViewProj;
        glm::vec4 clipBounds;
        uint32_t virtualResolution{16384};
        uint32_t pageSize{128};
        uint32_t pageGridDim{128}; // 16384 / 128 = 128
        uint32_t maxPhysicalPages{2048};
    };

    class VirtualShadowMap {
    public:
        void init(VkDevice device, class VulkanContext* context);
        void cleanup();
        void dispatchPageAllocation(VkCommandBuffer cmd);
        void renderShadows(VkCommandBuffer cmd);

        // UE5 Page lookup & address calculation
        uint32_t getPageIndex(uint32_t x, uint32_t y, uint32_t mip) const;
        bool allocatePage(uint32_t virtualX, uint32_t virtualY, uint32_t mip);
        const VsmProjectionData& getProjectionData() const { return m_projectionData; }

    private:
        VkDevice m_device{VK_NULL_HANDLE};
        VulkanContext* m_context{nullptr};
        VsmProjectionData m_projectionData;

        std::vector<VsmPageTableEntry> m_cpuPageTable;
        uint32_t m_nextPhysicalPage{0};

        VkBuffer m_pageTableBuffer{VK_NULL_HANDLE};
        VkBuffer m_physicalPagesBuffer{VK_NULL_HANDLE};
    };
}
