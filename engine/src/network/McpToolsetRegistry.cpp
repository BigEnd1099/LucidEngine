#include "network/McpToolsetRegistry.hpp"
#include "network/McpKnowledgeBase.hpp"
#include "renderer/SpatialAnnotation.hpp"
#include "renderer/VulkanRenderer.hpp"
#include "core/Engine.hpp"
#include "core/Logger.hpp"
#include "core/Profiler.hpp"

namespace engine::network::mcp {

    McpToolsetRegistry& McpToolsetRegistry::get() {
        static McpToolsetRegistry instance;
        return instance;
    }

    McpToolsetRegistry::McpToolsetRegistry() {
        registerDefaultEngineTools();
    }

    void McpToolsetRegistry::registerTool(const McpToolDefinition& tool) {
        m_tools[tool.name] = tool;
    }

    bool McpToolsetRegistry::hasTool(const std::string& name) const {
        return m_tools.find(name) != m_tools.end();
    }

    const McpToolDefinition* McpToolsetRegistry::getTool(const std::string& name) const {
        auto it = m_tools.find(name);
        return (it != m_tools.end()) ? &it->second : nullptr;
    }

    void McpToolsetRegistry::captureSnapshot(core::Engine* engine) {
        if (!engine || !engine->getRegistry()) return;
        m_lastSnapshot = ecs::SceneSnapshot::capture(*engine->getRegistry());
    }

    bool McpToolsetRegistry::rollbackLastTransaction(core::Engine* engine) {
        if (!engine || !engine->getRegistry() || m_lastSnapshot.empty()) return false;
        ecs::SceneSnapshot::restore(*engine->getRegistry(), engine->getPhysicsContext(), m_lastSnapshot);
        return true;
    }

    nlohmann::json McpToolsetRegistry::invoke(
        core::Engine* engine,
        const std::string& name,
        const nlohmann::json& args,
        AgentAccessTier clientTier) {
        
        auto it = m_tools.find(name);
        if (it == m_tools.end()) {
            return {
                {"error", {
                    {"code", -32601},
                    {"message", "Tool not found: " + name}
                }}
            };
        }

        const auto& tool = it->second;
        if (static_cast<uint8_t>(clientTier) < static_cast<uint8_t>(tool.requiredTier)) {
            return {
                {"error", {
                    {"code", -32001},
                    {"message", std::string("Permission Denied: Tool '") + name + "' requires tier " +
                                agentAccessTierToString(tool.requiredTier) + " but client has " +
                                agentAccessTierToString(clientTier)}
                }}
            };
        }

        // Automatic snapshot before executing live mutation tools
        if (tool.requiredTier >= AgentAccessTier::LiveEdit && (tool.domain == "ecs" || name == "ecs.spawn_entity" || name == "ecs.destroy_entity")) {
            captureSnapshot(engine);
        }

        try {
            nlohmann::json res = tool.handler(engine, args);
            return {{"result", res}};
        } catch (const std::exception& e) {
            return {
                {"error", {
                    {"code", -32000},
                    {"message", e.what()}
                }}
            };
        }
    }

    nlohmann::json McpToolsetRegistry::getToolsManifest(AgentAccessTier clientTier) const {
        nlohmann::json manifest = nlohmann::json::array();
        for (const auto& [name, tool] : m_tools) {
            if (static_cast<uint8_t>(clientTier) >= static_cast<uint8_t>(tool.requiredTier)) {
                manifest.push_back({
                    {"name", tool.name},
                    {"domain", tool.domain},
                    {"description", tool.description},
                    {"requiredTier", agentAccessTierToString(tool.requiredTier)},
                    {"inputSchema", tool.inputSchema}
                });
            }
        }
        return manifest;
    }

    void McpToolsetRegistry::registerDefaultEngineTools() {
        // --- DOMAIN: mcp ---
        registerTool({
            .name = "mcp.query_knowledge_base",
            .domain = "mcp",
            .description = "Zwraca pelny zywy graf wiedzy silnika: zarejestrowane komponenty C++, uniformy shaderow i podsumowanie sceny.",
            .requiredTier = AgentAccessTier::ReadOnly,
            .inputSchema = {{"type", "object"}, {"properties", nlohmann::json::object()}},
            .handler = [](core::Engine* eng, const nlohmann::json&) -> nlohmann::json {
                return McpKnowledgeBase::buildKnowledgeGraph(eng);
            }
        });

        registerTool({
            .name = "mcp.rollback_last_transaction",
            .domain = "mcp",
            .description = "Cofa ostatnia paczke mutacji ECS do stanu sprzed transakcji z synchronizacja Jolt Physics.",
            .requiredTier = AgentAccessTier::LiveEdit,
            .inputSchema = {{"type", "object"}, {"properties", nlohmann::json::object()}},
            .handler = [this](core::Engine* eng, const nlohmann::json&) -> nlohmann::json {
                bool ok = rollbackLastTransaction(eng);
                return {{"status", ok ? "rolled_back" : "no_snapshot"}, {"success", ok}};
            }
        });

        registerTool({
            .name = "mcp.place_annotation",
            .domain = "mcp",
            .description = "Umieszcza przestrzenny znacznik 3D (SpatialAnnotation) w scenie, widoczny w edytorze i na zrzutach klatek.",
            .requiredTier = AgentAccessTier::StageChanges,
            .inputSchema = {
                {"type", "object"},
                {"properties", {
                    {"worldPos", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}}},
                    {"radius", {{"type", "number"}}},
                    {"label", {{"type", "string"}}},
                    {"color", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 4}, {"maxItems", 4}}},
                    {"agentId", {{"type", "integer"}}},
                    {"lifetime", {{"type", "number"}}}
                }},
                {"required", {"worldPos", "label"}}
            },
            .handler = [](core::Engine*, const nlohmann::json& args) -> nlohmann::json {
                renderer::SpatialAnnotation ann{};
                if (args.contains("worldPos") && args["worldPos"].is_array() && args["worldPos"].size() >= 3) {
                    ann.worldPos = glm::vec3(args["worldPos"][0].get<float>(), args["worldPos"][1].get<float>(), args["worldPos"][2].get<float>());
                }
                if (args.contains("radius") && args["radius"].is_number()) {
                    ann.radius = args["radius"].get<float>();
                }
                if (args.contains("label") && args["label"].is_string()) {
                    ann.label = args["label"].get<std::string>();
                }
                if (args.contains("color") && args["color"].is_array() && args["color"].size() >= 4) {
                    ann.color = glm::vec4(args["color"][0].get<float>(), args["color"][1].get<float>(), args["color"][2].get<float>(), args["color"][3].get<float>());
                }
                if (args.contains("agentId") && args["agentId"].is_number_integer()) {
                    ann.agentId = args["agentId"].get<uint32_t>();
                }
                if (args.contains("lifetime") && args["lifetime"].is_number()) {
                    ann.lifetimeSeconds = args["lifetime"].get<float>();
                }
                uint32_t id = renderer::SpatialAnnotationRegistry::get().addAnnotation(ann);
                return {{"id", id}, {"status", "created"}};
            }
        });

        registerTool({
            .name = "mcp.clear_annotations",
            .domain = "mcp",
            .description = "Usuwa wszystkie znaczniki przestrzenne 3D.",
            .requiredTier = AgentAccessTier::StageChanges,
            .inputSchema = {{"type", "object"}, {"properties", nlohmann::json::object()}},
            .handler = [](core::Engine*, const nlohmann::json&) -> nlohmann::json {
                renderer::SpatialAnnotationRegistry::get().clear();
                return {{"status", "cleared"}};
            }
        });

        registerTool({
            .name = "mcp.list_annotations",
            .domain = "mcp",
            .description = "Zwraca liste aktywnych znacznikow przestrzennych 3D w scenie.",
            .requiredTier = AgentAccessTier::ReadOnly,
            .inputSchema = {{"type", "object"}, {"properties", nlohmann::json::object()}},
            .handler = [](core::Engine*, const nlohmann::json&) -> nlohmann::json {
                return renderer::SpatialAnnotationRegistry::get().toJson();
            }
        });

        // --- DOMAIN: ecs ---
        registerTool({
            .name = "ecs.query_entities",
            .domain = "ecs",
            .description = "Pobiera pelny zrzut wszystkich encji w scenie.",
            .requiredTier = AgentAccessTier::ReadOnly,
            .inputSchema = {{"type", "object"}, {"properties", nlohmann::json::object()}},
            .handler = [](core::Engine* eng, const nlohmann::json&) -> nlohmann::json {
                return eng ? eng->getSnapshotJson() : nlohmann::json::array();
            }
        });

        registerTool({
            .name = "ecs.spawn_entity",
            .domain = "ecs",
            .description = "Tworzy nowa encje w swiecie gry z komponentami (tag, transform, velocity, mesh, rigidBody).",
            .requiredTier = AgentAccessTier::LiveEdit,
            .inputSchema = {
                {"type", "object"},
                {"properties", {
                    {"tag", {{"type", "string"}}},
                    {"transform", {{"type", "object"}}},
                    {"velocity", {{"type", "object"}}},
                    {"mesh", {{"type", "object"}}},
                    {"rigidBody", {{"type", "object"}}}
                }}
            },
            .handler = [](core::Engine* eng, const nlohmann::json& args) -> nlohmann::json {
                uint32_t id = eng ? eng->spawnEntityFromJson(args) : 0;
                return {{"status", "success"}, {"entityId", id}};
            }
        });

        registerTool({
            .name = "ecs.destroy_entity",
            .domain = "ecs",
            .description = "Usuwa encje ze swiata gry po jej ID.",
            .requiredTier = AgentAccessTier::LiveEdit,
            .inputSchema = {
                {"type", "object"},
                {"properties", {
                    {"entityId", {{"type", "integer"}}}
                }},
                {"required", {"entityId"}}
            },
            .handler = [](core::Engine* eng, const nlohmann::json& args) -> nlohmann::json {
                uint32_t id = args.value("entityId", 0);
                bool ok = eng ? eng->destroyEntity(id) : false;
                return {{"status", ok ? "destroyed" : "not_found"}, {"entityId", id}};
            }
        });

        // --- DOMAIN: renderer ---
        registerTool({
            .name = "renderer.get_resolution",
            .domain = "renderer",
            .description = "Zwraca aktualna rozdzielczosc renderingu i viewportu.",
            .requiredTier = AgentAccessTier::ReadOnly,
            .inputSchema = {{"type", "object"}, {"properties", nlohmann::json::object()}},
            .handler = [](core::Engine* eng, const nlohmann::json&) -> nlohmann::json {
                uint32_t width = 0, height = 0;
                if (eng) eng->getRenderResolution(width, height);
                return {
                    {"width", width},
                    {"height", height}
                };
            }
        });

        registerTool({
            .name = "renderer.set_features",
            .domain = "renderer",
            .description = "Wlacza lub wylacza funkcje renderingu (rayTracing, bloom, gi, hiZCulling).",
            .requiredTier = AgentAccessTier::LiveEdit,
            .inputSchema = {
                {"type", "object"},
                {"properties", {
                    {"gtao", {{"type", "boolean"}}},
                    {"ssgi", {{"type", "boolean"}}},
                    {"bloom", {{"type", "boolean"}}},
                    {"hiZCulling", {{"type", "boolean"}}}
                }}
            },
            .handler = [](core::Engine* eng, const nlohmann::json& args) -> nlohmann::json {
                if (eng && eng->getRenderer()) {
                    auto* renderer = eng->getRenderer();
                    if (args.contains("gtao") && args["gtao"].is_boolean()) {
                        renderer->setGtaoEnabled(args["gtao"].get<bool>());
                    }
                    if (args.contains("ssgi") && args["ssgi"].is_boolean()) {
                        renderer->setSsgiEnabled(args["ssgi"].get<bool>());
                    }
                }
                return {{"status", "updated"}};
            }
        });

        registerTool({
            .name = "renderer.capture_aov",
            .domain = "renderer",
            .description = "Pobiera nieblokujacy zrzut wybranego bufora AOV (FinalComposite, NormalRoughness, AlbedoMetallic, GIIndirect, ShadowCascadeDebug) wraz z pelna telemetria passow i macierza kamery.",
            .requiredTier = AgentAccessTier::ReadOnly,
            .inputSchema = {
                {"type", "object"},
                {"properties", {
                    {"aov", {{"type", "string"}, {"enum", {"FinalComposite", "NormalRoughness", "AlbedoMetallic", "GIIndirect", "ShadowCascadeDebug"}}}}
                }}
            },
            .handler = [](core::Engine* eng, const nlohmann::json& args) -> nlohmann::json {
                std::string aov = "FinalComposite";
                if (args.contains("aov") && args["aov"].is_string()) {
                    aov = args["aov"].get<std::string>();
                }
                nlohmann::json metadata = nlohmann::json::object();
                std::string b64 = eng ? eng->captureAovBase64(aov, metadata) : "";
                return {
                    {"aov", aov},
                    {"metadata", metadata},
                    {"image", b64}
                };
            }
        });

        registerTool({
            .name = "renderer.get_pass_telemetry",
            .domain = "renderer",
            .description = "Pobiera czasy trwania wykonania poszczegolnych passow RenderGraph w milisekundach.",
            .requiredTier = AgentAccessTier::ReadOnly,
            .inputSchema = {{"type", "object"}, {"properties", nlohmann::json::object()}},
            .handler = [](core::Engine* eng, const nlohmann::json&) -> nlohmann::json {
                if (eng && eng->getRenderer()) {
                    return eng->getRenderer()->getRenderGraph().getPassTelemetryJson();
                }
                return nlohmann::json::array();
            }
        });

        // --- DOMAIN: profiler ---
        registerTool({
            .name = "profiler.get_gpu_stats",
            .domain = "profiler",
            .description = "Pobiera dokladna telemetrie czasow GPU i CPU klatki z profiler pool.",
            .requiredTier = AgentAccessTier::ReadOnly,
            .inputSchema = {{"type", "object"}, {"properties", nlohmann::json::object()}},
            .handler = [](core::Engine*, const nlohmann::json&) -> nlohmann::json {
                return core::Profiler::get().getStatsJson();
            }
        });

        // --- DOMAIN: shader ---
        registerTool({
            .name = "shader.inspect_uniforms",
            .domain = "shader",
            .description = "Zwraca struktury uniformow, buforow SSBO i push constantow aktywnych potokow.",
            .requiredTier = AgentAccessTier::ReadOnly,
            .inputSchema = {{"type", "object"}, {"properties", nlohmann::json::object()}},
            .handler = [](core::Engine*, const nlohmann::json&) -> nlohmann::json {
                return McpKnowledgeBase::getShaderUniformsSchema();
            }
        });

        // --- DOMAIN: script ---
        registerTool({
            .name = "script.load_script",
            .domain = "script",
            .description = "Wstrzykuje kod JavaScript (QuickJS-ng) do pętli rozgrywki.",
            .requiredTier = AgentAccessTier::FullControl,
            .inputSchema = {
                {"type", "object"},
                {"properties", {
                    {"code", {{"type", "string"}}}
                }},
                {"required", {"code"}}
            },
            .handler = [](core::Engine* eng, const nlohmann::json& args) -> nlohmann::json {
                bool ok = eng ? eng->loadScript(args.value("code", "")) : false;
                return {{"status", ok ? "success" : "script_error"}};
            }
        });

        // --- Legacy MCP Compatibility Aliases ---
        registerTool({
            .name = "get_scene_snapshot",
            .domain = "ecs",
            .description = "Alias dla ecs.query_entities",
            .requiredTier = AgentAccessTier::ReadOnly,
            .inputSchema = {{"type", "object"}, {"properties", nlohmann::json::object()}},
            .handler = [this](core::Engine* eng, const nlohmann::json& a) { return m_tools["ecs.query_entities"].handler(eng, a); }
        });

        registerTool({
            .name = "spawn_entity",
            .domain = "ecs",
            .description = "Alias dla ecs.spawn_entity",
            .requiredTier = AgentAccessTier::LiveEdit,
            .inputSchema = m_tools["ecs.spawn_entity"].inputSchema,
            .handler = [this](core::Engine* eng, const nlohmann::json& a) { return m_tools["ecs.spawn_entity"].handler(eng, a); }
        });

        registerTool({
            .name = "destroy_entity",
            .domain = "ecs",
            .description = "Alias dla ecs.destroy_entity",
            .requiredTier = AgentAccessTier::LiveEdit,
            .inputSchema = m_tools["ecs.destroy_entity"].inputSchema,
            .handler = [this](core::Engine* eng, const nlohmann::json& a) { return m_tools["ecs.destroy_entity"].handler(eng, a); }
        });

        registerTool({
            .name = "load_game_script",
            .domain = "script",
            .description = "Alias dla script.load_script",
            .requiredTier = AgentAccessTier::FullControl,
            .inputSchema = m_tools["script.load_script"].inputSchema,
            .handler = [this](core::Engine* eng, const nlohmann::json& a) { return m_tools["script.load_script"].handler(eng, a); }
        });
    }

} // namespace engine::network::mcp
