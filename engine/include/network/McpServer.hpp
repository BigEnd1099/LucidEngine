#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <unordered_map>

namespace engine::core { class Engine; }

namespace engine::network::mcp {

    struct McpTool {
        std::string name;
        std::string description;
        nlohmann::json inputSchema;
        std::function<nlohmann::json(const nlohmann::json&)> handler;
    };

    class McpServer {
    public:
        McpServer(core::Engine* engine);
        void processStdio(); // Obsługa standardowego wejścia/wyjścia pod MCP stdio transport
        void registerEngineTools();

    private:
        core::Engine* m_engine;
        std::unordered_map<std::string, McpTool> m_tools;

        void sendResponse(const nlohmann::json& id, const nlohmann::json& result);
        void sendError(const nlohmann::json& id, int code, const std::string& message);
    };

} // namespace engine::network::mcp
