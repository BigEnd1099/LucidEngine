#include "network/JsonRpcDispatcher.hpp"
#include "core/Engine.hpp"
#include "core/Logger.hpp"
#include <iostream>

namespace engine::network {
    void JsonRpcDispatcher::dispatch(const std::string& payload, ix::WebSocket* client, core::ThreadQueue<Command>& queue) {
        try {
            auto json = nlohmann::json::parse(payload);
            if (!json.contains("jsonrpc") || json["jsonrpc"] != "2.0") return;
            if (!json.contains("method")) return;

            std::string method = json["method"];
            auto id = json.contains("id") ? json["id"] : nlohmann::json();

            if (method == "engine.loadScript") {
                std::string code;
                if (json.contains("params")) {
                    if (json["params"].is_string()) {
                        code = json["params"].get<std::string>();
                    } else if (json["params"].is_object() && json["params"].contains("code")) {
                        code = json["params"]["code"].get<std::string>();
                    }
                }
                queue.push([code, client, id](core::Engine* eng) {
                    bool success = eng->loadScript(code);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", nlohmann::json::object({{"success", success}})}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.spawnEntity") {
                nlohmann::json comps = nlohmann::json::object();
                if (json.contains("params")) {
                    if (json["params"].is_object() && json["params"].contains("components")) {
                        comps = json["params"]["components"];
                    } else if (json["params"].is_object()) {
                        comps = json["params"];
                    }
                }
                queue.push([comps, client, id](core::Engine* eng) {
                    uint32_t entId = eng->spawnEntityFromJson(comps);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", nlohmann::json::object({{"entityId", entId}})}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.destroyEntity") {
                uint32_t entId = 0;
                if (json.contains("params")) {
                    if (json["params"].is_number()) {
                        entId = json["params"].get<uint32_t>();
                    } else if (json["params"].is_object()) {
                        if (json["params"].contains("entityId")) entId = json["params"]["entityId"];
                        else if (json["params"].contains("id")) entId = json["params"]["id"];
                    }
                }
                queue.push([entId, client, id](core::Engine* eng) {
                    bool ok = eng->destroyEntity(entId);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", nlohmann::json::object({{"success", ok}})}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.captureFrame") {
                queue.push([client, id](core::Engine* eng) {
                    std::string b64 = eng->captureFrameBase64();
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", nlohmann::json::object({{"frame", b64}})}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.stepSimulation") {
                int steps = 1;
                float dt = 0.016f;
                if (json.contains("params") && json["params"].is_object()) {
                    steps = json["params"].value("steps", 1);
                    dt = json["params"].value("dt", 0.016f);
                }
                queue.push([steps, dt, client, id](core::Engine* eng) {
                    eng->stepSimulation(steps, dt);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", "ok"}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.playSimulation") {
                queue.push([client, id](core::Engine* eng) {
                    eng->playSimulation();
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", nlohmann::json::object({{"status", "simulation_started"}, {"paused", false}})}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.stopSimulation") {
                queue.push([client, id](core::Engine* eng) {
                    eng->stopSimulation();
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", nlohmann::json::object({{"status", "simulation_stopped"}, {"paused", true}})}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.setPaused") {
                bool paused = true;
                if (json.contains("params")) {
                    if (json["params"].is_boolean()) paused = json["params"].get<bool>();
                    else if (json["params"].is_object() && json["params"].contains("paused")) paused = json["params"]["paused"].get<bool>();
                }
                queue.push([paused, client, id](core::Engine* eng) {
                    eng->setPaused(paused);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", nlohmann::json::object({{"paused", paused}})}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.togglePause") {
                queue.push([client, id](core::Engine* eng) {
                    eng->togglePause();
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", nlohmann::json::object({{"paused", eng->isPaused()}})}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.resetScene") {
                std::string preset = "physics";
                if (json.contains("params")) {
                    if (json["params"].is_string()) preset = json["params"].get<std::string>();
                    else if (json["params"].is_object() && json["params"].contains("preset")) preset = json["params"]["preset"].get<std::string>();
                }
                queue.push([preset, client, id](core::Engine* eng) {
                    eng->resetScene(preset);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", nlohmann::json::object({{"status", "scene_reset"}, {"preset", preset}})}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.getSnapshot") {
                queue.push([client, id](core::Engine* eng) {
                    nlohmann::json snap = eng->getSnapshotJson();
                    nlohmann::json res = {{"jsonrpc", "2.0"}, {"id", id}, {"result", snap}};
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.getTypeDefinitions") {
                queue.push([client, id](core::Engine* eng) {
                    std::string dts = eng->getTypeDefinitions();
                    nlohmann::json res = {{"jsonrpc", "2.0"}, {"id", id}, {"result", dts}};
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.loadGlbFromBase64") {
                std::string b64;
                if (json.contains("params")) {
                    if (json["params"].is_string()) {
                        b64 = json["params"].get<std::string>();
                    } else if (json["params"].is_object()) {
                        if (json["params"].contains("glbData")) b64 = json["params"]["glbData"].get<std::string>();
                        else if (json["params"].contains("base64")) b64 = json["params"]["base64"].get<std::string>();
                    }
                }
                queue.push([b64, client, id](core::Engine* eng) {
                    uint32_t entId = eng->loadGlbFromBase64(b64);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", nlohmann::json::object({{"entityId", entId}})}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.applyPbrMaterial") {
                nlohmann::json args = nlohmann::json::object();
                uint32_t entityId = 0;
                if (json.contains("params") && json["params"].is_object()) {
                    args = json["params"];
                    if (args.contains("entityId")) {
                        if (args["entityId"].is_number()) entityId = args["entityId"];
                        else if (args["entityId"].is_object() && args["entityId"].contains("entityId")) entityId = args["entityId"]["entityId"];
                    }
                }
                queue.push([entityId, args, client, id](core::Engine* eng) {
                    eng->applyPbrMaterial(entityId, args);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", nlohmann::json::object({{"status", "material_applied"}})}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.getLogs") {
                queue.push([client, id](core::Engine* eng) {
                    auto logs = core::Logger::getRecentLogs(50);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", logs}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.getCamera") {
                queue.push([client, id](core::Engine* eng) {
                    glm::vec3 pos = eng->getCameraPosition();
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", {
                            {"x", pos.x}, {"y", pos.y}, {"z", pos.z},
                            {"yaw", eng->getCameraYaw()},
                            {"pitch", eng->getCameraPitch()},
                            {"fov", eng->getCameraFov()}
                        }}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.setCamera") {
                glm::vec3 pos(0.0f, 15.0f, 30.0f);
                float yaw = -90.0f;
                float pitch = -20.0f;
                float fov = 45.0f;
                if (json.contains("params") && json["params"].is_object()) {
                    const auto& p = json["params"];
                    if (p.contains("x")) pos.x = p["x"].get<float>();
                    if (p.contains("y")) pos.y = p["y"].get<float>();
                    if (p.contains("z")) pos.z = p["z"].get<float>();
                    if (p.contains("yaw")) yaw = p["yaw"].get<float>();
                    if (p.contains("pitch")) pitch = p["pitch"].get<float>();
                    if (p.contains("fov")) fov = p["fov"].get<float>();
                }
                queue.push([pos, yaw, pitch, fov, client, id](core::Engine* eng) {
                    eng->setCamera(pos, yaw, pitch, fov);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", true}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.getGameState") {
                queue.push([client, id](core::Engine* eng) {
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", {
                            {"score", eng->getScore()},
                            {"gameState", eng->getGameState()}
                        }}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.saveProjectToDisk") {
                std::string path;
                nlohmann::json pData = nlohmann::json::object();
                if (json.contains("params") && json["params"].is_object()) {
                    path = json["params"].value("folderPath", json["params"].value("path", ""));
                    if (json["params"].contains("project")) pData = json["params"]["project"];
                }
                if (path.empty() && pData.contains("folderPath")) {
                    path = pData["folderPath"].get<std::string>();
                }
                queue.push([path, pData, client, id](core::Engine* eng) {
                    bool ok = eng->saveProjectToDisk(path, pData);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", nlohmann::json::object({{"success", ok}, {"savedPath", path}})}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.packageGame") {
                std::string outDir = "Build/Windows";
                std::string name = "LucidGame";
                if (json.contains("params") && json["params"].is_object()) {
                    outDir = json["params"].value("outDir", "Build/Windows");
                    name = json["params"].value("name", "LucidGame");
                }
                queue.push([outDir, name, client, id](core::Engine* eng) {
                    bool ok = eng->packageStandaloneGame(outDir, name);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", nlohmann::json::object({
                            {"success", ok},
                            {"targetFolder", outDir + "/" + name},
                            {"executable", name + ".exe"}
                        })}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.loadProjectFromDisk") {
                std::string path;
                if (json.contains("params")) {
                    if (json["params"].is_string()) path = json["params"].get<std::string>();
                    else if (json["params"].is_object()) path = json["params"].value("folderPath", json["params"].value("path", ""));
                }
                queue.push([path, client, id](core::Engine* eng) {
                    auto resData = eng->loadProjectFromDisk(path);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", resData}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.listDiskProjects") {
                std::string baseDir = "projects";
                if (json.contains("params") && json["params"].is_object() && json["params"].contains("baseDir")) {
                    baseDir = json["params"]["baseDir"].get<std::string>();
                }
                queue.push([baseDir, client, id](core::Engine* eng) {
                    auto list = eng->listDiskProjects(baseDir);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", list}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.updateEntity" || method == "engine.applySnapshotJson") {
                std::string snapStr;
                if (json.contains("params")) {
                    if (json["params"].is_string()) {
                        snapStr = json["params"].get<std::string>();
                    } else if (json["params"].is_array() || json["params"].is_object()) {
                        snapStr = json["params"].dump();
                    }
                }
                queue.push([snapStr, client, id](core::Engine* eng) {
                    eng->applySnapshotJson(snapStr);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", true}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.sendInput") {
                nlohmann::json inputData = nlohmann::json::object();
                if (json.contains("params") && json["params"].is_object()) {
                    inputData = json["params"];
                }
                queue.push([inputData](core::Engine* eng) {
                    eng->setInputState(inputData);
                });
            } else if (method == "engine.getHealth") {
                queue.push([client, id](core::Engine* eng) {
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", {
                            {"health", eng->getHealth()}
                        }}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.setHealth") {
                int hp = 100;
                if (json.contains("params") && json["params"].is_object() && json["params"].contains("health")) {
                    hp = json["params"]["health"].get<int>();
                }
                queue.push([client, id, hp](core::Engine* eng) {
                    eng->setHealth(hp);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", true}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.getGameStatus") {
                queue.push([client, id](core::Engine* eng) {
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", {
                            {"status", eng->getGameStatus()}
                        }}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.setGameStatus") {
                std::string st = "PLAYING";
                if (json.contains("params") && json["params"].is_object() && json["params"].contains("status")) {
                    st = json["params"]["status"].get<std::string>();
                }
                queue.push([client, id, st](core::Engine* eng) {
                    eng->setGameStatus(st);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", true}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.playSfx") {
                std::string type = "hit";
                float vol = 1.0f;
                if (json.contains("params") && json["params"].is_object()) {
                    if (json["params"].contains("type")) type = json["params"]["type"].get<std::string>();
                    if (json["params"].contains("volume")) vol = json["params"]["volume"].get<float>();
                }
                queue.push([client, id, type, vol](core::Engine* eng) {
                    eng->broadcastSfx(type, vol);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", true}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else if (method == "engine.shakeCamera") {
                float intensity = 0.5f;
                float duration = 0.3f;
                if (json.contains("params") && json["params"].is_object()) {
                    if (json["params"].contains("intensity")) intensity = json["params"]["intensity"].get<float>();
                    if (json["params"].contains("duration")) duration = json["params"]["duration"].get<float>();
                }
                queue.push([client, id, intensity, duration](core::Engine* eng) {
                    eng->broadcastCameraShake(intensity, duration);
                    nlohmann::json res = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", true}
                    };
                    eng->sendWsMessage(client, res.dump());
                });
            } else {
                core::Logger::warn("Unknown JSON-RPC method: " + method);
                if (!id.is_null() && client) {
                    nlohmann::json errRes = {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"error", {{"code", -32601}, {"message", "Method not found: " + method}}}
                    };
                    queue.push([errRes, client](core::Engine* eng) {
                        eng->sendWsMessage(client, errRes.dump());
                    });
                }
            }
        } catch (const std::exception& e) {
            core::Logger::error(std::string("JSON-RPC Error: ") + e.what());
        }
    }
}
