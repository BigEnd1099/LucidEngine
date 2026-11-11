#include "renderer/Lighting/VirtualShadowMap.hpp"
#include "core/Logger.hpp"

namespace engine::renderer {

    void VirtualShadowMap::init(VkDevice device, VulkanContext* context) {
        m_device = device;
        m_context = context;
        
        // Initialize 128x128 page grid (16,384 virtual pages per 16k map)
        uint32_t totalVirtualPages = m_projectionData.pageGridDim * m_projectionData.pageGridDim;
        m_cpuPageTable.resize(totalVirtualPages);

        for (auto& entry : m_cpuPageTable) {
            entry.physicalPageIndex = 0x00FFFFFF; // Invalid
            entry.isAllocated = 0;
            entry.isDirty = 0;
            entry.mipLevel = 0;
        }

        m_nextPhysicalPage = 0;
        core::Logger::info("VirtualShadowMap (VSM) initialized with 16k x 16k virtual resolution and " + 
                           std::to_string(totalVirtualPages) + " page entries (UE5 layout).");
    }

    void VirtualShadowMap::cleanup() {
        m_cpuPageTable.clear();
    }

    uint32_t VirtualShadowMap::getPageIndex(uint32_t x, uint32_t y, uint32_t mip) const {
        uint32_t gridDim = m_projectionData.pageGridDim >> mip;
        if (gridDim == 0) gridDim = 1;
        return (y * gridDim + x);
    }

    bool VirtualShadowMap::allocatePage(uint32_t virtualX, uint32_t virtualY, uint32_t mip) {
        uint32_t idx = getPageIndex(virtualX, virtualY, mip);
        if (idx >= m_cpuPageTable.size()) return false;

        if (m_cpuPageTable[idx].isAllocated) return true;

        if (m_nextPhysicalPage < m_projectionData.maxPhysicalPages) {
            m_cpuPageTable[idx].physicalPageIndex = m_nextPhysicalPage++;
            m_cpuPageTable[idx].isAllocated = 1;
            m_cpuPageTable[idx].isDirty = 1;
            m_cpuPageTable[idx].mipLevel = mip;
            return true;
        }

        return false;
    }

    void VirtualShadowMap::dispatchPageAllocation(VkCommandBuffer) {
        // GPU Compute dispatch to analyze depth pyramid and mark needed VSM pages
    }

    void VirtualShadowMap::renderShadows(VkCommandBuffer) {
        // Rasterize depth into allocated physical tiles in the physical tile atlas
    }
}
