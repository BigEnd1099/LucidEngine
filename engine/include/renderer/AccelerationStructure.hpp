#pragma once
#include <vulkan/vulkan.h>
#include "renderer/VulkanContext.hpp"

namespace engine::renderer {
    class AccelerationStructure {
    public:
        void init(VulkanContext* context);
        void cleanup();
        void buildBLAS(VkCommandBuffer cmd, VkBuffer vertexBuffer, uint32_t vertexCount, VkBuffer indexBuffer, uint32_t indexCount);
        void buildTLAS(VkCommandBuffer cmd);
        
    private:
        VulkanContext* m_context;
        VkAccelerationStructureKHR m_tlas{VK_NULL_HANDLE};
        VkBuffer m_tlasBuffer{VK_NULL_HANDLE};
        // VMA allocations would be here for TLAS and BLAS buffers
    };
}
