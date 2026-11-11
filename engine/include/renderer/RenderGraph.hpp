#pragma once
#include <vulkan/vulkan.h>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace engine::renderer {

    struct PassTelemetry {
        std::string passName;
        double durationMs{0.0};
        uint32_t drawCount{0};
    };

    // A pass declares logical resource reads/writes. The graph compiles those
    // declarations into a deterministic execution order; Vulkan barriers remain
    // explicit until resource handles and stage/access metadata are part of the API.
    struct RenderPassDef {
        std::string name;
        std::vector<std::string> inputs;
        std::vector<std::string> outputs;
        std::function<void(VkCommandBuffer)> executeCallback;
        std::vector<std::string> dependsOn;
    };

    class RenderGraph {
    public:
        RenderGraph() = default;
        ~RenderGraph() = default;

        void addPass(
            const std::string& name,
            const std::vector<std::string>& inputs,
            const std::vector<std::string>& outputs,
            std::function<void(VkCommandBuffer)> callback,
            const std::vector<std::string>& dependsOn = {});

        bool compile();
        void execute(VkCommandBuffer cmd);

        bool isCompiled() const { return m_isCompiled; }
        const std::string& getLastError() const { return m_lastError; }
        const std::vector<size_t>& getExecutionOrder() const { return m_executionOrder; }
        const std::vector<RenderPassDef>& getPasses() const { return m_passes; }
        const std::vector<PassTelemetry>& getPassTelemetry() const { return m_passTelemetry; }
        nlohmann::json getPassTelemetryJson() const;
        void clear();

    private:
        std::vector<RenderPassDef> m_passes;
        std::vector<size_t> m_executionOrder;
        std::vector<PassTelemetry> m_passTelemetry;
        std::string m_lastError;
        bool m_isCompiled{false};
    };
}
