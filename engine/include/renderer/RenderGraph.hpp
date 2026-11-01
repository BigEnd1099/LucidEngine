#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace engine::renderer {
    // A simplified abstraction for FrameGraph / RenderGraph
    // This allows automated resolution of VkImageMemoryBarrier2 and resource aliasing.
    
    struct RenderPassDef {
        std::string name;
        std::vector<std::string> inputs;
        std::vector<std::string> outputs;
        std::function<void(VkCommandBuffer)> executeCallback;
    };

    class RenderGraph {
    public:
        RenderGraph() = default;
        ~RenderGraph() = default;

        void addPass(const std::string& name, const std::vector<std::string>& inputs, const std::vector<std::string>& outputs, std::function<void(VkCommandBuffer)> callback);
        void compile(); // Resolves barriers and transitions
        void execute(VkCommandBuffer cmd);

    private:
        std::vector<RenderPassDef> m_passes;
        bool m_isCompiled{false};
    };
}
