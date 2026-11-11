#include "renderer/Geometry/VirtualTextureManager.hpp"
#include "core/Logger.hpp"

namespace engine::renderer {
    void VirtualTextureManager::init(VulkanContext* context) {
        m_context = context;
        core::Logger::info("VirtualTextureManager (Sparse Textures) initialized.");
    }
    void VirtualTextureManager::cleanup() {}
    void VirtualTextureManager::updatePageTable(VkCommandBuffer) {}
}
