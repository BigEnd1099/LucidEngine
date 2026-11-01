#ifdef USE_GLFW
#include <GLFW/glfw3.h>
#include "imgui.h"
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

#include "core/Engine.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include "core/Logger.hpp"
#include "ecs/TypeExporter.hpp"
#include <chrono>
#include <thread>
#include <fstream>
#include <filesystem>
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Math/Quat.h>

namespace engine::core {

    // Simple robust base64 decoder
    static std::vector<std::byte> decodeBase64(const std::string& in) {
        // Strip data URL scheme prefix if present (e.g. "data:application/octet-stream;base64,...")
        size_t commaPos = in.find(',');
        std::string_view dataView = in;
        if (commaPos != std::string::npos && in.substr(0, commaPos).find("base64") != std::string::npos) {
            dataView = std::string_view(in).substr(commaPos + 1);
        }

        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; ++i) {
            T[static_cast<unsigned char>("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i])] = i;
        }

        std::vector<std::byte> out;
        out.reserve(dataView.size() * 3 / 4);
        int val = 0, valb = -8;
        for (char c_in : dataView) {
            unsigned char c = static_cast<unsigned char>(c_in);
            if (c == '=' || c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
            if (T[c] == -1) continue;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0) {
                out.push_back(static_cast<std::byte>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return out;
    }

    Engine::Engine(int wsPort, bool headless) : m_headless(headless) {
        m_registry = std::make_unique<ecs::Registry>();
        m_scriptContext = std::make_unique<script::QuickJSContext>();
        m_scriptContext->init(this);
        m_scriptContext->setMemoryLimit(64 * 1024 * 1024); // 64 MB AI script limit
        m_scriptContext->setOnError([this](const std::string& type, const std::string& msg, const std::string& stack) {
            broadcastScriptError(type, msg, stack);
        });
        
        m_wsServer = std::make_unique<network::WebSocketServer>(wsPort, m_commandQueue);
        m_udpServer = std::make_unique<network::UdpAnimationServer>(9001, m_commandQueue);
        m_mcpServer = std::make_unique<network::mcp::McpServer>(this);
        
        m_physicsContext = std::make_unique<physics::PhysicsContext>();
        m_physicsContext->init();
        m_physicsSystem = std::make_unique<physics::PhysicsSystem>(m_physicsContext.get());

        initTestScene();

        m_vulkanContext = std::make_unique<renderer::VulkanContext>(headless);
        m_vulkanContext->init();
        m_assetManager = std::make_unique<AssetManager>(m_vulkanContext.get());
        
        uint32_t renderW = 1920;
        uint32_t renderH = 1080;
#ifdef USE_GLFW
        if (!headless && m_vulkanContext->getSwapchainExtent().width > 0) {
            renderW = m_vulkanContext->getSwapchainExtent().width;
            renderH = m_vulkanContext->getSwapchainExtent().height;
        }
#endif
        m_renderer = std::make_unique<renderer::VulkanRenderer>(m_vulkanContext.get(), renderW, renderH);

        m_networkThread = std::jthread([this]() {
            try {
                m_wsServer->start();
                m_udpServer->start();
                while (m_running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                m_udpServer->stop();
                m_wsServer->stop();
            } catch (const std::exception& e) {
                core::Logger::error(std::string("Network thread exception: ") + e.what());
            } catch (...) {
                core::Logger::error("Unknown network thread exception");
            }
        });
        
        m_mcpThread = std::jthread([this]() {
            try {
                m_mcpServer->processStdio();
            } catch (...) {}
        });
    }

    Engine::~Engine() {
        stop();
        m_renderer.reset();
        if (m_vulkanContext) {
            m_vulkanContext->cleanup();
        }
    }

    void Engine::stop() {
        m_running = false;
    }

    void Engine::broadcastSimulationState() {
        if (m_wsServer) {
            nlohmann::json msg = {
                {"jsonrpc", "2.0"},
                {"method", "engine.onSimulationStateChanged"},
                {"params", {
                    {"paused", m_paused},
                    {"simulating", !m_paused}
                }}
            };
            m_wsServer->sendToAll(msg.dump());
        }
    }

    void Engine::playSimulation() {
        if (m_paused) {
            // 1. Capture full scene snapshot for non-destructive restore on STOP
            m_preSimulationSnapshot = getSnapshotJson();
            m_hasPreSimulationSnapshot = true;

            // 2. Sync all editor transforms to Jolt bodies and clear residual velocities
            if (m_physicsContext && m_registry) {
                auto& bi = m_physicsContext->getBodyInterface();
                auto view = m_registry->get().view<ecs::TransformComponent, ecs::RigidBodyComponent>();
                for (auto entity : view) {
                    auto& t = view.get<ecs::TransformComponent>(entity);
                    auto& rb = view.get<ecs::RigidBodyComponent>(entity);
                    if (rb.bodyId != 0xFFFFFFFF) {
                        JPH::BodyID bid(rb.bodyId);
                        if (!bid.IsInvalid()) {
                            JPH::Quat joltRot = (glm::dot(t.orientation, t.orientation) > 0.001f)
                                ? JPH::Quat(t.orientation.x, t.orientation.y, t.orientation.z, t.orientation.w)
                                : JPH::Quat::sEulerAngles(JPH::Vec3(
                                    glm::radians(t.rotation.x),
                                    glm::radians(t.rotation.y),
                                    glm::radians(t.rotation.z)
                                ));
                            bi.SetPositionAndRotation(
                                bid,
                                JPH::RVec3(t.position.x, t.position.y, t.position.z),
                                joltRot,
                                JPH::EActivation::Activate
                            );
                            bi.SetLinearVelocity(bid, JPH::Vec3::sZero());
                            bi.SetAngularVelocity(bid, JPH::Vec3::sZero());
                        }
                    }
                }
            }

            m_paused = false;
            broadcastSimulationState();
            core::Logger::info("Simulation PLAY started: Edit snapshot secured, Jolt bodies activated.");
        }
    }

    void Engine::stopSimulation() {
        m_paused = true;
        m_physicsAccumulator = 0.0f;

        // Restore pre-simulation snapshot
        if (m_hasPreSimulationSnapshot && !m_preSimulationSnapshot.empty()) {
            applySnapshotJson(m_preSimulationSnapshot.dump());
        }

        // Zero all velocities and force-freeze Jolt bodies to the restored edit transforms
        if (m_physicsContext && m_registry) {
            auto& bi = m_physicsContext->getBodyInterface();
            auto view = m_registry->get().view<ecs::TransformComponent, ecs::RigidBodyComponent>();
            for (auto entity : view) {
                auto& t = view.get<ecs::TransformComponent>(entity);
                auto& rb = view.get<ecs::RigidBodyComponent>(entity);
                if (rb.bodyId != 0xFFFFFFFF) {
                    JPH::BodyID bid(rb.bodyId);
                    if (!bid.IsInvalid()) {
                        bi.SetLinearVelocity(bid, JPH::Vec3::sZero());
                        bi.SetAngularVelocity(bid, JPH::Vec3::sZero());
                        JPH::Quat joltRot = (glm::dot(t.orientation, t.orientation) > 0.001f)
                            ? JPH::Quat(t.orientation.x, t.orientation.y, t.orientation.z, t.orientation.w)
                            : JPH::Quat::sEulerAngles(JPH::Vec3(
                                glm::radians(t.rotation.x),
                                glm::radians(t.rotation.y),
                                glm::radians(t.rotation.z)
                            ));
                        bi.SetPositionAndRotation(
                            bid,
                            JPH::RVec3(t.position.x, t.position.y, t.position.z),
                            joltRot,
                            JPH::EActivation::DontActivate
                        );
                    }
                }
            }
        }

        m_score.store(0);
        m_health.store(100);
        m_gameStatus = "PLAYING";
        broadcastSimulationState();
        core::Logger::info("Simulation STOPPED: Scene restored to pre-simulation edit pose (no reset).");
    }

    void Engine::setPaused(bool paused) {
        if (paused) {
            m_paused = true;
            broadcastSimulationState();
        } else {
            playSimulation();
        }
    }

    void Engine::togglePause() {
        setPaused(!m_paused);
    }

    void Engine::update(float dt) {
        float clampedDt = std::min(dt, 0.1f);
        m_physicsAccumulator += clampedDt;

        if (m_physicsSystem && m_registry) {
            while (m_physicsAccumulator >= FIXED_TIMESTEP) {
                m_physicsSystem->update(m_registry.get(), FIXED_TIMESTEP);
                m_physicsAccumulator -= FIXED_TIMESTEP;
            }
        }

        auto contacts = m_physicsContext ? m_physicsContext->getAndClearContacts() : std::vector<physics::ContactPoint>{};
        m_lastContacts = contacts;

        if (m_scriptContext && m_registry) {
            m_scriptContext->tickDirect(m_registry.get(), m_physicsContext.get(), dt, m_inputState, contacts);
        }
    }

    void Engine::applySnapshotJson(const std::string& jsonStr) {
        try {
            auto ents = nlohmann::json::parse(jsonStr);
            if (ents.is_object()) {
                nlohmann::json arr = nlohmann::json::array();
                arr.push_back(ents);
                ents = arr;
            }
            if (!ents.is_array()) return;

            for (const auto& entJson : ents) {
                if (!entJson.contains("id")) continue;
                auto entity = static_cast<entt::entity>(entJson["id"].get<uint32_t>());
                if (!m_registry->get().valid(entity)) continue;

                if (entJson.contains("tag") && entJson["tag"].is_string()) {
                    m_registry->get().get_or_emplace<ecs::TagComponent>(entity).name = entJson["tag"].get<std::string>();
                }

                if (entJson.contains("transform")) {
                    auto& t = m_registry->get().get_or_emplace<ecs::TransformComponent>(entity);
                    const auto& transJson = entJson["transform"];
                    bool posChanged = false;
                    bool rotChanged = false;
                    if (transJson.contains("x")) { t.position.x = transJson["x"]; posChanged = true; }
                    if (transJson.contains("y")) { t.position.y = transJson["y"]; posChanged = true; }
                    if (transJson.contains("z")) { t.position.z = transJson["z"]; posChanged = true; }
                    if (transJson.contains("rx")) { t.rotation.x = transJson["rx"]; rotChanged = true; }
                    if (transJson.contains("ry")) { t.rotation.y = transJson["ry"]; rotChanged = true; }
                    if (transJson.contains("rz")) { t.rotation.z = transJson["rz"]; rotChanged = true; }
                    if (transJson.contains("sx")) t.scale.x = transJson["sx"];
                    if (transJson.contains("sy")) t.scale.y = transJson["sy"];
                    if (transJson.contains("sz")) t.scale.z = transJson["sz"];

                    if ((posChanged || rotChanged) && m_physicsContext && m_registry->get().all_of<ecs::RigidBodyComponent>(entity)) {
                        auto& rb = m_registry->get().get<ecs::RigidBodyComponent>(entity);
                        if (rb.bodyId != 0xFFFFFFFF) {
                            JPH::BodyID bId(rb.bodyId);
                            JPH::RVec3 jPos(t.position.x, t.position.y, t.position.z);
                            JPH::Quat jRot = JPH::Quat::sEulerAngles(JPH::Vec3(
                                glm::radians(t.rotation.x),
                                glm::radians(t.rotation.y),
                                glm::radians(t.rotation.z)
                            ));
                            m_physicsContext->getBodyInterface().SetPositionAndRotation(bId, jPos, jRot, JPH::EActivation::Activate);
                        }
                    }
                }

                if (entJson.contains("velocity")) {
                    auto& v = m_registry->get().get_or_emplace<ecs::VelocityComponent>(entity);
                    const auto& velJson = entJson["velocity"];
                    if (velJson.contains("vx")) v.velocity.x = velJson["vx"];
                    if (velJson.contains("vy")) v.velocity.y = velJson["vy"];
                    if (velJson.contains("vz")) v.velocity.z = velJson["vz"];

                    if (m_physicsContext && m_registry->get().all_of<ecs::RigidBodyComponent>(entity)) {
                        auto& rb = m_registry->get().get<ecs::RigidBodyComponent>(entity);
                        if (rb.bodyId != 0xFFFFFFFF && rb.mass > 0.0f) {
                            JPH::BodyID bId(rb.bodyId);
                            m_physicsContext->getBodyInterface().SetLinearVelocity(bId, JPH::Vec3(v.velocity.x, v.velocity.y, v.velocity.z));
                        }
                    }
                }

                if (entJson.contains("rigidBody") && m_registry->get().all_of<ecs::RigidBodyComponent>(entity)) {
                    auto& rb = m_registry->get().get<ecs::RigidBodyComponent>(entity);
                    if (entJson["rigidBody"].contains("mass")) {
                        rb.mass = entJson["rigidBody"]["mass"];
                    }
                }

                if (entJson.contains("impulse") && m_physicsContext && m_registry->get().all_of<ecs::RigidBodyComponent>(entity)) {
                    auto& rb = m_registry->get().get<ecs::RigidBodyComponent>(entity);
                    if (rb.bodyId != 0xFFFFFFFF) {
                        JPH::BodyID bId(rb.bodyId);
                        float ix = entJson["impulse"].value("x", 0.0f);
                        float iy = entJson["impulse"].value("y", 0.0f);
                        float iz = entJson["impulse"].value("z", 0.0f);
                        m_physicsContext->getBodyInterface().AddImpulse(bId, JPH::Vec3(ix, iy, iz));
                    }
                }
            }
        } catch (...) {}
    }

    void Engine::run() {
        core::Logger::info("Engine started. Headless: " + std::string(m_headless ? "true" : "false"));
        auto lastTime = std::chrono::high_resolution_clock::now();
        while (m_running) {
            try {
#ifdef USE_GLFW
                if (!m_headless && m_vulkanContext && m_vulkanContext->getWindow()) {
                    glfwPollEvents();
                    GLFWwindow* win = m_vulkanContext->getWindow();
                    if (glfwWindowShouldClose(win)) {
                        m_running = false;
                        break;
                    }
                    auto now = std::chrono::high_resolution_clock::now();
                    float dt = std::chrono::duration<float>(now - lastTime).count();
                    bool imguiWantsInput = (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard);
                    if (m_paused) {
                        if (!imguiWantsInput) {
                            float speed = 20.0f * dt;
                            glm::vec3 front;
                            front.x = cos(glm::radians(m_camYaw)) * cos(glm::radians(m_camPitch));
                            front.y = sin(glm::radians(m_camPitch));
                            front.z = sin(glm::radians(m_camYaw)) * cos(glm::radians(m_camPitch));
                            front = glm::normalize(front);
                            glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0,1,0)));
                            glm::vec3 up = glm::normalize(glm::cross(right, front));

                            static double s_lastMouseX = 0.0;
                            static double s_lastMouseY = 0.0;
                            static bool s_firstMouse = true;
                            double curMouseX, curMouseY;
                            glfwGetCursorPos(win, &curMouseX, &curMouseY);
                            if (s_firstMouse) {
                                s_lastMouseX = curMouseX;
                                s_lastMouseY = curMouseY;
                                s_firstMouse = false;
                            }
                            if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
                                float dx = static_cast<float>(curMouseX - s_lastMouseX);
                                float dy = static_cast<float>(curMouseY - s_lastMouseY);
                                m_camYaw += dx * 0.15f;
                                m_camPitch -= dy * 0.15f;
                            }
                            s_lastMouseX = curMouseX;
                            s_lastMouseY = curMouseY;

                            if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) m_camPos += front * speed;
                            if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) m_camPos -= front * speed;
                            if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) m_camPos -= right * speed;
                            if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) m_camPos += right * speed;
                            if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS) m_camPos += up * speed;
                            if (glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS) m_camPos -= up * speed;
                            
                            if (glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS) m_camYaw -= 90.0f * dt;
                            if (glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS) m_camYaw += 90.0f * dt;
                            if (glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS) m_camPitch += 90.0f * dt;
                            if (glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS) m_camPitch -= 90.0f * dt;
                            if (m_camPitch > 89.0f) m_camPitch = 89.0f;
                            if (m_camPitch < -89.0f) m_camPitch = -89.0f;
                        }
                    } else {
                        // Native Gameplay Controls in GLFW Desktop Window
                        nlohmann::json keys = nlohmann::json::object();
                        if (!imguiWantsInput) {
                            if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS) {
                                keys["KeyA"] = true;
                                keys["ArrowLeft"] = true;
                            }
                            if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS) {
                                keys["KeyD"] = true;
                                keys["ArrowRight"] = true;
                            }
                            if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS) {
                                keys["KeyW"] = true;
                                keys["ArrowUp"] = true;
                            }
                            if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS) {
                                keys["KeyS"] = true;
                                keys["ArrowDown"] = true;
                            }
                            if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) {
                                keys["Space"] = true;
                            }
                            if (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS) {
                                keys["KeyR"] = true;
                            }
                        }
                        m_inputState["keys"] = keys;
                    }
                }
#endif
                while (auto cmd = m_commandQueue.pop()) {
                    try {
                        if (*cmd) (*cmd)(this);
                    } catch (const std::exception& e) {
                        core::Logger::error(std::string("Command exception: ") + e.what());
                    } catch (...) {
                        core::Logger::error("Unknown command exception in queue");
                    }
                }
                auto current_now = std::chrono::high_resolution_clock::now();
                float dt_real = std::chrono::duration<float>(current_now - lastTime).count();
                lastTime = current_now;
                if (!m_paused) {
                    try { update(dt_real); } catch (const std::exception& e) { core::Logger::error(std::string("Update exception: ") + e.what()); }
                }
                if (!m_headless) {
                    try { render(); } catch(const std::exception& e) { core::Logger::error(std::string("Render exception: ") + e.what()); }
                }

                if (m_headless || m_paused) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
            } catch (const std::exception& e) {
                core::Logger::error(std::string("Loop exception: ") + e.what());
            } catch (...) {
                core::Logger::error("Unknown loop exception in Engine::run");
            }
        }
    }

    void Engine::render() {
        glm::vec3 front;
        front.x = cos(glm::radians(m_camYaw)) * cos(glm::radians(m_camPitch));
        front.y = sin(glm::radians(m_camPitch));
        front.z = sin(glm::radians(m_camYaw)) * cos(glm::radians(m_camPitch));
        front = glm::normalize(front);
        glm::mat4 viewMatrix = glm::lookAt(m_camPos, m_camPos + front, glm::vec3(0.0f, 1.0f, 0.0f));
        m_renderer->render(*m_registry, m_assetManager.get(), viewMatrix, this);
    }
    
    bool Engine::loadScript(const std::string& code) {
        core::Logger::info("Loading new script into QuickJS...");
        m_currentScriptCode = code;
        return m_scriptContext->evaluate(code);
    }

    uint32_t Engine::spawnEntityFromJson(const nlohmann::json& components) {
        auto entity = m_registry->createEntity();
        
        if (components.contains("tag")) {
            ecs::TagComponent tag;
            if (components["tag"].is_string()) {
                tag.name = components["tag"].get<std::string>();
            } else if (components["tag"].is_object() && components["tag"].contains("name")) {
                tag.name = components["tag"]["name"].get<std::string>();
            }
            m_registry->get().emplace<ecs::TagComponent>(entity, tag);
        }

        if (components.contains("transform")) {
            ecs::TransformComponent t;
            const auto& trans = components["transform"];
            if (trans.contains("x")) t.position.x = trans["x"];
            if (trans.contains("y")) t.position.y = trans["y"];
            if (trans.contains("z")) t.position.z = trans["z"];
            if (trans.contains("rx")) t.rotation.x = trans["rx"];
            if (trans.contains("ry")) t.rotation.y = trans["ry"];
            if (trans.contains("rz")) t.rotation.z = trans["rz"];
            if (trans.contains("sx")) t.scale.x = trans["sx"];
            if (trans.contains("sy")) t.scale.y = trans["sy"];
            if (trans.contains("sz")) t.scale.z = trans["sz"];
            m_registry->get().emplace<ecs::TransformComponent>(entity, t);
        } else {
            m_registry->get().emplace<ecs::TransformComponent>(entity, ecs::TransformComponent{});
        }

        if (components.contains("velocity")) {
            ecs::VelocityComponent v;
            const auto& vel = components["velocity"];
            if (vel.contains("vx")) v.velocity.x = vel["vx"];
            if (vel.contains("vy")) v.velocity.y = vel["vy"];
            if (vel.contains("vz")) v.velocity.z = vel["vz"];
            m_registry->get().emplace<ecs::VelocityComponent>(entity, v);
        }

        if (components.contains("mesh")) {
            ecs::MeshComponent m;
            const auto& mesh = components["mesh"];
            m.meshId = mesh.value("meshId", 2); // Default to cube mesh
            m.materialId = mesh.value("materialId", 0);
            m_registry->get().emplace<ecs::MeshComponent>(entity, m);
        }

        if (components.contains("rigidBody")) {
            ecs::RigidBodyComponent rb;
            const auto& rbJson = components["rigidBody"];
            rb.mass = rbJson.value("mass", 1.0f);
            rb.shapeType = rbJson.value("shapeType", 0);
            rb.restitution = rbJson.value("restitution", 0.6f);
            rb.friction = rbJson.value("friction", 0.3f);
            rb.capsuleRadius = rbJson.value("capsuleRadius", 0.5f);
            rb.capsuleHalfHeight = rbJson.value("capsuleHalfHeight", 0.8f);
            rb.lockRotationX = rbJson.value("lockRotationX", false);
            rb.lockRotationY = rbJson.value("lockRotationY", false);
            rb.lockRotationZ = rbJson.value("lockRotationZ", false);
            rb.isSensor = rbJson.value("isSensor", false);
            if (rbJson.contains("halfExtents")) {
                rb.halfExtents.x = rbJson["halfExtents"].value("x", 0.5f);
                rb.halfExtents.y = rbJson["halfExtents"].value("y", 0.5f);
                rb.halfExtents.z = rbJson["halfExtents"].value("z", 0.5f);
            }
            m_registry->get().emplace<ecs::RigidBodyComponent>(entity, rb);
        }

        if (components.contains("material")) {
            ecs::MaterialComponent mat;
            const auto& matJson = components["material"];
            if (matJson.contains("color") && matJson["color"].is_array() && matJson["color"].size() >= 3) {
                mat.baseColor.r = matJson["color"][0].get<float>();
                mat.baseColor.g = matJson["color"][1].get<float>();
                mat.baseColor.b = matJson["color"][2].get<float>();
                mat.baseColor.a = matJson["color"].size() >= 4 ? matJson["color"][3].get<float>() : 1.0f;
            } else if (matJson.contains("r")) {
                mat.baseColor.r = matJson.value("r", 1.0f);
                mat.baseColor.g = matJson.value("g", 1.0f);
                mat.baseColor.b = matJson.value("b", 1.0f);
                mat.baseColor.a = matJson.value("a", 1.0f);
            }
            mat.metallic = matJson.value("metallic", 0.1f);
            mat.roughness = matJson.value("roughness", 0.5f);
            m_registry->get().emplace<ecs::MaterialComponent>(entity, mat);
        } else if (components.contains("color")) {
            ecs::MaterialComponent mat;
            const auto& col = components["color"];
            if (col.is_array() && col.size() >= 3) {
                float r = col[0].get<float>();
                float g = col[1].get<float>();
                float b = col[2].get<float>();
                float a = col.size() >= 4 ? col[3].get<float>() : 1.0f;
                mat.baseColor = glm::vec4(r, g, b, a);
            }
            m_registry->get().emplace<ecs::MaterialComponent>(entity, mat);
        }

        return static_cast<uint32_t>(entity);
    }

    bool Engine::destroyEntity(uint32_t id) {
        auto entity = static_cast<entt::entity>(id);
        if (!m_registry->get().valid(entity)) return false;

        if (m_registry->get().all_of<ecs::RigidBodyComponent>(entity)) {
            auto& rb = m_registry->get().get<ecs::RigidBodyComponent>(entity);
            if (rb.bodyId != 0xFFFFFFFF && m_physicsContext) {
                try {
                    JPH::BodyID bId(rb.bodyId);
                    auto& bi = m_physicsContext->getBodyInterface();
                    if (bi.IsAdded(bId)) {
                        bi.RemoveBody(bId);
                    }
                    bi.DestroyBody(bId);
                } catch (...) {}
                rb.bodyId = 0xFFFFFFFF;
            }
        }

        m_registry->get().destroy(entity);
        return true;
    }

    void Engine::resetScene(const std::string& preset) {
        auto view = m_registry->get().view<ecs::TransformComponent>();
        std::vector<entt::entity> toDelete;
        for (auto entity : view) {
            toDelete.push_back(entity);
        }
        for (auto entity : toDelete) {
            destroyEntity(static_cast<uint32_t>(entity));
        }

        if (preset == "solar") {
            // Sun
            spawnEntityFromJson({
                {"tag", "Sun"},
                {"transform", {{"x", 0}, {"y", 0}, {"z", 0}, {"sx", 3}, {"sy", 3}, {"sz", 3}}},
                {"velocity", {{"vx", 0}, {"vy", 0}, {"vz", 0}}},
                {"mesh", {{"meshId", 2}}},
                {"color", {0.98f, 0.80f, 0.08f, 1.0f}}
            });

            // Earth
            spawnEntityFromJson({
                {"tag", "Earth"},
                {"transform", {{"x", 12}, {"y", 0}, {"z", 0}, {"sx", 1}, {"sy", 1}, {"sz", 1}}},
                {"velocity", {{"vx", 0}, {"vy", 0}, {"vz", 4.5}}},
                {"mesh", {{"meshId", 2}}},
                {"color", {0.22f, 0.74f, 0.97f, 1.0f}}
            });

            // Mars
            spawnEntityFromJson({
                {"tag", "Mars"},
                {"transform", {{"x", -20}, {"y", 0}, {"z", 0}, {"sx", 0.8}, {"sy", 0.8}, {"sz", 0.8}}},
                {"velocity", {{"vx", 0}, {"vy", 0}, {"vz", -3.5}}},
                {"mesh", {{"meshId", 2}}},
                {"color", {0.97f, 0.44f, 0.44f, 1.0f}}
            });
            core::Logger::info("Scene reset to: Solar Preset");
        } else if (preset == "breakout") {
            // Reset score
            m_score.store(0);

            // Paddle
            spawnEntityFromJson({
                {"tag", "Paddle"},
                {"transform", {{"x", 0}, {"y", -8}, {"z", 0}, {"sx", 4}, {"sy", 0.8}, {"sz", 1}}},
                {"velocity", {{"vx", 0}, {"vy", 0}, {"vz", 0}}},
                {"mesh", {{"meshId", 2}}},
                {"rigidBody", {{"mass", 0.0f}, {"shapeType", 0}, {"restitution", 1.0f}, {"friction", 0.0f}}},
                {"color", {0.06f, 0.73f, 0.51f, 1.0f}}
            });

            // Ball
            spawnEntityFromJson({
                {"tag", "Ball"},
                {"transform", {{"x", 0}, {"y", -5}, {"z", 0}, {"sx", 0.8}, {"sy", 0.8}, {"sz", 0.8}}},
                {"velocity", {{"vx", 6.0f}, {"vy", 8.0f}, {"vz", 0.0f}}},
                {"mesh", {{"meshId", 2}}},
                {"rigidBody", {{"mass", 1.0f}, {"shapeType", 1}, {"restitution", 1.0f}, {"friction", 0.0f}}},
                {"color", {0.96f, 0.25f, 0.37f, 1.0f}}
            });

            // Left Wall
            spawnEntityFromJson({
                {"tag", "WallLeft"},
                {"transform", {{"x", -11.0f}, {"y", 2.0f}, {"z", 0.0f}, {"sx", 0.8f}, {"sy", 22.0f}, {"sz", 1.0f}}},
                {"mesh", {{"meshId", 2}}},
                {"rigidBody", {{"mass", 0.0f}, {"shapeType", 0}, {"restitution", 1.0f}, {"friction", 0.0f}}},
                {"color", {0.3f, 0.4f, 0.5f, 1.0f}}
            });

            // Right Wall
            spawnEntityFromJson({
                {"tag", "WallRight"},
                {"transform", {{"x", 11.0f}, {"y", 2.0f}, {"z", 0.0f}, {"sx", 0.8f}, {"sy", 22.0f}, {"sz", 1.0f}}},
                {"mesh", {{"meshId", 2}}},
                {"rigidBody", {{"mass", 0.0f}, {"shapeType", 0}, {"restitution", 1.0f}, {"friction", 0.0f}}},
                {"color", {0.3f, 0.4f, 0.5f, 1.0f}}
            });

            // Top Wall
            spawnEntityFromJson({
                {"tag", "WallTop"},
                {"transform", {{"x", 0.0f}, {"y", 13.0f}, {"z", 0.0f}, {"sx", 23.0f}, {"sy", 0.8f}, {"sz", 1.0f}}},
                {"mesh", {{"meshId", 2}}},
                {"rigidBody", {{"mass", 0.0f}, {"shapeType", 0}, {"restitution", 1.0f}, {"friction", 0.0f}}},
                {"color", {0.3f, 0.4f, 0.5f, 1.0f}}
            });

            // Bricks
            for (int r = 0; r < 4; ++r) {
                for (int c = -4; c <= 4; ++c) {
                    spawnEntityFromJson({
                        {"tag", "Brick"},
                        {"transform", {{"x", c * 2.2f}, {"y", 5.0f + r * 1.2f}, {"z", 0}, {"sx", 1.8}, {"sy", 0.8}, {"sz", 0.8}}},
                        {"mesh", {{"meshId", 2}}},
                        {"rigidBody", {{"mass", 0.0f}, {"shapeType", 0}, {"restitution", 1.0f}, {"friction", 0.0f}}},
                        {"color", {0.66f, 0.33f, 0.97f, 1.0f}}
                    });
                }
            }
            core::Logger::info("Scene reset to: Breakout Preset");
        } else if (preset == "racer") {
            // Ground
            spawnEntityFromJson({
                {"tag", "Ground"},
                {"transform", {{"x", 0}, {"y", -1}, {"z", 0}, {"sx", 50}, {"sy", 1}, {"sz", 100}}},
                {"mesh", {{"meshId", 1}}},
                {"rigidBody", {{"mass", 0.0f}, {"shapeType", 0}}},
                {"color", {0.2f, 0.2f, 0.25f, 1.0f}}
            });
            // Player Car
            spawnEntityFromJson({
                {"tag", "PlayerCar"},
                {"transform", {{"x", 0}, {"y", 0.5}, {"z", 8}, {"sx", 2}, {"sy", 0.8}, {"sz", 3}}},
                {"velocity", {{"vx", 0}, {"vy", 0}, {"vz", 0}}},
                {"mesh", {{"meshId", 2}}},
                {"rigidBody", {{"mass", 2.0f}, {"shapeType", 0}}},
                {"color", {0.95f, 0.15f, 0.15f, 1.0f}}
            });
            // Traffic obstacles
            for (int i = 0; i < 5; ++i) {
                float ox = ((i % 3) - 1.0f) * 6.0f;
                float oz = -10.0f - i * 8.0f;
                spawnEntityFromJson({
                    {"tag", "Traffic"},
                    {"transform", {{"x", ox}, {"y", 0.5}, {"z", oz}, {"sx", 1.5}, {"sy", 1.2}, {"sz", 2.5}}},
                    {"velocity", {{"vx", 0}, {"vy", 0}, {"vz", 0}}},
                    {"mesh", {{"meshId", 2}}},
                    {"rigidBody", {{"mass", 5.0f}, {"shapeType", 0}}},
                    {"color", {0.2f, 0.6f, 0.95f, 1.0f}}
                });
            }
            core::Logger::info("Scene reset to: Racer Preset");
        } else if (preset == "platformer") {
            // Ground
            spawnEntityFromJson({
                {"tag", "Ground"},
                {"transform", {{"x", 0}, {"y", -1}, {"z", 0}, {"sx", 50}, {"sy", 1}, {"sz", 50}}},
                {"mesh", {{"meshId", 1}}},
                {"rigidBody", {{"mass", 0.0f}, {"shapeType", 0}}}
            });
            // Player
            spawnEntityFromJson({
                {"tag", "Player"},
                {"transform", {{"x", 0}, {"y", 0.5}, {"z", 5}, {"sx", 1.2}, {"sy", 1.2}, {"sz", 1.2}}},
                {"velocity", {{"vx", 0}, {"vy", 0}, {"vz", 0}}},
                {"mesh", {{"meshId", 2}}},
                {"rigidBody", {{"mass", 1.0f}, {"shapeType", 0}}}
            });
            // Coins
            for (int i = 0; i < 8; ++i) {
                float cx = ((i % 4) - 1.5f) * 4.0f;
                float cz = ((i / 4) - 0.5f) * 8.0f;
                spawnEntityFromJson({
                    {"tag", "Coin"},
                    {"transform", {{"x", cx}, {"y", 0.6}, {"z", cz}, {"sx", 0.6}, {"sy", 0.6}, {"sz", 0.6}}},
                    {"velocity", {{"vx", 0}, {"vy", 0}, {"vz", 0}}},
                    {"mesh", {{"meshId", 2}}},
                    {"rigidBody", {{"mass", 0.1f}, {"shapeType", 1}}}
                });
            }
            core::Logger::info("Scene reset to: Platformer Preset");
        } else if (preset == "shooter") {
            // Player Ship
            spawnEntityFromJson({
                {"tag", "PlayerShip"},
                {"transform", {{"x", 0}, {"y", 0.5}, {"z", 12}, {"sx", 2}, {"sy", 0.5}, {"sz", 2}}},
                {"velocity", {{"vx", 0}, {"vy", 0}, {"vz", 0}}},
                {"mesh", {{"meshId", 2}}},
                {"rigidBody", {{"mass", 1.0f}, {"shapeType", 0}}}
            });
            // Meteors
            for (int i = 0; i < 6; ++i) {
                float mx = ((i % 3) - 1.0f) * 7.0f;
                float mz = -15.0f - i * 6.0f;
                spawnEntityFromJson({
                    {"tag", "Meteor"},
                    {"transform", {{"x", mx}, {"y", 0.5}, {"z", mz}, {"sx", 1.2}, {"sy", 1.2}, {"sz", 1.2}}},
                    {"velocity", {{"vx", 0}, {"vy", 0}, {"vz", 0}}},
                    {"mesh", {{"meshId", 2}}},
                    {"rigidBody", {{"mass", 1.0f}, {"shapeType", 1}}}
                });
            }
            core::Logger::info("Scene reset to: Shooter Preset");
        } else if (preset == "empty") {
            // Ground plane only
            auto ground = m_registry->createEntity();
            m_registry->get().emplace<ecs::TagComponent>(ground, ecs::TagComponent{"Ground"});
            m_registry->get().emplace<ecs::TransformComponent>(ground, glm::vec3(0.0f, -1.0f, 0.0f));
            ecs::MeshComponent groundMesh;
            groundMesh.meshId = 1;
            m_registry->get().emplace<ecs::MeshComponent>(ground, groundMesh);
            ecs::RigidBodyComponent groundRb;
            groundRb.mass = 0.0f;
            groundRb.shapeType = 0;
            groundRb.halfExtents = glm::vec3(50.0f, 1.0f, 50.0f);
            m_registry->get().emplace<ecs::RigidBodyComponent>(ground, groundRb);
            core::Logger::info("Scene reset to: Empty Preset");
        } else {
            initTestScene();
        }

        m_currentPreset = preset;
        m_score.store(0);
        m_health.store(100);
        m_gameStatus = "PLAYING";
        loadScript(getDefaultScript(preset));
    }

    std::string Engine::getDefaultScript(const std::string& preset) {
        if (preset == "breakout") {
            return R"(// --- NATIVE QUICKJS BREAKOUT GAMEPLAY CONTROLLER ---
function update(entities, dt, input, contacts) {
  var paddle = entities.find(e => e.tag === 'Paddle');
  if (paddle && paddle.transform) {
    var speed = 20.0;
    if (input && input.keys && (input.keys.KeyA || input.keys.ArrowLeft)) {
      paddle.transform.x = Math.max(-9.0, paddle.transform.x - speed * dt);
    }
    if (input && input.keys && (input.keys.KeyD || input.keys.ArrowRight)) {
      paddle.transform.x = Math.min(9.0, paddle.transform.x + speed * dt);
    }
  }
  var ball = entities.find(e => e.tag === 'Ball');
  if (ball && ball.transform) {
    if (ball.transform.y < -5.0 && typeof engine !== 'undefined' && engine.setGameStatus) {
      engine.setGameStatus('GAMEOVER');
      engine.playSfx('gameover', 1.0);
    }
  }
  if (contacts && contacts.length > 0 && typeof engine !== 'undefined') {
    for (var i = 0; i < contacts.length; i++) {
      var c = contacts[i];
      var ea = entities.find(e => e.id === c.a);
      var eb = entities.find(e => e.id === c.b);
      if (!ea || !eb) continue;
      if ((ea.tag === 'Ball' && eb.tag && eb.tag.startsWith('Brick')) ||
          (eb.tag === 'Ball' && ea.tag && ea.tag.startsWith('Brick'))) {
        var brick = ea.tag.startsWith('Brick') ? ea : eb;
        engine.destroyEntity(brick.id);
        engine.setScore(engine.getScore() + 10);
        engine.playSfx('coin', 0.8);
      } else if (ea.tag === 'Ball' || eb.tag === 'Ball') {
        engine.playSfx('hit', 0.6);
      }
    }
  }
}
)";
        } else if (preset == "platformer") {
            return R"(// --- NATIVE QUICKJS PLATFORMER CONTROLLER ---
function update(entities, dt, input, contacts) {
  var player = entities.find(e => e.tag === 'Player');
  if (!player || !player.transform) return;
  var speed = 12.0;
  if (!player.velocity) player.velocity = { vx: 0, vy: 0, vz: 0 };
  var moveX = 0, moveZ = 0;
  if (input && input.keys && (input.keys.KeyW || input.keys.ArrowUp)) moveZ -= 1;
  if (input && input.keys && (input.keys.KeyS || input.keys.ArrowDown)) moveZ += 1;
  if (input && input.keys && (input.keys.KeyA || input.keys.ArrowLeft)) moveX -= 1;
  if (input && input.keys && (input.keys.KeyD || input.keys.ArrowRight)) moveX += 1;
  player.velocity.vx = moveX * speed;
  player.velocity.vz = moveZ * speed;
  if (input && input.keys && input.keys.Space && Math.abs(player.velocity.vy) < 0.2) {
    player.velocity.vy = 8.5;
    if (typeof engine !== 'undefined' && engine.playSfx) engine.playSfx('jump', 0.8);
  }
  if (contacts && contacts.length > 0 && typeof engine !== 'undefined') {
    for (var i = 0; i < contacts.length; i++) {
      var c = contacts[i];
      var ea = entities.find(e => e.id === c.a);
      var eb = entities.find(e => e.id === c.b);
      if (!ea || !eb) continue;
      if ((ea.tag === 'Player' && eb.tag === 'Coin') || (eb.tag === 'Player' && ea.tag === 'Coin')) {
        var coin = ea.tag === 'Coin' ? ea : eb;
        engine.destroyEntity(coin.id);
        engine.setScore(engine.getScore() + 50);
        engine.playSfx('coin', 1.0);
      }
    }
  }
}
)";
        } else if (preset == "shooter") {
            return R"(// --- NATIVE QUICKJS SPACE SHOOTER CONTROLLER ---
var lastShootTime = 0;
function update(entities, dt, input, contacts) {
  var ship = entities.find(e => e.tag === 'PlayerShip');
  if (!ship || !ship.transform) return;
  var speed = 18.0;
  if (input && input.keys && (input.keys.KeyA || input.keys.ArrowLeft)) ship.transform.x = Math.max(-12.0, ship.transform.x - speed * dt);
  if (input && input.keys && (input.keys.KeyD || input.keys.ArrowRight)) ship.transform.x = Math.min(12.0, ship.transform.x + speed * dt);
  if (input && input.keys && (input.keys.KeyW || input.keys.ArrowUp)) ship.transform.z = Math.max(0.0, ship.transform.z - speed * dt);
  if (input && input.keys && (input.keys.KeyS || input.keys.ArrowDown)) ship.transform.z = Math.min(14.0, ship.transform.z + speed * dt);
  
  lastShootTime += dt;
  if (input && input.keys && input.keys.Space && lastShootTime > 0.25) {
    lastShootTime = 0;
    if (typeof engine !== 'undefined' && engine.playSfx) engine.playSfx('laser', 0.8);
  }
  if (contacts && contacts.length > 0 && typeof engine !== 'undefined') {
    for (var i = 0; i < contacts.length; i++) {
      var c = contacts[i];
      var ea = entities.find(e => e.id === c.a);
      var eb = entities.find(e => e.id === c.b);
      if (!ea || !eb) continue;
      if ((ea.tag === 'Laser' && eb.tag === 'Meteor') || (eb.tag === 'Laser' && ea.tag === 'Meteor')) {
        engine.destroyEntity(ea.id);
        engine.destroyEntity(eb.id);
        engine.setScore(engine.getScore() + 100);
        engine.playSfx('hit', 1.0);
      }
    }
  }
}
)";
        } else if (preset == "racer") {
            return R"(// --- NATIVE QUICKJS HOVER RACER CONTROLLER ---
var currentSpeed = 0.0;
function update(entities, dt, input, contacts) {
  var car = entities.find(e => e.tag === 'PlayerCar');
  if (!car || !car.transform) return;
  var maxSpeed = 35.0, accel = 18.0;
  if (input && input.keys && (input.keys.KeyW || input.keys.ArrowUp)) currentSpeed = Math.min(maxSpeed, currentSpeed + accel * dt);
  else if (input && input.keys && (input.keys.KeyS || input.keys.ArrowDown)) currentSpeed = Math.max(-10.0, currentSpeed - accel * dt);
  else currentSpeed *= 0.95;
  if (!car.velocity) car.velocity = { vx: 0, vy: 0, vz: 0 };
  car.velocity.vz = -currentSpeed;
  if (input && input.keys && (input.keys.KeyA || input.keys.ArrowLeft)) car.transform.x -= 15.0 * dt;
  if (input && input.keys && (input.keys.KeyD || input.keys.ArrowRight)) car.transform.x += 15.0 * dt;
}
)";
        }
        return "function update(entities, dt, input, contacts) {}\n";
    }

    std::string Engine::captureFrameBase64() {
        if (m_headless) {
            glm::vec3 front;
            front.x = cos(glm::radians(m_camYaw)) * cos(glm::radians(m_camPitch));
            front.y = sin(glm::radians(m_camPitch));
            front.z = sin(glm::radians(m_camYaw)) * cos(glm::radians(m_camPitch));
            front = glm::normalize(front);
            glm::mat4 viewMatrix = glm::lookAt(m_camPos, m_camPos + front, glm::vec3(0.0f, 1.0f, 0.0f));
            try {
                m_renderer->render(*m_registry, m_assetManager.get(), viewMatrix, this);
            } catch (...) {}
        }
        return m_renderer->captureFrameBase64();
    }

    void Engine::stepSimulation(int steps, float dt) {
        for (int i = 0; i < steps; ++i) {
            update(dt);
        }
        render(); // render last frame
    }

    nlohmann::json Engine::getSnapshotJson() {
        nlohmann::json snap = nlohmann::json::array();
        auto view = m_registry->get().view<ecs::TransformComponent>();
        for (auto [entity, transform] : view.each()) {
            nlohmann::json entJson = {
                {"id", static_cast<uint32_t>(entity)},
                {"transform", {
                    {"x", transform.position.x},
                    {"y", transform.position.y},
                    {"z", transform.position.z},
                    {"rx", transform.rotation.x},
                    {"ry", transform.rotation.y},
                    {"rz", transform.rotation.z},
                    {"qx", transform.orientation.x},
                    {"qy", transform.orientation.y},
                    {"qz", transform.orientation.z},
                    {"qw", transform.orientation.w},
                    {"sx", transform.scale.x},
                    {"sy", transform.scale.y},
                    {"sz", transform.scale.z}
                }}
            };
            if (m_registry->get().all_of<ecs::TagComponent>(entity)) {
                entJson["tag"] = m_registry->get().get<ecs::TagComponent>(entity).name;
            }
            if (m_registry->get().all_of<ecs::VelocityComponent>(entity)) {
                auto& vel = m_registry->get().get<ecs::VelocityComponent>(entity);
                entJson["velocity"] = {
                    {"vx", vel.velocity.x},
                    {"vy", vel.velocity.y},
                    {"vz", vel.velocity.z}
                };
            }
            if (m_registry->get().all_of<ecs::MeshComponent>(entity)) {
                auto& m = m_registry->get().get<ecs::MeshComponent>(entity);
                entJson["mesh"] = {
                    {"meshId", m.meshId},
                    {"materialId", m.materialId}
                };
            }
            if (m_registry->get().all_of<ecs::RigidBodyComponent>(entity)) {
                auto& rb = m_registry->get().get<ecs::RigidBodyComponent>(entity);
                entJson["rigidBody"] = {
                    {"mass", rb.mass},
                    {"shapeType", rb.shapeType},
                    {"restitution", rb.restitution},
                    {"friction", rb.friction},
                    {"capsuleRadius", rb.capsuleRadius},
                    {"capsuleHalfHeight", rb.capsuleHalfHeight},
                    {"lockRotationX", rb.lockRotationX},
                    {"lockRotationY", rb.lockRotationY},
                    {"lockRotationZ", rb.lockRotationZ},
                    {"isSensor", rb.isSensor}
                };
            }
            if (m_registry->get().all_of<ecs::MaterialComponent>(entity)) {
                auto& mat = m_registry->get().get<ecs::MaterialComponent>(entity);
                entJson["color"] = { mat.baseColor.r, mat.baseColor.g, mat.baseColor.b, mat.baseColor.a };
                entJson["material"] = {
                    {"color", { mat.baseColor.r, mat.baseColor.g, mat.baseColor.b, mat.baseColor.a }},
                    {"metallic", mat.metallic},
                    {"roughness", mat.roughness}
                };
            }
            snap.push_back(entJson);
        }
        return snap;
    }

    std::string Engine::getTypeDefinitions() {
        return ecs::TypeExporter::generateTypeScriptDefinitions();
    }

    void Engine::initTestScene() {
        // Ground (Static)
        auto ground = m_registry->createEntity();
        m_registry->get().emplace<ecs::TagComponent>(ground, ecs::TagComponent{"GroundPlane"});
        m_registry->get().emplace<ecs::TransformComponent>(ground, glm::vec3(0.0f, -2.0f, 0.0f));
        
        ecs::MeshComponent groundMesh;
        groundMesh.meshId = 1; // Generic floor mesh
        m_registry->get().emplace<ecs::MeshComponent>(ground, groundMesh);
        
        ecs::RigidBodyComponent groundRb;
        groundRb.mass = 0.0f; // Static
        groundRb.shapeType = 0; // Box
        groundRb.halfExtents = glm::vec3(50.0f, 2.0f, 50.0f);
        groundRb.restitution = 0.1f;
        groundRb.friction = 0.6f;
        m_registry->get().emplace<ecs::RigidBodyComponent>(ground, groundRb);

        // Falling Objects (Dynamic) - Organized in a neat physical cluster
        int count = 0;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 5; ++c) {
                auto entity = m_registry->createEntity();
                float x = (c - 2) * 2.2f;
                float z = (r - 1) * 2.2f;
                float y = 4.0f + (count * 1.5f);
                
                m_registry->get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"DynamicBody_" + std::to_string(count)});
                m_registry->get().emplace<ecs::TransformComponent>(entity, glm::vec3(x, y, z));
                
                ecs::MeshComponent dynMesh;
                dynMesh.meshId = 2; // Generic cube
                m_registry->get().emplace<ecs::MeshComponent>(entity, dynMesh);
                
                ecs::RigidBodyComponent dynRb;
                dynRb.mass = 2.0f;
                dynRb.shapeType = (count % 2 == 0) ? 0 : 1; // Alternate Box and Sphere
                dynRb.halfExtents = glm::vec3(0.5f, 0.5f, 0.5f);
                dynRb.restitution = 0.15f; // Realistic solid bounce, settles naturally
                dynRb.friction = 0.5f;
                m_registry->get().emplace<ecs::RigidBodyComponent>(entity, dynRb);
                count++;
            }
        }

        core::Logger::info("Test scene initialized: 1 static ground, 15 dynamic falling rigidbodies.");
    }

    void Engine::exportTypeDefinitions() {
        ecs::TypeExporter::exportToTypeScript("engine.d.ts");
    }

    void Engine::broadcastScriptError(const std::string& type, const std::string& msg, const std::string& stack) {
        nlohmann::json rpc = {
            {"jsonrpc", "2.0"},
            {"method", "engine.onScriptError"},
            {"params", {
                {"errorType", type},
                {"message", msg},
                {"stackTrace", stack}
            }}
        };
        m_wsServer->sendToAll(rpc.dump());
    }

    void Engine::broadcastSfx(const std::string& type, float volume) {
#ifdef _WIN32
        // Native C++ desktop audio playback directly from speakers
        std::thread([type]() {
            if (type == "hit") {
                MessageBeep(MB_ICONHAND);
            } else if (type == "coin" || type == "score") {
                MessageBeep(MB_OK);
            } else if (type == "jump") {
                MessageBeep(MB_ICONASTERISK);
            } else if (type == "laser") {
                MessageBeep(MB_ICONQUESTION);
            } else if (type == "gameover") {
                MessageBeep(MB_ICONEXCLAMATION);
            } else {
                MessageBeep(MB_OK);
            }
        }).detach();
#endif
        if (!m_wsServer) return;
        nlohmann::json rpc = {
            {"jsonrpc", "2.0"},
            {"method", "engine.onPlaySfx"},
            {"params", {
                {"type", type},
                {"volume", volume}
            }}
        };
        m_wsServer->sendToAll(rpc.dump());
    }

    void Engine::broadcastCameraShake(float intensity, float duration) {
        if (!m_wsServer) return;
        nlohmann::json rpc = {
            {"jsonrpc", "2.0"},
            {"method", "engine.onCameraShake"},
            {"params", {
                {"intensity", intensity},
                {"duration", duration}
            }}
        };
        m_wsServer->sendToAll(rpc.dump());
    }

    uint32_t Engine::loadGlbFromBase64(const std::string& base64Str) {
        auto glbData = decodeBase64(base64Str);
        if (glbData.empty()) return 0;
        
        uint32_t meshId = 0;
        if (m_assetManager) {
            meshId = m_assetManager->loadGLTFFromMemory(reinterpret_cast<const std::vector<uint8_t>&>(glbData));
        }
        
        if (meshId == 0) return 0;

        auto entity = m_registry->createEntity();
        
        ecs::TagComponent tag{"Imported_GLB_Mesh"};
        m_registry->get().emplace<ecs::TagComponent>(entity, tag);

        ecs::TransformComponent t;
        m_registry->get().emplace<ecs::TransformComponent>(entity, t);
        
        ecs::MeshComponent mesh;
        mesh.meshId = meshId;
        m_registry->get().emplace<ecs::MeshComponent>(entity, mesh);
        
        core::Logger::info("Successfully loaded GLB, Mesh ID: " + std::to_string(meshId));
        return static_cast<uint32_t>(entity);
    }

    void Engine::applyPbrMaterial(uint32_t entityId, const nlohmann::json& args) {
        auto entity = static_cast<entt::entity>(entityId);
        if (m_registry->get().valid(entity) && m_registry->get().all_of<ecs::MeshComponent>(entity)) {
            auto& mesh = m_registry->get().get<ecs::MeshComponent>(entity);
            
            static uint32_t appendOnlyTextureCounter = 500;
            if (args.contains("albedo_base64")) mesh.albedoIdx = ++appendOnlyTextureCounter;
            if (args.contains("normal_base64")) mesh.normalIdx = ++appendOnlyTextureCounter;
            if (args.contains("orm_base64")) mesh.ormIdx = ++appendOnlyTextureCounter;
            
            core::Logger::info("Applied PBR material to entity " + std::to_string(entityId));
        }
    }

    void Engine::updateEntityBones(uint32_t entityId, const std::vector<anim::BoneTransformRaw>& bones) {
        auto entity = static_cast<entt::entity>(entityId);
        if (m_registry->get().valid(entity)) {
            if (!m_registry->get().all_of<anim::SkeletalMeshComponent>(entity)) {
                m_registry->get().emplace<anim::SkeletalMeshComponent>(entity);
            }
            auto& skel = m_registry->get().get<anim::SkeletalMeshComponent>(entity);
            if (skel.boneMatrices.size() < bones.size()) {
                skel.boneMatrices.resize(bones.size(), glm::mat4(1.0f));
            }
            for (const auto& b : bones) {
                if (b.boneId < skel.boneMatrices.size()) {
                    glm::mat4 t = glm::translate(glm::mat4(1.0f), b.translation);
                    glm::mat4 r = glm::mat4_cast(b.rotation);
                    glm::mat4 s = glm::scale(glm::mat4(1.0f), b.scale);
                    skel.boneMatrices[b.boneId] = t * r * s;
                }
            }
        }
    }

    void Engine::sendWsMessage(ix::WebSocket* client, const std::string& msg) {
        if (m_wsServer) {
            m_wsServer->sendToClient(client, msg);
        }
    }

    bool Engine::saveProjectToDisk(const std::string& folderPath, const nlohmann::json& projectData) {
        try {
            std::filesystem::path dir(folderPath);
            std::string projName = projectData.value("name", dir.filename().string());
            if (projName.empty()) projName = "MyProject";

            // 1. Create Professional Unreal Engine 5.8 Folder Hierarchy
            std::filesystem::create_directories(dir / "Config");
            std::filesystem::create_directories(dir / "Content" / "Maps");
            std::filesystem::create_directories(dir / "Content" / "Blueprints");
            std::filesystem::create_directories(dir / "Content" / "Materials");
            std::filesystem::create_directories(dir / "Content" / "Meshes");
            std::filesystem::create_directories(dir / "Content" / "FX");
            std::filesystem::create_directories(dir / "Source" / projName);
            std::filesystem::create_directories(dir / "Saved" / "Logs");
            std::filesystem::create_directories(dir / "Saved" / "Config");

            // Also keep legacy folders for backwards compatibility
            std::filesystem::create_directories(dir / "scenes");
            std::filesystem::create_directories(dir / "scripts");
            std::filesystem::create_directories(dir / "materials");

            // 2. Save <ProjectName>.uproject (Unreal Engine 5.8 Standard Descriptor)
            nlohmann::json uproject = {
                {"FileVersion", 3},
                {"EngineAssociation", "5.8"},
                {"Category", ""},
                {"Description", projectData.value("description", "Lucid Engine 5.8 Project")},
                {"Modules", nlohmann::json::array({
                    {
                        {"Name", projName},
                        {"Type", "Runtime"},
                        {"LoadingPhase", "Default"}
                    }
                })},
                {"Plugins", nlohmann::json::array({
                    {
                        {"Name", "ModelingToolsEditorMode"},
                        {"Enabled", true},
                        {"TargetAllowList", nlohmann::json::array({"Editor"})}
                    }
                })},
                {"lucidEngine", projectData}
            };

            std::ofstream upFile(dir / (projName + ".uproject"));
            if (upFile.is_open()) {
                upFile << uproject.dump(2);
                upFile.close();
            }

            // Also save project.json in root for web/IPC compatibility
            std::ofstream pFile(dir / "project.json");
            if (pFile.is_open()) {
                pFile << projectData.dump(2);
                pFile.close();
            }

            // 3. Save Unreal Engine Config files (.ini)
            std::ofstream engineIni(dir / "Config" / "DefaultEngine.ini");
            if (engineIni.is_open()) {
                engineIni << "[/Script/Engine.Engine]\n"
                          << "NearClipPlane=3.0\n"
                          << "bSubstepping=true\n"
                          << "MaxSubstepDeltaTime=0.016667\n"
                          << "MaxSubsteps=6\n\n"
                          << "[/Script/Engine.RendererSettings]\n"
                          << "r.DynamicGlobalIlluminationMethod=1\n"
                          << "r.Lumen.Reflections.Allow=1\n"
                          << "r.AntiAliasingMethod=4\n"
                          << "r.VelocityOutput=1\n\n"
                          << "[/Script/LucidEngine.Physics]\n"
                          << "GravityZ=" << (projectData.contains("settings") ? projectData["settings"].value("gravity", -9.81f) : -9.81f) << "\n"
                          << "mConvexRadius=0.01\n"
                          << "MotionQuality=LinearCast\n";
                engineIni.close();
            }

            std::ofstream gameIni(dir / "Config" / "DefaultGame.ini");
            if (gameIni.is_open()) {
                gameIni << "[/Script/EngineSettings.GeneralProjectSettings]\n"
                        << "ProjectID=" << projectData.value("id", "proj_lucid") << "\n"
                        << "ProjectName=" << projName << "\n"
                        << "Description=" << projectData.value("description", "") << "\n"
                        << "ProjectVersion=1.0.0.0\n";
                gameIni.close();
            }

            std::ofstream inputIni(dir / "Config" / "DefaultInput.ini");
            if (inputIni.is_open()) {
                inputIni << "[/Script/Engine.InputSettings]\n"
                         << "+ActionMappings=(ActionName=\"Jump\",bShift=False,bCtrl=False,bAlt=False,bCmd=False,Key=SpaceBar)\n"
                         << "+ActionMappings=(ActionName=\"Fire\",bShift=False,bCtrl=False,bAlt=False,bCmd=False,Key=LeftMouseButton)\n"
                         << "+AxisMappings=(AxisName=\"MoveForward\",Scale=1.000000,Key=W)\n"
                         << "+AxisMappings=(AxisName=\"MoveForward\",Scale=-1.000000,Key=S)\n"
                         << "+AxisMappings=(AxisName=\"MoveRight\",Scale=1.000000,Key=D)\n"
                         << "+AxisMappings=(AxisName=\"MoveRight\",Scale=-1.000000,Key=A)\n";
                inputIni.close();
            }

            // 4. Save Scenes / Levels into Content/Maps/ (with Lvl_ prefix)
            if (projectData.contains("scenes") && projectData["scenes"].is_array()) {
                for (const auto& sc : projectData["scenes"]) {
                    std::string scName = sc.value("name", "Lvl_MainLevel.scene");
                    if (scName.find("Lvl_") != 0 && scName.find("Map_") != 0) {
                        scName = "Lvl_" + scName;
                    }
                    if (scName.find(".scene") == std::string::npos && scName.find(".umap") == std::string::npos) {
                        scName += ".scene";
                    }
                    std::ofstream scFile(dir / "Content" / "Maps" / scName);
                    if (scFile.is_open()) {
                        scFile << sc.dump(2);
                        scFile.close();
                    }
                    // Legacy path
                    std::ofstream scLegFile(dir / "scenes" / sc.value("name", scName));
                    if (scLegFile.is_open()) {
                        scLegFile << sc.dump(2);
                        scLegFile.close();
                    }
                }
            }

            // 5. Save Blueprints into Content/Blueprints/ (with BP_ prefix) and Source/<ProjectName>/
            if (projectData.contains("scripts") && projectData["scripts"].is_array()) {
                for (const auto& s : projectData["scripts"]) {
                    std::string sName = s.value("name", "script.js");
                    std::string bpName = sName;
                    if (bpName.find("BP_") != 0) bpName = "BP_" + bpName;
                    std::string sCode = s.value("code", "");

                    // Content/Blueprints/
                    std::ofstream bpFile(dir / "Content" / "Blueprints" / bpName);
                    if (bpFile.is_open()) {
                        bpFile << sCode;
                        bpFile.close();
                    }
                    // Source/<ProjectName>/
                    std::ofstream srcFile(dir / "Source" / projName / bpName);
                    if (srcFile.is_open()) {
                        srcFile << sCode;
                        srcFile.close();
                    }
                    // Legacy scripts/
                    std::ofstream sFile(dir / "scripts" / sName);
                    if (sFile.is_open()) {
                        sFile << sCode;
                        sFile.close();
                    }
                }
            }

            // 6. Save Materials into Content/Materials/ (with M_ / MI_ prefix)
            if (projectData.contains("materials") && projectData["materials"].is_array()) {
                for (const auto& m : projectData["materials"]) {
                    std::string mName = m.value("name", "Material");
                    if (mName.find("M_") != 0 && mName.find("MI_") != 0) mName = "M_" + mName;
                    if (mName.find(".json") == std::string::npos) mName += ".json";
                    std::ofstream mFile(dir / "Content" / "Materials" / mName);
                    if (mFile.is_open()) {
                        mFile << m.dump(2);
                        mFile.close();
                    }
                    std::ofstream legMatFile(dir / "materials" / mName);
                    if (legMatFile.is_open()) {
                        legMatFile << m.dump(2);
                        legMatFile.close();
                    }
                }
            }

            // 7. Save Saved/Logs/Execution.log
            std::ofstream logFile(dir / "Saved" / "Logs" / "Execution.log", std::ios::app);
            if (logFile.is_open()) {
                logFile << "LogInit: Lucid Engine 5.8 Project saved successfully at: " << folderPath << "\n";
                logFile.close();
            }

            core::Logger::info("Unreal Engine 5.8 project structure successfully saved to disk: " + folderPath);
            return true;
        } catch (const std::exception& e) {
            core::Logger::error("Failed to save project to disk: " + std::string(e.what()));
            return false;
        }
    }

    nlohmann::json Engine::loadProjectFromDisk(const std::string& folderPath) {
        try {
            std::filesystem::path dir(folderPath);
            std::filesystem::path targetFile;

            if (!std::filesystem::is_directory(dir)) {
                targetFile = dir;
            } else {
                for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".uproject") {
                        targetFile = entry.path();
                        break;
                    }
                }
                if (targetFile.empty() || !std::filesystem::exists(targetFile)) {
                    targetFile = dir / "project.json";
                }
            }

            if (!std::filesystem::exists(targetFile)) {
                return {{"error", "Neither .uproject nor project.json found in " + folderPath}};
            }

            std::ifstream file(targetFile);
            if (!file.is_open()) return {{"error", "Failed to open project file: " + targetFile.string()}};
            nlohmann::json j;
            file >> j;

            if (j.contains("lucidEngine") && j["lucidEngine"].is_object()) {
                core::Logger::info("Project loaded from .uproject: " + targetFile.string());
                return j["lucidEngine"];
            }

            core::Logger::info("Project loaded from disk: " + targetFile.string());
            return j;
        } catch (const std::exception& e) {
            return {{"error", e.what()}};
        }
    }

    nlohmann::json Engine::listDiskProjects(const std::string& baseDir) {
        nlohmann::json list = nlohmann::json::array();
        try {
            if (!std::filesystem::exists(baseDir)) return list;
            for (const auto& entry : std::filesystem::directory_iterator(baseDir)) {
                if (entry.is_directory()) {
                    std::filesystem::path pFile;
                    for (const auto& f : std::filesystem::directory_iterator(entry.path())) {
                        if (f.is_regular_file() && f.path().extension() == ".uproject") {
                            pFile = f.path();
                            break;
                        }
                    }
                    if (pFile.empty()) {
                        auto pJson = entry.path() / "project.json";
                        if (std::filesystem::exists(pJson)) pFile = pJson;
                    }

                    if (!pFile.empty() && std::filesystem::exists(pFile)) {
                        try {
                            std::ifstream f(pFile);
                            nlohmann::json meta;
                            f >> meta;
                            if (meta.contains("lucidEngine")) meta = meta["lucidEngine"];
                            meta["folderPath"] = entry.path().string();
                            list.push_back(meta);
                        } catch (...) {}
                    }
                }
            }
        } catch (...) {}
        return list;
    }

    void Engine::setStandaloneGameMode(bool enabled) {
        m_standaloneGameMode = enabled;
        if (m_renderer) {
            m_renderer->setStandaloneGameMode(enabled);
        }
    }

    bool Engine::packageStandaloneGame(const std::string& targetFolder, const std::string& gameName) {
        try {
            std::filesystem::path outDir(targetFolder);
            std::filesystem::path gameDir = outDir / gameName;
            std::filesystem::create_directories(gameDir);
            std::filesystem::create_directories(gameDir / "assets" / "models");
            std::filesystem::create_directories(gameDir / "shaders");

            // 1. Copy engine binary -> <gameName>.exe
            std::filesystem::path exeSource = "engine/build/Release/engine.exe";
            if (!std::filesystem::exists(exeSource)) exeSource = "engine.exe";
            if (std::filesystem::exists(exeSource)) {
                std::filesystem::copy_file(exeSource, gameDir / (gameName + ".exe"), std::filesystem::copy_options::overwrite_existing);
            }

            // 2. Copy SPIR-V PBR Shaders
            if (std::filesystem::exists("engine/build/Release/pbr.vert.spv")) {
                std::filesystem::copy_file("engine/build/Release/pbr.vert.spv", gameDir / "pbr.vert.spv", std::filesystem::copy_options::overwrite_existing);
            }
            if (std::filesystem::exists("engine/build/Release/pbr.frag.spv")) {
                std::filesystem::copy_file("engine/build/Release/pbr.frag.spv", gameDir / "pbr.frag.spv", std::filesystem::copy_options::overwrite_existing);
            }

            // 3. Copy 3D Models (UE 5.8 converted assets)
            std::filesystem::path modelsSrc = "public/assets/models";
            if (std::filesystem::exists(modelsSrc)) {
                for (const auto& f : std::filesystem::directory_iterator(modelsSrc)) {
                    if (f.is_regular_file()) {
                        std::filesystem::copy_file(f.path(), gameDir / "assets" / "models" / f.path().filename(), std::filesystem::copy_options::overwrite_existing);
                    }
                }
            }

            // 4. Save game project package descriptor (game.json)
            nlohmann::json gamePackage = {
                {"name", gameName},
                {"engineAssociation", "Lucid 5.8 Native Vulkan Runtime"},
                {"preset", m_currentPreset},
                {"entities", getSnapshotJson()},
                {"script", m_currentScriptCode.empty() ? getDefaultScript(m_currentPreset) : m_currentScriptCode},
                {"settings", {
                    {"gravity", -9.81f},
                    {"fullscreen", false},
                    {"windowWidth", 1920},
                    {"windowHeight", 1080}
                }}
            };

            std::ofstream pkgFile(gameDir / "game.json");
            if (pkgFile.is_open()) {
                pkgFile << gamePackage.dump(2);
                pkgFile.close();
            }

            // 5. Create 1-click launcher script RunGame.bat
            std::ofstream batFile(gameDir / "RunGame.bat");
            if (batFile.is_open()) {
                batFile << "@echo off\n"
                        << "title " << gameName << " - Lucid Engine 5.8 (Vulkan 1.3 PC Game)\n"
                        << "start \"\" \"%~dp0" << gameName << ".exe\" --game game.json\n";
                batFile.close();
            }

            core::Logger::info("Game successfully packaged to standalone binary: " + gameDir.string());
            return true;
        } catch (const std::exception& e) {
            core::Logger::error(std::string("Package Game failed: ") + e.what());
            return false;
        }
    }
}
