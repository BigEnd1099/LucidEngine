#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include <cstdint>
#include "ecs/SceneSnapshot.hpp"

namespace engine::core { class Engine; }

namespace engine::network::mcp {

    enum class AgentAccessTier : uint8_t {
        ReadOnly = 0,     // Read queries, telemetria, knowledge base
        StageChanges = 1, // Zgłaszanie zmian do bufora staging
        LiveEdit = 2,     // Bezpośrednie modyfikacje ECS z automatycznym snapshotem RAM
        FullControl = 3   // Skrypty, zapis projektów na dysk, reload potoków
    };

    inline const char* agentAccessTierToString(AgentAccessTier tier) {
        switch (tier) {
            case AgentAccessTier::ReadOnly: return "ReadOnly";
            case AgentAccessTier::StageChanges: return "StageChanges";
            case AgentAccessTier::LiveEdit: return "LiveEdit";
            case AgentAccessTier::FullControl: return "FullControl";
        }
        return "Unknown";
    }

    inline AgentAccessTier parseAgentAccessTier(const std::string& str) {
        if (str == "ReadOnly" || str == "readonly" || str == "0") return AgentAccessTier::ReadOnly;
        if (str == "StageChanges" || str == "stage" || str == "1") return AgentAccessTier::StageChanges;
        if (str == "LiveEdit" || str == "edit" || str == "2") return AgentAccessTier::LiveEdit;
        if (str == "FullControl" || str == "full" || str == "admin" || str == "3") return AgentAccessTier::FullControl;
        return AgentAccessTier::LiveEdit; // Default
    }

    struct McpToolDefinition {
        std::string name;
        std::string domain; // "ecs", "renderer", "profiler", "shader", "script", "mcp"
        std::string description;
        AgentAccessTier requiredTier{AgentAccessTier::ReadOnly};
        nlohmann::json inputSchema;
        std::function<nlohmann::json(core::Engine*, const nlohmann::json&)> handler;
    };

    class McpToolsetRegistry {
    public:
        static McpToolsetRegistry& get();

        void registerTool(const McpToolDefinition& tool);
        bool hasTool(const std::string& name) const;
        const McpToolDefinition* getTool(const std::string& name) const;

        nlohmann::json invoke(
            core::Engine* engine,
            const std::string& name,
            const nlohmann::json& args,
            AgentAccessTier clientTier);

        nlohmann::json getToolsManifest(AgentAccessTier clientTier) const;
        
        bool rollbackLastTransaction(core::Engine* engine);
        bool hasSnapshot() const { return !m_lastSnapshot.empty(); }
        void captureSnapshot(core::Engine* engine);

        void registerDefaultEngineTools();

    private:
        McpToolsetRegistry();
        std::unordered_map<std::string, McpToolDefinition> m_tools;
        std::vector<ecs::EntityBinaryRecord> m_lastSnapshot;
    };

} // namespace engine::network::mcp
