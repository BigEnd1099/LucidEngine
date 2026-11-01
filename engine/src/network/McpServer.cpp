#include "network/McpServer.hpp"
#include "core/Engine.hpp"
#include "core/Logger.hpp"
#include <iostream>

namespace engine::network::mcp {

    McpServer::McpServer(core::Engine* engine) : m_engine(engine) {
        registerEngineTools();
    }

    void McpServer::registerEngineTools() {
        // 1. Scene Inspection
        m_tools["get_scene_snapshot"] = {
            .name = "get_scene_snapshot",
            .description = "Pobiera pełny zrzut wszystkich encji w scenie (ID, tagi, transformacje, prędkości, meshe, fizyka).",
            .inputSchema = {
                {"type", "object"},
                {"properties", nlohmann::json::object()}
            },
            .handler = [this](const nlohmann::json&) -> nlohmann::json {
                return m_engine->executeOnMainThread([](core::Engine* eng) {
                    return eng->getSnapshotJson();
                });
            }
        };

        // 2. Tworzenie encji w świecie gry
        m_tools["spawn_entity"] = {
            .name = "spawn_entity",
            .description = "Tworzy nową encję w świecie gry z zadanymi komponentami (tag, transform, velocity, mesh, rigidBody).",
            .inputSchema = {
                {"type", "object"},
                {"properties", {
                    {"tag", {{"type", "string"}, {"description", "Etykieta encji (np. Player, Enemy, Ball, Ground)"}}},
                    {"transform", {
                        {"type", "object"},
                        {"properties", {
                            {"x", {{"type", "number"}}},
                            {"y", {{"type", "number"}}},
                            {"z", {{"type", "number"}}},
                            {"rx", {{"type", "number"}}},
                            {"ry", {{"type", "number"}}},
                            {"rz", {{"type", "number"}}},
                            {"sx", {{"type", "number"}}},
                            {"sy", {{"type", "number"}}},
                            {"sz", {{"type", "number"}}}
                        }}
                    }},
                    {"velocity", {
                        {"type", "object"},
                        {"properties", {
                            {"vx", {{"type", "number"}}},
                            {"vy", {{"type", "number"}}},
                            {"vz", {{"type", "number"}}}
                        }}
                    }},
                    {"mesh", {
                        {"type", "object"},
                        {"properties", {
                            {"meshId", {{"type", "integer"}, {"description", "1 = Floor plane, 2 = Cube/Box"}}}
                        }}
                    }},
                    {"rigidBody", {
                        {"type", "object"},
                        {"properties", {
                            {"mass", {{"type", "number"}, {"description", "0 = statyczny, >0 = dynamiczne ciało fizyczne"}}},
                            {"shapeType", {{"type", "integer"}, {"description", "0 = Box, 1 = Sphere"}}}
                        }}
                    }}
                }}
            },
            .handler = [this](const nlohmann::json& args) -> nlohmann::json {
                return m_engine->executeOnMainThread([args](core::Engine* eng) -> nlohmann::json {
                    uint32_t id = eng->spawnEntityFromJson(args);
                    return {{"status", "success"}, {"entityId", id}};
                });
            }
        };

        // 3. Usuwanie encji
        m_tools["destroy_entity"] = {
            .name = "destroy_entity",
            .description = "Usuwa encję ze świata gry po jej ID.",
            .inputSchema = {
                {"type", "object"},
                {"properties", {
                    {"entityId", {{"type", "integer"}, {"description", "ID encji do usunięcia"}}}
                }},
                {"required", {"entityId"}}
            },
            .handler = [this](const nlohmann::json& args) -> nlohmann::json {
                return m_engine->executeOnMainThread([args](core::Engine* eng) -> nlohmann::json {
                    uint32_t id = args.value("entityId", 0);
                    bool ok = eng->destroyEntity(id);
                    return {{"status", ok ? "destroyed" : "not_found"}, {"entityId", id}};
                });
            }
        };

        // 4. Ładowanie skryptu mechaniki gry (QuickJS ESM)
        m_tools["load_game_script"] = {
            .name = "load_game_script",
            .description = "Wstrzykuje kod JavaScript/QuickJS wykonujący logikę gry. Wymaga wyeksportowania funkcji 'update(entities, dt)'.",
            .inputSchema = {
                {"type", "object"},
                {"properties", {
                    {"code", {{"type", "string"}, {"description", "Kod źródłowy JavaScript"}}}
                }},
                {"required", {"code"}}
            },
            .handler = [this](const nlohmann::json& args) -> nlohmann::json {
                return m_engine->executeOnMainThread([args](core::Engine* eng) -> nlohmann::json {
                    bool ok = eng->loadScript(args["code"].get<std::string>());
                    return {{"status", ok ? "success" : "script_error"}};
                });
            }
        };

        // 5. Krok symulacji (Krok fizyki i skryptu weryfikacyjny)
        m_tools["step_simulation"] = {
            .name = "step_simulation",
            .description = "Wykonuje N kroków symulacji silnika (fizyka Jolt + QuickJS tick).",
            .inputSchema = {
                {"type", "object"},
                {"properties", {
                    {"steps", {{"type", "integer"}, {"description", "Liczba kroków symulacji (domyślnie 1)"}}},
                    {"dt", {{"type", "number"}, {"description", "Krok czasowy w sekundach (domyślnie 0.016)"}}}
                }}
            },
            .handler = [this](const nlohmann::json& args) -> nlohmann::json {
                return m_engine->executeOnMainThread([args](core::Engine* eng) -> nlohmann::json {
                    int steps = args.value("steps", 1);
                    float dt = args.value("dt", 0.016f);
                    eng->stepSimulation(steps, dt);
                    return {{"status", "stepped"}, {"steps", steps}, {"dt", dt}};
                });
            }
        };

        // 6. Resetowanie sceny i wybór archetypu gry
        m_tools["reset_scene"] = {
            .name = "reset_scene",
            .description = "Resetuje scenę do zadanego archetypu: 'physics', 'solar', 'breakout', lub 'empty'.",
            .inputSchema = {
                {"type", "object"},
                {"properties", {
                    {"preset", {{"type", "string"}, {"description", "Nazwa szablonu: physics, solar, breakout, empty"}}}
                }}
            },
            .handler = [this](const nlohmann::json& args) -> nlohmann::json {
                return m_engine->executeOnMainThread([args](core::Engine* eng) -> nlohmann::json {
                    std::string preset = args.value("preset", "physics");
                    eng->resetScene(preset);
                    return {{"status", "scene_reset"}, {"preset", preset}};
                });
            }
        };

        // 7. Zatrzymanie / wznowienie symulacji
        m_tools["set_paused"] = {
            .name = "set_paused",
            .description = "Zatrzymuje (pause) lub wznawia pętlę symulacji czasu rzeczywistego.",
            .inputSchema = {
                {"type", "object"},
                {"properties", {
                    {"paused", {{"type", "boolean"}}}
                }},
                {"required", {"paused"}}
            },
            .handler = [this](const nlohmann::json& args) -> nlohmann::json {
                return m_engine->executeOnMainThread([args](core::Engine* eng) -> nlohmann::json {
                    bool p = args.value("paused", true);
                    eng->setPaused(p);
                    return {{"status", "success"}, {"paused", p}};
                });
            }
        };

        // 8. Pobranie definicji TypeScript (dla LLM)
        m_tools["get_type_definitions"] = {
            .name = "get_type_definitions",
            .description = "Pobiera plik engine.d.ts z definicjami typów komponentów i API silnika.",
            .inputSchema = {
                {"type", "object"},
                {"properties", nlohmann::json::object()}
            },
            .handler = [this](const nlohmann::json&) -> nlohmann::json {
                return m_engine->executeOnMainThread([](core::Engine* eng) -> nlohmann::json {
                    return {{"definitions", eng->getTypeDefinitions()}};
                });
            }
        };

        // 9. Narzędzie mesh ze streamu generatywnego
        m_tools["spawn_procedural_mesh"] = {
            .name = "spawn_procedural_mesh",
            .description = "Wstrzykuje wygenerowaną siatkę 3D GLB do świata gry.",
            .inputSchema = {
                {"type", "object"},
                {"properties", {
                    {"glb_base64", {{"type", "string"}, {"description", "Binarny plik GLB zakodowany w Base64"}}}
                }},
                {"required", {"glb_base64"}}
            },
            .handler = [this](const nlohmann::json& args) -> nlohmann::json {
                return m_engine->executeOnMainThread([args](core::Engine* eng) -> nlohmann::json {
                    uint32_t entityId = eng->loadGlbFromBase64(args["glb_base64"].get<std::string>());
                    return {{"status", "success"}, {"entityId", entityId}};
                });
            }
        };

        // 10. Nadanie tekstur PBR
        m_tools["apply_pbr_textures"] = {
            .name = "apply_pbr_textures",
            .description = "Przypisuje wygenerowane tekstury PBR do encji.",
            .inputSchema = {
                {"type", "object"},
                {"properties", {
                    {"entityId", {{"type", "integer"}}},
                    {"albedo_base64", {{"type", "string"}}},
                    {"normal_base64", {{"type", "string"}}},
                    {"orm_base64", {{"type", "string"}}}
                }},
                {"required", {"entityId"}}
            },
            .handler = [this](const nlohmann::json& args) -> nlohmann::json {
                return m_engine->executeOnMainThread([args](core::Engine* eng) -> nlohmann::json {
                    eng->applyPbrMaterial(args["entityId"], args);
                    return {{"status", "material_applied"}};
                });
            }
        };

        // 11. Ostatnie logi diagnostyczne silnika
        m_tools["get_recent_logs"] = {
            .name = "get_recent_logs",
            .description = "Pobiera ostatnie logi silnika i błędy środowiska QuickJS.",
            .inputSchema = {
                {"type", "object"},
                {"properties", nlohmann::json::object()}
            },
            .handler = [this](const nlohmann::json&) -> nlohmann::json {
                return {{"logs", core::Logger::getRecentLogs(50)}};
            }
        };

        // 12. Stan rozgrywki i wynik punktowy
        m_tools["get_game_state"] = {
            .name = "get_game_state",
            .description = "Pobiera aktualny wynik punktowy (score) i stan rozgrywki ustalony przez skrypty gry.",
            .inputSchema = {
                {"type", "object"},
                {"properties", nlohmann::json::object()}
            },
            .handler = [this](const nlohmann::json&) -> nlohmann::json {
                return m_engine->executeOnMainThread([](core::Engine* eng) -> nlohmann::json {
                    return {
                        {"score", eng->getScore()},
                        {"gameState", eng->getGameState()}
                    };
                });
            }
        };
    }

    void McpServer::processStdio() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            try {
                auto req = nlohmann::json::parse(line);
                if (!req.contains("method")) continue;
                
                std::string method = req["method"];
                auto id = req.contains("id") ? req["id"] : nlohmann::json();

                if (method == "initialize") {
                    nlohmann::json initResult = {
                        {"protocolVersion", "2024-11-05"},
                        {"capabilities", {
                            {"tools", nlohmann::json::object()}
                        }},
                        {"serverInfo", {
                            {"name", "LucidEngine-MCP"},
                            {"version", "1.0.0"}
                        }}
                    };
                    sendResponse(id, initResult);
                } else if (method == "notifications/initialized") {
                    // Client notification - no response required
                } else if (method == "ping") {
                    sendResponse(id, nlohmann::json::object());
                } else if (method == "tools/list") {
                    nlohmann::json toolList = nlohmann::json::array();
                    for (const auto& [name, tool] : m_tools) {
                        toolList.push_back({
                            {"name", tool.name},
                            {"description", tool.description},
                            {"inputSchema", tool.inputSchema}
                        });
                    }
                    sendResponse(id, {{"tools", toolList}});
                } else if (method == "tools/call") {
                    std::string toolName = req["params"]["name"];
                    auto args = req["params"].contains("arguments") ? req["params"]["arguments"] : nlohmann::json::object();
                    if (m_tools.contains(toolName)) {
                        try {
                            auto res = m_tools[toolName].handler(args);
                            sendResponse(id, {{"content", {{{"type", "text"}, {"text", res.dump()}}}}});
                        } catch (const std::exception& e) {
                            sendError(id, -32000, e.what());
                        }
                    } else {
                        sendError(id, -32601, "Tool not found: " + toolName);
                    }
                }
            } catch(...) {
                // Ignore parsing errors from arbitrary stdin garbage
            }
        }
    }

    void McpServer::sendResponse(const nlohmann::json& id, const nlohmann::json& result) {
        nlohmann::json res = {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
        std::cout << res.dump() << "\n" << std::flush;
    }

    void McpServer::sendError(const nlohmann::json& id, int code, const std::string& message) {
        nlohmann::json err = {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
        std::cout << err.dump() << "\n" << std::flush;
    }

} // namespace engine::network::mcp
