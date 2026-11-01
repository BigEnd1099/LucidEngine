#include "renderer/AccelerationStructure.hpp"
#include "core/Logger.hpp"

namespace engine::renderer {
    void AccelerationStructure::init(VulkanContext* context) {
        m_context = context;
        core::Logger::info("AccelerationStructure (TLAS/BLAS) manager initialized.");
    }
    
    void AccelerationStructure::cleanup() {}
    
    void AccelerationStructure::buildBLAS(VkCommandBuffer cmd, VkBuffer vertexBuffer, uint32_t vertexCount, VkBuffer indexBuffer, uint32_t indexCount) {
        // vkCmdBuildAccelerationStructuresKHR
    }
    
    void AccelerationStructure::buildTLAS(VkCommandBuffer cmd) {
        // vkCmdBuildAccelerationStructuresKHR
    }
}
