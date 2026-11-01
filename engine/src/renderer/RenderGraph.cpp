#include "renderer/RenderGraph.hpp"
#include "core/Logger.hpp"

namespace engine::renderer {
    void RenderGraph::addPass(const std::string& name, const std::vector<std::string>& inputs, const std::vector<std::string>& outputs, std::function<void(VkCommandBuffer)> callback) {
        m_passes.push_back({name, inputs, outputs, callback});
        m_isCompiled = false;
    }

    void RenderGraph::compile() {
        // In a full implementation, this will build a Directed Acyclic Graph (DAG),
        // determine the first and last usage of every resource (image/buffer),
        // insert VkImageMemoryBarrier2 transitions between passes automatically,
        // and reuse transient memory allocations.
        
        core::Logger::info("RenderGraph compiled with " + std::to_string(m_passes.size()) + " logical passes.");
        m_isCompiled = true;
    }

    void RenderGraph::execute(VkCommandBuffer cmd) {
        if (!m_isCompiled) {
            compile();
        }

        for (const auto& pass : m_passes) {
            // Here we would insert automated barriers before executing the pass
            // vkCmdPipelineBarrier2(...)
            
            // Execute the actual render logic (vkCmdBeginRendering, draws, etc)
            pass.executeCallback(cmd);
        }
    }
}
