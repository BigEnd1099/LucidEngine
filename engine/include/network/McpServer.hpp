#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include "network/McpToolsetRegistry.hpp"

namespace engine::core { class Engine; }

namespace engine::network::mcp {

    class McpServer {
    public:
        McpServer(core::Engine* engine, AgentAccessTier tier = AgentAccessTier::LiveEdit);
        void processStdio(); // Obsluga standardowego wejscia/wyjscia pod MCP stdio transport
        void setAccessTier(AgentAccessTier tier) { m_tier = tier; }
        AgentAccessTier getAccessTier() const { return m_tier; }

    private:
        core::Engine* m_engine;
        AgentAccessTier m_tier{AgentAccessTier::LiveEdit};

        void sendResponse(const nlohmann::json& id, const nlohmann::json& result);
        void sendError(const nlohmann::json& id, int code, const std::string& message);
    };

} // namespace engine::network::mcp
