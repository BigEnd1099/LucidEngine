#pragma once
#include <nlohmann/json.hpp>

namespace engine::core { class Engine; }

namespace engine::network::mcp {

    class McpKnowledgeBase {
    public:
        static nlohmann::json buildKnowledgeGraph(core::Engine* engine);
        static nlohmann::json getComponentSchemas();
        static nlohmann::json getShaderUniformsSchema();
    };

} // namespace engine::network::mcp
