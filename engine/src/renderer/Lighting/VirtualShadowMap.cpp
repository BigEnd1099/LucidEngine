#include "renderer/Lighting/VirtualShadowMap.hpp"
#include "core/Logger.hpp"

namespace engine::renderer {

    void VirtualShadowMap::init(VkDevice device, VulkanContext* context) {
        m_device = device;
        m_context = context;
        
        // Initialize pyramidal page grid across all mip levels (128x128 -> 1x1)
        uint32_t totalVirtualPages = 0;
        uint32_t dim = m_projectionData.pageGridDim;
        while (dim > 0) {
            totalVirtualPages += dim * dim;
            if (dim == 1) break;
            dim >>= 1;
        }
        m_cpuPageTable.resize(totalVirtualPages);

        for (auto& entry : m_cpuPageTable) {
            entry.physicalPageIndex = 0x00FFFFFF; // Invalid
            entry.isAllocated = 0;
            entry.isDirty = 0;
            entry.mipLevel = 0;
        }

        m_nextPhysicalPage = 0;
        core::Logger::info("VirtualShadowMap (VSM) initialized with 16k x 16k virtual resolution and " + 
                           std::to_string(totalVirtualPages) + " page entries across all mip levels (UE5 layout).");
    }

    void VirtualShadowMap::cleanup() {
        m_cpuPageTable.clear();
    }

    uint32_t VirtualShadowMap::getPageIndex(uint32_t x, uint32_t y, uint32_t mip) const {
        uint32_t offset = 0;
        uint32_t curDim = m_projectionData.pageGridDim;
        for (uint32_t m = 0; m < mip; ++m) {
            offset += curDim * curDim;
            curDim = (curDim > 1) ? (curDim >> 1) : 1;
        }
        uint32_t gridDim = m_projectionData.pageGridDim >> mip;
        if (gridDim == 0) gridDim = 1;
        return offset + (y * gridDim + x);
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
