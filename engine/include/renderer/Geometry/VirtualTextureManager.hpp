#pragma once
#include <vulkan/vulkan.h>
#include "renderer/VulkanContext.hpp"

namespace engine::renderer {
    class VirtualTextureManager {
    public:
        void init(VulkanContext* context);
        void cleanup();
        void updatePageTable(VkCommandBuffer cmd);
    private:
        VulkanContext* m_context;
    };
}
