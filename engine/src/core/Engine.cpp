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
#include "renderer/EditorLocalization.hpp"
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
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <unordered_set>

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

    static glm::vec4 parseColorJson(const nlohmann::json& j, const glm::vec4& fallback = glm::vec4(1.0f)) {
        if (j.is_string()) {
            std::string s = j.get<std::string>();
            if (!s.empty() && s[0] == '#') s = s.substr(1);
            if (s.length() == 6 || s.length() == 8) {
                try {
                    uint32_t val = std::stoul(s, nullptr, 16);
                    if (s.length() == 6) {
                        float r = ((val >> 16) & 0xFF) / 255.0f;
                        float g = ((val >> 8) & 0xFF) / 255.0f;
                        float b = (val & 0xFF) / 255.0f;
                        return glm::vec4(r, g, b, 1.0f);
                    } else {
                        float r = ((val >> 24) & 0xFF) / 255.0f;
                        float g = ((val >> 16) & 0xFF) / 255.0f;
                        float b = ((val >> 8) & 0xFF) / 255.0f;
                        float a = (val & 0xFF) / 255.0f;
                        return glm::vec4(r, g, b, a);
                    }
                } catch (...) {}
            }
        } else if (j.is_array() && j.size() >= 3) {
            float r = j[0].get<float>();
            float g = j[1].get<float>();
            float b = j[2].get<float>();
            float a = j.size() >= 4 ? j[3].get<float>() : 1.0f;
            return glm::vec4(r, g, b, a);
        } else if (j.is_object() && j.contains("r") && j.contains("g") && j.contains("b")) {
            return glm::vec4(j.value("r", 1.0f), j.value("g", 1.0f), j.value("b", 1.0f), j.value("a", 1.0f));
        }
        return fallback;
    }

    Engine::Engine(int wsPort, bool headless, bool enableRemote) : m_headless(headless) {
        m_registry = std::make_unique<ecs::Registry>();
        m_scriptContext = std::make_unique<script::QuickJSContext>();
        m_scriptContext->init(this);
        m_scriptContext->setMemoryLimit(64 * 1024 * 1024); // 64 MB AI script limit
        m_scriptContext->setOnError([this](const std::string& type, const std::string& msg, const std::string& stack) {
            broadcastScriptError(type, msg, stack);
        });
        
        m_sharedStateRing = std::make_unique<network::SharedStateRingBuffer>();
        m_mpscCommandQueue = std::make_unique<network::MpscCommandQueue>();
        if (enableRemote && wsPort > 0) { m_unifiedServer = std::make_unique<network::SinglePortUnifiedServer>(wsPort, *m_sharedStateRing, *m_mpscCommandQueue, m_commandQueue); } else { core::Logger::info("Pure Local Mode active (--no-remote): 0 network ports opened."); }
        if (enableRemote && wsPort > 0) { m_udpServer = std::make_unique<network::UdpAnimationServer>(9001, m_commandQueue); }
        m_mcpServer = std::make_unique<network::mcp::McpServer>(this);
        
        m_physicsContext = std::make_unique<physics::PhysicsContext>();
        m_physicsContext->init();
        m_physicsSystem = std::make_unique<physics::PhysicsSystem>(m_physicsContext.get());
        m_chemistrySystem = std::make_unique<physics::ChemistrySystem>();
        m_destructionSystem = std::make_unique<physics::DestructionSystem>();
        m_navigationSystem = std::make_unique<ai::NavigationSystem>();
        m_motionMatching = std::make_unique<anim::MotionMatching>();
        m_worldPartition = std::make_unique<world::WorldPartition>();
        m_pcgSystem = std::make_unique<world::PcgSystem>();
        m_replicationGraph = std::make_unique<network::ReplicationGraph>();
        m_audioEngine = std::make_unique<audio::SpatialAudioEngine>();

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

        loadUserPreferences();
        applyPreferences();

        m_networkThread = std::jthread([this]() {
            try {
                if (m_unifiedServer) m_unifiedServer->start();
                if (m_udpServer) m_udpServer->start();
                while (m_running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                if (m_udpServer) m_udpServer->stop();
                if (m_unifiedServer) m_unifiedServer->stop();
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
        if (m_preferences.autoSavePreferences) {
            saveUserPreferences();
        }
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
        if (m_unifiedServer) {
            nlohmann::json msg = {
                {"jsonrpc", "2.0"},
                {"method", "engine.onSimulationStateChanged"},
                {"params", {
                    {"paused", m_paused},
                    {"simulating", !m_paused}
                }}
            };
            m_unifiedServer->broadcastTextMessage(msg.dump());
        }
    }

    void Engine::playSimulation() {
        if (m_paused) {
            const bool startingNewSession = !m_simulationSessionActive;
            // Ensure orientation is synchronized from rotation before capturing snapshot
            if (m_registry) {
                auto view = m_registry->get().view<ecs::TransformComponent>();
                for (auto entity : view) {
                    auto& t = view.get<ecs::TransformComponent>(entity);
                    if (glm::dot(t.orientation, t.orientation) < 0.001f) {
                        t.orientation = glm::quat(glm::radians(t.rotation));
                    }
                }
            }

            // Capture the edit baseline only once per Play session. Pause/resume must
            // never replace the original snapshot used by STOP.
            if (startingNewSession) {
                m_preSimulationSnapshot = getSnapshotJson();
                m_hasPreSimulationSnapshot = true;
                m_simulationSessionActive = true;
                m_preSimulationCamPos = m_camPos;
                m_preSimulationCamYaw = m_camYaw;
                m_preSimulationCamPitch = m_camPitch;
                m_preSimulationCamFov = m_camFov;
                m_preSimulationCamMode = m_cameraMode;
            }

            // Sync editor transforms to Jolt bodies and clear residual velocities
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
            if (m_cameraMode == "editor" && m_registry) {
                auto charView = m_registry->get().view<ecs::CharacterMovementComponent>();
                if (!charView.empty()) {
                    auto firstEnt = *charView.begin();
                    bool isFpp = false;
                    if (m_registry->get().all_of<ecs::CameraBoomComponent>(firstEnt)) {
                        isFpp = m_registry->get().get<ecs::CameraBoomComponent>(firstEnt).isFirstPerson;
                    }
                    m_cameraMode = isFpp ? "firstPerson" : "thirdPerson";
                }
            }
            broadcastSimulationState();
            core::Logger::info("Simulation PLAY started: Edit snapshot secured, Jolt bodies activated.");
        }
    }

    void Engine::stopSimulation() {
        m_paused = true;
        m_physicsAccumulator = 0.0f;
        m_cameraMode = "editor";

        const bool hadActiveSession = m_simulationSessionActive;
        m_simulationSessionActive = false;

        // Restore pre-simulation snapshot
        if (hadActiveSession && m_hasPreSimulationSnapshot && !m_preSimulationSnapshot.empty()) {
            applySnapshotJson(m_preSimulationSnapshot.dump());
        }

        // Restore editor camera pose from before Play session
        if (hadActiveSession) {
            m_camPos = m_preSimulationCamPos;
            m_camYaw = m_preSimulationCamYaw;
            m_camPitch = m_preSimulationCamPitch;
            m_camFov = m_preSimulationCamFov;
            m_cameraMode = m_preSimulationCamMode.empty() ? "editor" : m_preSimulationCamMode;
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
                        JPH::Quat joltRot(t.orientation.x, t.orientation.y, t.orientation.z, t.orientation.w);
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

        // Restore all mesh components visibility in editor mode
        if (m_registry) {
            auto meshView = m_registry->get().view<ecs::MeshComponent>();
            for (auto entity : meshView) {
                meshView.get<ecs::MeshComponent>(entity).visible = true;
            }
        }

        m_score.store(0);
        m_health.store(100);
        m_gameStatus = "PLAYING";
        broadcastSimulationState();
        core::Logger::info("Simulation STOPPED: Scene restored to pre-simulation edit pose (position & rotation).");
    }

    void Engine::restartSimulation() {
        if (m_hasPreSimulationSnapshot && !m_preSimulationSnapshot.empty()) {
            applySnapshotJson(m_preSimulationSnapshot.dump());
        }
        m_physicsAccumulator = 0.0f;
        m_score.store(0);
        m_health.store(100);
        m_gameStatus = "PLAYING";

        if (!m_currentScriptCode.empty()) {
            loadScript(m_currentScriptCode);
        }

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

        if (m_registry) {
            auto meshView = m_registry->get().view<ecs::MeshComponent>();
            for (auto entity : meshView) {
                meshView.get<ecs::MeshComponent>(entity).visible = true;
            }
        }

        m_paused = false;
        if (m_cameraMode == "editor" && m_registry) {
            auto charView = m_registry->get().view<ecs::CharacterMovementComponent>();
            if (!charView.empty()) {
                auto firstEnt = *charView.begin();
                bool isFpp = false;
                if (m_registry->get().all_of<ecs::CameraBoomComponent>(firstEnt)) {
                    isFpp = m_registry->get().get<ecs::CameraBoomComponent>(firstEnt).isFirstPerson;
                }
                m_cameraMode = isFpp ? "firstPerson" : "thirdPerson";
            }
        }
        invalidateRtHistory();
        broadcastSimulationState();
        core::Logger::info("Simulation RESTARTED to pre-simulation state");
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

    void Engine::syncEntityTransformPhysics(entt::entity entity) {
        if (m_physicsContext && m_registry && m_registry->get().valid(entity) &&
            m_registry->get().all_of<ecs::TransformComponent, ecs::RigidBodyComponent>(entity)) {
            auto& t = m_registry->get().get<ecs::TransformComponent>(entity);
            auto& rb = m_registry->get().get<ecs::RigidBodyComponent>(entity);
            if (rb.bodyId != 0xFFFFFFFF) {
                JPH::BodyID bId(rb.bodyId);
                auto& bi = m_physicsContext->getBodyInterface();
                JPH::RVec3 jPos(t.position.x, t.position.y, t.position.z);
                JPH::Quat jRot = JPH::Quat::sEulerAngles(JPH::Vec3(
                    glm::radians(t.rotation.x),
                    glm::radians(t.rotation.y),
                    glm::radians(t.rotation.z)
                ));
                bi.SetPositionAndRotation(bId, jPos, jRot, JPH::EActivation::Activate);

                // Update Jolt collision shape to match current transform scale
                rb.lastScale = t.scale;
                JPH::ShapeRefC shape = physics::PhysicsSystem::createShape(rb, t.scale);
                if (shape) {
                    bi.SetShape(bId, shape, true, JPH::EActivation::Activate);
                }
            }
        }
    }

    void Engine::update(float dt) {
        LUCID_PROFILE_SCOPE("FrameUpdate");
        float clampedDt = std::min(dt, 0.1f);
        m_physicsAccumulator += clampedDt;

        {
            LUCID_PROFILE_SCOPE("Physics");
            if (m_physicsSystem && m_registry) {
                while (m_physicsAccumulator >= FIXED_TIMESTEP) {
                    m_physicsSystem->update(m_registry.get(), FIXED_TIMESTEP);
                    m_physicsAccumulator -= FIXED_TIMESTEP;
                }
            }
        }

        auto contacts = m_physicsContext ? m_physicsContext->getAndClearContacts() : std::vector<physics::ContactPoint>{};
        m_lastContacts = contacts;

        {
            LUCID_PROFILE_SCOPE("Chemistry");
            if (m_chemistrySystem && m_registry) {
                m_chemistrySystem->update(m_registry.get(), dt, contacts);
            }
        }

        {
            LUCID_PROFILE_SCOPE("Destruction");
            if (m_destructionSystem && m_registry && m_physicsContext) {
                m_destructionSystem->update(m_registry.get(), m_physicsContext.get(), this, dt, contacts);
            }
        }

        {
            LUCID_PROFILE_SCOPE("NavigationAI");
            if (m_navigationSystem && m_registry) {
                m_navigationSystem->update(m_registry.get(), dt);
            }
        }

        {
            LUCID_PROFILE_SCOPE("MotionIK");
            if (m_motionMatching && m_registry) {
                m_motionMatching->update(*m_registry, m_physicsContext.get(), dt);
            }
        }

        {
            LUCID_PROFILE_SCOPE("WorldStreaming");
            if (m_worldPartition && m_registry) {
                m_worldPartition->update(m_registry.get(), m_physicsContext.get(), m_camPos, dt);
                m_worldPartition->checkAndRebaseOrigin(m_registry.get(), m_physicsContext.get(), m_camPos, 2000.0f);
            }
        }

        {
            LUCID_PROFILE_SCOPE("Scripts");
            if (m_scriptContext && m_registry) {
                m_scriptContext->tickDirect(m_registry.get(), m_physicsContext.get(), dt, m_inputState, contacts);
            }
        }

        {
            LUCID_PROFILE_SCOPE("CharacterMovement");
            if (!m_paused && m_registry) {
                auto charView = m_registry->get().view<ecs::TransformComponent, ecs::CharacterMovementComponent>();
                for (auto entity : charView) {
                    auto& t = charView.get<ecs::TransformComponent>(entity);
                    const auto& mc = charView.get<ecs::CharacterMovementComponent>(entity);

                    float inputFwd = 0.0f;
                    float inputRight = 0.0f;
                    bool jumpRequested = false;
                    bool isSprinting = false;

                    if (m_inputState.contains("keys") && m_inputState["keys"].is_object()) {
                        const auto& keys = m_inputState["keys"];
                        if (keys.value("KeyW", false) || keys.value("ArrowUp", false)) inputFwd += 1.0f;
                        if (keys.value("KeyS", false) || keys.value("ArrowDown", false)) inputFwd -= 1.0f;
                        if (keys.value("KeyD", false) || keys.value("ArrowRight", false)) inputRight += 1.0f;
                        if (keys.value("KeyA", false) || keys.value("ArrowLeft", false)) inputRight -= 1.0f;
                        if (keys.value("Space", false)) jumpRequested = true;
                        if (keys.value("ShiftLeft", false) || keys.value("ShiftRight", false)) isSprinting = true;
                    }

                    // Compute wish direction relative to camera yaw on horizontal plane
                    float radYaw = glm::radians(m_camYaw);
                    glm::vec3 camFwdXZ = glm::normalize(glm::vec3(cos(radYaw), 0.0f, sin(radYaw)));
                    glm::vec3 camRightXZ = glm::normalize(glm::vec3(-sin(radYaw), 0.0f, cos(radYaw)));
                    glm::vec3 wishDir = camFwdXZ * inputFwd + camRightXZ * inputRight;
                    if (glm::length(wishDir) > 0.001f) wishDir = glm::normalize(wishDir);

                    float moveSpeed = mc.maxWalkSpeed * (isSprinting ? 1.75f : 1.0f);

                    if (m_physicsContext && m_registry->get().all_of<ecs::RigidBodyComponent>(entity)) {
                        auto& rb = m_registry->get().get<ecs::RigidBodyComponent>(entity);
                        if (rb.bodyId != 0xFFFFFFFF) {
                            JPH::BodyID bId(rb.bodyId);
                            JPH::Vec3 curVel = m_physicsContext->getBodyInterface().GetLinearVelocity(bId);

                            const bool isGrounded = std::abs(curVel.GetY()) < 0.25f;
                            const float accelRate = isGrounded ? 16.0f : (16.0f * mc.airControl);

                            float targetVelX = wishDir.x * moveSpeed;
                            float targetVelZ = wishDir.z * moveSpeed;
                            float newVelX = glm::mix(curVel.GetX(), targetVelX, glm::clamp(dt * accelRate, 0.0f, 1.0f));
                            float newVelZ = glm::mix(curVel.GetZ(), targetVelZ, glm::clamp(dt * accelRate, 0.0f, 1.0f));
                            float newVelY = curVel.GetY();

                            if (jumpRequested && isGrounded) {
                                newVelY = mc.jumpZVelocity;
                            }

                            m_physicsContext->getBodyInterface().SetLinearVelocity(bId, JPH::Vec3(newVelX, newVelY, newVelZ));
                            m_physicsContext->getBodyInterface().ActivateBody(bId);

                            bool isFpp = (m_cameraMode == "firstPerson");
                            if (!isFpp && m_registry->get().all_of<ecs::CameraBoomComponent>(entity)) {
                                isFpp = m_registry->get().get<ecs::CameraBoomComponent>(entity).isFirstPerson;
                            }

                            if (isFpp) {
                                // In First-Person, character body faces the camera look yaw
                                t.rotation.y = m_camYaw;
                                t.orientation = glm::quat(glm::radians(t.rotation));
                                JPH::Quat jRot = JPH::Quat::sEulerAngles(JPH::Vec3(
                                    0.0f,
                                    glm::radians(m_camYaw),
                                    0.0f
                                ));
                                m_physicsContext->getBodyInterface().SetRotation(bId, jRot, JPH::EActivation::DontActivate);
                            } else if (glm::length(wishDir) > 0.1f) {
                                // Smoothly turn character model to face movement direction
                                float targetYaw = glm::degrees(std::atan2(-wishDir.x, -wishDir.z));
                                float diff = std::remainder(targetYaw - t.rotation.y, 360.0f);
                                t.rotation.y += diff * glm::clamp(dt * 12.0f, 0.0f, 1.0f);
                                t.orientation = glm::quat(glm::radians(t.rotation));
                                JPH::Quat jRot = JPH::Quat::sEulerAngles(JPH::Vec3(
                                    glm::radians(t.rotation.x),
                                    glm::radians(t.rotation.y),
                                    glm::radians(t.rotation.z)
                                ));
                                m_physicsContext->getBodyInterface().SetRotation(bId, jRot, JPH::EActivation::DontActivate);
                            }
                        }
                    } else {
                        // Kinematic translation if without rigid body
                        t.position += wishDir * moveSpeed * dt;
                        bool isFpp = (m_cameraMode == "firstPerson");
                        if (!isFpp && m_registry->get().all_of<ecs::CameraBoomComponent>(entity)) {
                            isFpp = m_registry->get().get<ecs::CameraBoomComponent>(entity).isFirstPerson;
                        }

                        if (isFpp) {
                            t.rotation.y = m_camYaw;
                            t.orientation = glm::quat(glm::radians(t.rotation));
                        } else if (glm::length(wishDir) > 0.1f) {
                            float targetYaw = glm::degrees(std::atan2(-wishDir.x, -wishDir.z));
                            float diff = std::remainder(targetYaw - t.rotation.y, 360.0f);
                            t.rotation.y += diff * glm::clamp(dt * 12.0f, 0.0f, 1.0f);
                            t.orientation = glm::quat(glm::radians(t.rotation));
                        }
                    }
                }
            }
        }

        // Autonomous Camera Director (Spring Arm / Follow Cam for non-editor modes)
        if (m_cameraMode != "editor" && m_registry) {
            glm::vec3 playerPos(0.0f);
            bool foundPlayer = false;
            float armLength = 8.0f;
            float socketY = 2.5f;
            bool cameraLag = true;
            float lagSpeed = 10.0f;
            bool isFirstPerson = (m_cameraMode == "firstPerson");
            entt::entity playerEntity = entt::null;

            // Prioritize CharacterMovementComponent or Player tag
            auto charView = m_registry->get().view<ecs::TransformComponent, ecs::CharacterMovementComponent>();
            for (auto entity : charView) {
                playerEntity = entity;
                playerPos = charView.get<ecs::TransformComponent>(entity).position;
                foundPlayer = true;
                if (m_registry->get().all_of<ecs::CameraBoomComponent>(entity)) {
                    const auto& boom = m_registry->get().get<ecs::CameraBoomComponent>(entity);
                    armLength = boom.targetArmLength;
                    socketY = boom.socketOffsetY;
                    cameraLag = boom.cameraLag;
                    lagSpeed = boom.lagSpeed;
                    if (boom.isFirstPerson) isFirstPerson = true;
                }
                break;
            }

            if (!foundPlayer) {
                auto tagView = m_registry->get().view<ecs::TagComponent, ecs::TransformComponent>();
                for (auto entity : tagView) {
                    const auto& tag = tagView.get<ecs::TagComponent>(entity);
                    if (tag.name == "Player" || tag.name == "Paddle" || tag.name == "PlayerShip" || tag.name == "PlayerCar") {
                        playerEntity = entity;
                        playerPos = tagView.get<ecs::TransformComponent>(entity).position;
                        foundPlayer = true;
                        if (m_registry->get().all_of<ecs::CameraBoomComponent>(entity)) {
                            const auto& boom = m_registry->get().get<ecs::CameraBoomComponent>(entity);
                            armLength = boom.targetArmLength;
                            socketY = boom.socketOffsetY;
                            cameraLag = boom.cameraLag;
                            lagSpeed = boom.lagSpeed;
                            if (boom.isFirstPerson) isFirstPerson = true;
                        }
                        break;
                    }
                }
            }

            if (foundPlayer) {
                // If FPP, hide the player's mesh so it doesn't obstruct camera view inside the capsule
                if (playerEntity != entt::null && m_registry->get().valid(playerEntity) && m_registry->get().all_of<ecs::MeshComponent>(playerEntity)) {
                    auto& mc = m_registry->get().get<ecs::MeshComponent>(playerEntity);
                    mc.visible = !isFirstPerson;
                }

                glm::vec3 targetCamPos = m_camPos;
                float targetYaw = m_camYaw;
                float targetPitch = m_camPitch;

                if (isFirstPerson || m_cameraMode == "firstPerson") {
                    float radYaw = glm::radians(m_camYaw);
                    float radPitch = glm::radians(m_camPitch);
                    glm::vec3 camFwd(
                        cos(radYaw) * cos(radPitch),
                        sin(radPitch),
                        sin(radYaw) * cos(radPitch)
                    );
                    targetCamPos = playerPos + glm::vec3(0.0f, socketY > 0.5f ? socketY : 1.65f, 0.0f) + camFwd * 0.15f;
                    m_camPos = targetCamPos; // Instant 1:1 attach, 0 lag in FPP
                } else if (m_cameraMode == "thirdPerson") {
                    float radYaw = glm::radians(m_camYaw);
                    float radPitch = glm::radians(m_camPitch);
                    glm::vec3 camDir(
                        cos(radYaw) * cos(radPitch),
                        sin(radPitch),
                        sin(radYaw) * cos(radPitch)
                    );
                    targetCamPos = (playerPos + glm::vec3(0.0f, socketY, 0.0f)) - glm::normalize(camDir) * armLength;
                    if (cameraLag) {
                        if (glm::distance(m_camPos, targetCamPos) > 30.0f) {
                            m_camPos = targetCamPos;
                        } else {
                            float lag = glm::clamp(dt * lagSpeed, 0.0f, 1.0f);
                            m_camPos = glm::mix(m_camPos, targetCamPos, lag);
                        }
                    } else {
                        m_camPos = targetCamPos;
                    }
                } else if (m_cameraMode == "topDown") {
                    targetCamPos = playerPos + glm::vec3(0.0f, 22.0f, 6.0f);
                    targetYaw = -90.0f;
                    targetPitch = -70.0f;
                    float lag = glm::clamp(dt * 10.0f, 0.0f, 1.0f);
                    m_camPos = glm::mix(m_camPos, targetCamPos, lag);
                    m_camYaw = glm::mix(m_camYaw, targetYaw, lag);
                    m_camPitch = glm::mix(m_camPitch, targetPitch, lag);
                } else if (m_cameraMode == "sideScroller") {
                    targetCamPos = playerPos + glm::vec3(0.0f, 2.0f, 18.0f);
                    targetYaw = -90.0f;
                    targetPitch = 0.0f;
                    float lag = glm::clamp(dt * 10.0f, 0.0f, 1.0f);
                    m_camPos = glm::mix(m_camPos, targetCamPos, lag);
                    m_camYaw = glm::mix(m_camYaw, targetYaw, lag);
                    m_camPitch = glm::mix(m_camPitch, targetPitch, lag);
                }

                if (m_cameraMode != "firstPerson" && m_cameraMode != "thirdPerson") {
                    float lag = glm::clamp(dt * 10.0f, 0.0f, 1.0f);
                    m_camPos = glm::mix(m_camPos, targetCamPos, lag);
                    m_camYaw = glm::mix(m_camYaw, targetYaw, lag);
                    m_camPitch = glm::mix(m_camPitch, targetPitch, lag);
                }
            }
        }

        {
            LUCID_PROFILE_SCOPE("SpatialAudio");
            if (m_audioEngine && m_registry) {
                glm::vec3 front;
                front.x = cos(glm::radians(m_camYaw)) * cos(glm::radians(m_camPitch));
                front.y = sin(glm::radians(m_camPitch));
                front.z = sin(glm::radians(m_camYaw)) * cos(glm::radians(m_camPitch));
                m_audioEngine->update(m_registry.get(), m_camPos, glm::normalize(front), glm::vec3(0.0f), dt);
            }
        }

        {
            // Evaluate parent-relative transforms before publishing the frame snapshot.
            if (m_registry) m_registry->updateWorldTransforms();

            LUCID_PROFILE_SCOPE("Netcode");
            if (m_replicationGraph && m_registry) {
                m_replicationGraph->recordSnapshot(++m_currentTick, m_registry.get());
            }
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

            std::unordered_set<uint32_t> validRestoredIds;

            for (const auto& entJson : ents) {
                if (!entJson.is_object()) continue;
                if (!entJson.contains("id")) {
                    uint32_t newId = spawnEntityFromJson(entJson);
                    validRestoredIds.insert(newId);
                    continue;
                }
                auto entity = static_cast<entt::entity>(entJson["id"].get<uint32_t>());
                if (!m_registry->get().valid(entity)) {
                    uint32_t newId = spawnEntityFromJson(entJson);
                    validRestoredIds.insert(newId);
                    continue;
                }
                validRestoredIds.insert(static_cast<uint32_t>(entity));

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

                    // Restore exact quaternion orientation
                    if (transJson.contains("qw") && transJson.contains("qx") && transJson.contains("qy") && transJson.contains("qz")) {
                        t.orientation = glm::quat(
                            transJson["qw"].get<float>(),
                            transJson["qx"].get<float>(),
                            transJson["qy"].get<float>(),
                            transJson["qz"].get<float>()
                        );
                        rotChanged = true;
                    } else if (rotChanged) {
                        t.orientation = glm::quat(glm::radians(t.rotation));
                    }

                    if ((posChanged || rotChanged) && m_physicsContext && m_registry->get().all_of<ecs::RigidBodyComponent>(entity)) {
                        auto& rb = m_registry->get().get<ecs::RigidBodyComponent>(entity);
                        if (rb.bodyId != 0xFFFFFFFF) {
                            JPH::BodyID bId(rb.bodyId);
                            JPH::RVec3 jPos(t.position.x, t.position.y, t.position.z);
                            JPH::Quat jRot(t.orientation.x, t.orientation.y, t.orientation.z, t.orientation.w);
                            m_physicsContext->getBodyInterface().SetPositionAndRotation(bId, jPos, jRot, JPH::EActivation::DontActivate);
                            m_physicsContext->getBodyInterface().SetLinearVelocity(bId, JPH::Vec3::sZero());
                            m_physicsContext->getBodyInterface().SetAngularVelocity(bId, JPH::Vec3::sZero());
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

                if (entJson.contains("mesh")) {
                    auto& m = m_registry->get().get_or_emplace<ecs::MeshComponent>(entity);
                    const auto& mesh = entJson["mesh"];
                    m.meshId = mesh.value("meshId", 2);
                    m.materialId = mesh.value("materialId", 0);
                }

                if (entJson.contains("material")) {
                    auto& mat = m_registry->get().get_or_emplace<ecs::MaterialComponent>(entity);
                    const auto& mJson = entJson["material"];
                    if (mJson.contains("color")) mat.baseColor = parseColorJson(mJson["color"], mat.baseColor);
                    if (mJson.contains("metallic")) mat.metallic = mJson["metallic"].get<float>();
                    if (mJson.contains("roughness")) mat.roughness = mJson["roughness"].get<float>();
                    if (mJson.contains("emissive")) mat.emissive = glm::vec3(parseColorJson(mJson["emissive"], glm::vec4(0.0f)));
                    if (mJson.contains("emissiveIntensity")) mat.emissiveIntensity = mJson["emissiveIntensity"].get<float>();
                    if (mJson.contains("materialId")) mat.materialId = mJson["materialId"].get<uint32_t>();
                    if (mJson.contains("albedoTextureId")) mat.albedoTextureId = mJson["albedoTextureId"].get<uint32_t>();
                    if (mJson.contains("normalTextureId")) mat.normalTextureId = mJson["normalTextureId"].get<uint32_t>();
                    if (mJson.contains("ormTextureId")) mat.ormTextureId = mJson["ormTextureId"].get<uint32_t>();
                    if (mJson.contains("albedoPath")) mat.albedoPath = mJson["albedoPath"].get<std::string>();
                    if (mJson.contains("normalPath")) mat.normalPath = mJson["normalPath"].get<std::string>();
                    if (mJson.contains("ormPath")) mat.ormPath = mJson["ormPath"].get<std::string>();
                } else if (entJson.contains("color")) {
                    auto& mat = m_registry->get().get_or_emplace<ecs::MaterialComponent>(entity);
                    mat.baseColor = parseColorJson(entJson["color"], mat.baseColor);
                }

                if (entJson.contains("light")) {
                    auto& lc = m_registry->get().get_or_emplace<ecs::LightComponent>(entity);
                    const auto& lJson = entJson["light"];
                    if (lJson.contains("type")) lc.type = lJson["type"].get<std::string>();
                    if (lJson.contains("color")) lc.color = glm::vec3(parseColorJson(lJson["color"], glm::vec4(lc.color, 1.0f)));
                    if (lJson.contains("intensity")) lc.intensity = lJson["intensity"].get<float>();
                    if (lJson.contains("range")) lc.range = lJson["range"].get<float>();
                    if (lJson.contains("innerConeAngle")) lc.innerConeAngle = lJson["innerConeAngle"].get<float>();
                    if (lJson.contains("outerConeAngle")) lc.outerConeAngle = lJson["outerConeAngle"].get<float>();
                    if (lJson.contains("areaSizeX")) lc.areaSizeX = lJson["areaSizeX"].get<float>();
                    if (lJson.contains("areaSizeY")) lc.areaSizeY = lJson["areaSizeY"].get<float>();
                }

                if (entJson.contains("audio")) {
                    auto& ac = m_registry->get().get_or_emplace<ecs::AudioComponent>(entity);
                    const auto& aJson = entJson["audio"];
                    if (aJson.contains("soundAssetId")) ac.soundAssetId = aJson["soundAssetId"].get<std::string>();
                    if (aJson.contains("volume")) ac.volume = aJson["volume"].get<float>();
                    if (aJson.contains("pitch")) ac.pitch = aJson["pitch"].get<float>();
                    if (aJson.contains("is3D")) ac.is3D = aJson["is3D"].get<bool>();
                    if (aJson.contains("loop")) ac.loop = aJson["loop"].get<bool>();
                    if (aJson.contains("autoPlay")) ac.autoPlay = aJson["autoPlay"].get<bool>();
                }

                if (entJson.contains("movement")) {
                    auto& mc = m_registry->get().get_or_emplace<ecs::CharacterMovementComponent>(entity);
                    const auto& mvJson = entJson["movement"];
                    if (mvJson.contains("maxWalkSpeed")) mc.maxWalkSpeed = mvJson["maxWalkSpeed"].get<float>();
                    if (mvJson.contains("jumpZVelocity")) mc.jumpZVelocity = mvJson["jumpZVelocity"].get<float>();
                    if (mvJson.contains("gravityScale")) mc.gravityScale = mvJson["gravityScale"].get<float>();
                    if (mvJson.contains("airControl")) mc.airControl = mvJson["airControl"].get<float>();
                }

                if (entJson.contains("cameraBoom")) {
                    auto& bc = m_registry->get().get_or_emplace<ecs::CameraBoomComponent>(entity);
                    const auto& bJson = entJson["cameraBoom"];
                    if (bJson.contains("targetArmLength")) bc.targetArmLength = bJson["targetArmLength"].get<float>();
                    if (bJson.contains("socketOffsetY")) bc.socketOffsetY = bJson["socketOffsetY"].get<float>();
                    if (bJson.contains("cameraLag")) bc.cameraLag = bJson["cameraLag"].get<bool>();
                    if (bJson.contains("lagSpeed")) bc.lagSpeed = bJson["lagSpeed"].get<float>();
                    if (bJson.contains("isFirstPerson")) bc.isFirstPerson = bJson["isFirstPerson"].get<bool>();
                }

                if (entJson.contains("particles") || entJson.contains("particleEmitter")) {
                    auto& pc = m_registry->get().get_or_emplace<ecs::ParticleEmitterComponent>(entity);
                    const auto& pJson = entJson.contains("particles") ? entJson["particles"] : entJson["particleEmitter"];
                    if (pJson.contains("type")) pc.type = pJson["type"].get<std::string>();
                    if (pJson.contains("color")) pc.color = glm::vec3(parseColorJson(pJson["color"], glm::vec4(pc.color, 1.0f)));
                    if (pJson.contains("count")) pc.count = pJson["count"].get<uint32_t>();
                    if (pJson.contains("speed")) pc.speed = pJson["speed"].get<float>();
                    if (pJson.contains("size")) pc.size = pJson["size"].get<float>();
                }
            }

            // Clean up any dynamic entities spawned during simulation that weren't in the snapshot
            auto allEntsView = m_registry->get().view<ecs::TransformComponent>();
            std::vector<entt::entity> extraEntities;
            for (auto entity : allEntsView) {
                uint32_t id = static_cast<uint32_t>(entity);
                if (validRestoredIds.find(id) == validRestoredIds.end()) {
                    extraEntities.push_back(entity);
                }
            }
            for (auto e : extraEntities) {
                destroyEntity(static_cast<uint32_t>(e));
            }
        } catch (...) {}
    }

    void Engine::requestLoadProject(const std::string& path) {
        m_pendingProjectLoadPath = path;
        m_hasPendingProjectLoad = true;
    }

    void Engine::run() {
        core::Logger::info("Engine started. Headless: " + std::string(m_headless ? "true" : "false"));
        auto lastTime = std::chrono::high_resolution_clock::now();
        while (m_running) {
            Profiler::get().beginFrame();
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
                    bool imguiWantsTextInput = (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantTextInput);
                    bool imguiWantsMouse = (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse);

                    // Native Viewport Fly Camera (PPM + WASD / QE / Shift)
                    static bool s_rmbDown = false;
                    static double s_lastX = 0.0, s_lastY = 0.0;
                    if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS && !imguiWantsMouse) {
                        double curX, curY;
                        glfwGetCursorPos(win, &curX, &curY);
                        if (!s_rmbDown) {
                            s_rmbDown = true;
                            s_lastX = curX;
                            s_lastY = curY;
                        } else {
                            float dx = static_cast<float>(curX - s_lastX);
                            float dy = static_cast<float>(curY - s_lastY);
                            s_lastX = curX;
                            s_lastY = curY;

                            float sens = m_preferences.camMouseSensitivity * 0.75f;
                            float yDelta = dy * sens;
                            if (m_preferences.invertY) yDelta = -yDelta;
                            m_camYaw += dx * sens;
                            m_camPitch = glm::clamp(m_camPitch - yDelta, -89.0f, 89.0f);

                        }

                        // Fly Movement in camera direction
                        float radYaw = glm::radians(m_camYaw);
                        float radPitch = glm::radians(m_camPitch);
                        glm::vec3 fwd(
                            cos(radYaw) * cos(radPitch),
                            sin(radPitch),
                            sin(radYaw) * cos(radPitch)
                        );
                        fwd = glm::normalize(fwd);
                        glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
                        glm::vec3 up(0.0f, 1.0f, 0.0f);

                        float baseSpeed = m_preferences.camFlySpeed * (m_preferences.cameraSpeed / 4.0f);
                        float speed = (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? (baseSpeed * 3.0f) : baseSpeed;
                        glm::vec3 moveDir(0.0f);
                        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) moveDir += fwd;
                        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) moveDir -= fwd;
                        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) moveDir += right;
                        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) moveDir -= right;
                        if (glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS) moveDir += up;
                        if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS) moveDir -= up;

                        if (glm::length(moveDir) > 0.001f) {
                            m_camPos += glm::normalize(moveDir) * speed * dt;
                        }
                    } else {
                        s_rmbDown = false;
                    }

                    if (!m_paused) {
                        // Native Gameplay Controls in GLFW Desktop Window
                        nlohmann::json keys = nlohmann::json::object();
                        if (!imguiWantsTextInput) {
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
                            if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
                                keys["ShiftLeft"] = true;
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
                // Dual-Transport: Process MPSC binary command queue
                if (m_mpscCommandQueue) {
                    std::vector<network::RpcCommandMessage> mpscCmds;
                    m_mpscCommandQueue->dequeueAll(mpscCmds);
                    for (const auto& cmd : mpscCmds) {
                        if (cmd.command_id == 1) {
                            bool paused = (cmd.payload_len > 0 && cmd.payload[0] != 0);
                            if (paused) stopSimulation(); else playSimulation();
                        } else if (cmd.command_id == 2) {
                            std::string preset = (cmd.payload_len > 0) ? std::string(reinterpret_cast<const char*>(cmd.payload), strnlen(reinterpret_cast<const char*>(cmd.payload), cmd.payload_len)) : "empty";
                            resetScene(preset);
                        }
                    }
                }

                if (m_hasPendingProjectLoad) {
                    m_hasPendingProjectLoad = false;
                    const std::string path = m_pendingProjectLoadPath;
                    m_pendingProjectLoadPath.clear();
                    loadCurrentProject(path);
                }

                auto current_now = std::chrono::high_resolution_clock::now();
                float dt_real = std::chrono::duration<float>(current_now - lastTime).count();
                lastTime = current_now;
                if (!m_paused) {
                    try { update(dt_real); } catch (const std::exception& e) { core::Logger::error(std::string("Update exception: ") + e.what()); }
                }
                if (!m_headless) {
                    try { render(); } catch(const std::exception& e) { core::Logger::error(std::string("Render exception: ") + e.what()); } catch(...) { core::Logger::error("Unknown render exception!"); }
                }

                // Dual-Transport: Publish authoritative TelemetrySnapshot to 32-slot Seqlock Ring Buffer
                if (m_sharedStateRing) {
                    float cpuMs = dt_real * 1000.0f;
                    float audioDb = -6.0f;
                    uint32_t flags = network::STATE_FLAG_NONE;
                    if (!m_paused) flags |= network::STATE_FLAG_SIMULATING;
                    else flags |= network::STATE_FLAG_PAUSED;
                    if (m_unifiedServer && m_unifiedServer->isRunning()) flags |= network::STATE_FLAG_NET_ACTIVE;

                    uint32_t entCount = m_registry ? static_cast<uint32_t>(m_registry->get().view<ecs::TransformComponent>().size()) : 0;
                    m_sharedStateRing->publish(cpuMs, audioDb, flags, entCount);
                }

                if (m_headless || m_paused) {
                    if (m_commandQueue.empty() && (!m_mpscCommandQueue || m_mpscCommandQueue->empty())) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                } else if (m_targetFps > 0) {
                    float targetFrameTime = 1.0f / static_cast<float>(m_targetFps);
                    auto frameEndTime = std::chrono::high_resolution_clock::now();
                    float elapsed = std::chrono::duration<float>(frameEndTime - current_now).count();
                    float sleepRemaining = targetFrameTime - elapsed;
                    if (sleepRemaining > 0.0015f) {
                        std::this_thread::sleep_for(std::chrono::duration<float>(sleepRemaining - 0.001f));
                    }
                    while (std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - current_now).count() < targetFrameTime) {
                        std::this_thread::yield();
                    }
                }
            } catch (const std::exception& e) {
                core::Logger::error(std::string("Loop exception: ") + e.what());
            } catch (...) {
                core::Logger::error("Unknown loop exception in Engine::run");
            }
            Profiler::get().endFrame();
        }
    }

    void Engine::render() {
        LUCID_PROFILE_SCOPE("Render");
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
        if (!m_scriptContext) {
            core::Logger::error("Cannot load script: QuickJS context is unavailable.");
            return false;
        }

        // Commit the editor's current script only after QuickJS accepts the new source.
        // A syntax/runtime evaluation failure must not replace the last known-good script.
        if (!m_scriptContext->evaluate(code)) {
            core::Logger::error("QuickJS script evaluation failed; keeping the previous script source.");
            return false;
        }

        m_currentScriptCode = code;
        return true;
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
            if (trans.contains("qw") && trans.contains("qx") && trans.contains("qy") && trans.contains("qz")) {
                t.orientation = glm::quat(trans["qw"].get<float>(), trans["qx"].get<float>(),
                                          trans["qy"].get<float>(), trans["qz"].get<float>());
            } else {
                t.orientation = glm::quat(glm::radians(t.rotation));
            }
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
            if (matJson.contains("color")) {
                mat.baseColor = parseColorJson(matJson["color"], glm::vec4(1.0f));
            } else if (matJson.contains("baseColor")) {
                mat.baseColor = parseColorJson(matJson["baseColor"], glm::vec4(1.0f));
            }
            mat.metallic = matJson.value("metallic", 0.1f);
            mat.roughness = matJson.value("roughness", 0.5f);
            if (matJson.contains("emissive")) {
                mat.emissive = glm::vec3(parseColorJson(matJson["emissive"], glm::vec4(0.0f)));
            }
            mat.emissiveIntensity = matJson.value("emissiveIntensity", 0.0f);
            mat.materialId = matJson.value("materialId", 0u);
            mat.albedoTextureId = matJson.value("albedoTextureId", 0u);
            mat.normalTextureId = matJson.value("normalTextureId", 0u);
            mat.ormTextureId = matJson.value("ormTextureId", 0u);
            mat.albedoPath = matJson.value("albedoPath", "");
            mat.normalPath = matJson.value("normalPath", "");
            mat.ormPath = matJson.value("ormPath", "");
            m_registry->get().emplace<ecs::MaterialComponent>(entity, mat);
        } else if (components.contains("color") || components.contains("baseColor")) {
            ecs::MaterialComponent mat;
            mat.baseColor = parseColorJson(components.contains("color") ? components["color"] : components["baseColor"], glm::vec4(1.0f));
            m_registry->get().emplace<ecs::MaterialComponent>(entity, mat);
        }

        if (components.contains("light")) {
            ecs::LightComponent light;
            const auto& lJson = components["light"];
            light.type = lJson.value("type", "point");
            if (lJson.contains("color")) {
                light.color = glm::vec3(parseColorJson(lJson["color"], glm::vec4(1.0f)));
            }
            light.intensity = lJson.value("intensity", 1.0f);
            light.range = lJson.value("range", 20.0f);
            light.innerConeAngle = lJson.value("innerConeAngle", 25.0f);
            light.outerConeAngle = lJson.value("outerConeAngle", 40.0f);
            light.areaSizeX = lJson.value("areaSizeX", 2.5f);
            light.areaSizeY = lJson.value("areaSizeY", 2.0f);
            m_registry->get().emplace<ecs::LightComponent>(entity, light);
        }

        if (components.contains("audio")) {
            ecs::AudioComponent audio;
            const auto& aJson = components["audio"];
            audio.soundAssetId = aJson.value("soundAssetId", "");
            audio.volume = aJson.value("volume", 1.0f);
            audio.pitch = aJson.value("pitch", 1.0f);
            audio.is3D = aJson.value("is3D", true);
            audio.loop = aJson.value("loop", false);
            audio.autoPlay = aJson.value("autoPlay", false);
            m_registry->get().emplace<ecs::AudioComponent>(entity, audio);
        }

        if (components.contains("movement")) {
            ecs::CharacterMovementComponent mv;
            const auto& mJson = components["movement"];
            mv.maxWalkSpeed = mJson.value("maxWalkSpeed", 8.0f);
            mv.jumpZVelocity = mJson.value("jumpZVelocity", 12.0f);
            mv.gravityScale = mJson.value("gravityScale", 1.0f);
            mv.airControl = mJson.value("airControl", 0.3f);
            m_registry->get().emplace<ecs::CharacterMovementComponent>(entity, mv);
        }

        if (components.contains("cameraBoom")) {
            ecs::CameraBoomComponent boom;
            const auto& bJson = components["cameraBoom"];
            boom.targetArmLength = bJson.value("targetArmLength", 10.0f);
            boom.socketOffsetY = bJson.value("socketOffsetY", 3.0f);
            boom.cameraLag = bJson.value("cameraLag", true);
            boom.lagSpeed = bJson.value("lagSpeed", 10.0f);
            m_registry->get().emplace<ecs::CameraBoomComponent>(entity, boom);
        }

        if (components.contains("particles") || components.contains("particleEmitter")) {
            ecs::ParticleEmitterComponent pe;
            const auto& pJson = components.contains("particles") ? components["particles"] : components["particleEmitter"];
            pe.type = pJson.value("type", "fire");
            if (pJson.contains("color")) {
                pe.color = glm::vec3(parseColorJson(pJson["color"], glm::vec4(1.0f, 0.4f, 0.0f, 1.0f)));
            }
            pe.count = pJson.value("count", 200);
            pe.speed = pJson.value("speed", 3.0f);
            pe.size = pJson.value("size", 0.4f);
            m_registry->get().emplace<ecs::ParticleEmitterComponent>(entity, pe);
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

        m_registry->destroyEntity(entity);
        return true;
    }

    void Engine::resetScene(const std::string& preset) {
        // Force simulation into paused edit mode and clear accumulator
        m_paused = true;
        m_physicsAccumulator = 0.0f;
        m_simulationSessionActive = false;
        m_hasPreSimulationSnapshot = false;
        m_preSimulationSnapshot = nlohmann::json::array();
        setSelectedEntity(entt::null);
        broadcastSimulationState();

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
        } else if (preset == "clean" || preset == "blank") {
            core::Logger::info("Scene reset to: Clean Slate");
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

            // Directional Light (Sun)
            auto sun = m_registry->createEntity();
            m_registry->get().emplace<ecs::TagComponent>(sun, ecs::TagComponent{"DirectionalLight (Sun)"});
            m_registry->get().emplace<ecs::TransformComponent>(sun, glm::vec3(0.0f, 18.0f, 0.0f), glm::vec3(-45.0f, -40.0f, 0.0f), glm::vec3(1.0f));
            ecs::LightComponent sunLight;
            sunLight.type = "directional";
            sunLight.color = glm::vec3(1.0f, 0.98f, 0.92f);
            sunLight.intensity = 3.5f;
            sunLight.range = 0.0f;
            m_registry->get().emplace<ecs::LightComponent>(sun, sunLight);

            core::Logger::info("Scene reset to: Empty Preset");
        } else {
            initTestScene();
        }

        m_currentPreset = preset;
        m_score.store(0);
        m_health.store(100);
        m_gameStatus = "PLAYING";
        if (preset != "clean" && preset != "blank") {
            loadScript(getDefaultScript(preset));
        }

        // Ensure orientations are normalized and synchronized for all newly spawned actors
        if (m_registry) {
            auto tView = m_registry->get().view<ecs::TransformComponent>();
            for (auto entity : tView) {
                auto& t = tView.get<ecs::TransformComponent>(entity);
                if (glm::dot(t.orientation, t.orientation) < 0.001f) {
                    t.orientation = glm::quat(glm::radians(t.rotation));
                }
            }
        }

        // Secure initial baseline edit snapshot for newly changed scene
        m_preSimulationSnapshot = getSnapshotJson();
        m_hasPreSimulationSnapshot = true;
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
        return R"(// --- UNIVERSAL LUCID GAMEPLAY & PHYSICS CONTROLLER ---
function update(entities, dt, input, contacts) {
  // 1. Player Character Controls (WASD + Space)
  var player = entities.find(e => e.tag === 'Player' || e.tag === 'Character' || e.tag === 'PlayerCar' || e.tag === 'Paddle');
  if (player && player.transform) {
    var speed = 12.0;
    if (!player.velocity) player.velocity = { vx: 0, vy: 0, vz: 0 };
    var moveX = 0, moveZ = 0;
    if (input && input.keys) {
      if (input.keys.KeyW || input.keys.ArrowUp) moveZ -= 1;
      if (input.keys.KeyS || input.keys.ArrowDown) moveZ += 1;
      if (input.keys.KeyA || input.keys.ArrowLeft) moveX -= 1;
      if (input.keys.KeyD || input.keys.ArrowRight) moveX += 1;
    }
    player.velocity.vx = moveX * speed;
    player.velocity.vz = moveZ * speed;
    if (input && input.keys && input.keys.Space && Math.abs(player.velocity.vy) < 0.2) {
      player.velocity.vy = 8.5;
      if (typeof engine !== 'undefined' && engine.playSfx) engine.playSfx('jump', 0.8);
    }
  }

  // 2. Interactive Contacts (Coins, Triggers, Hazards)
  if (contacts && contacts.length > 0 && typeof engine !== 'undefined') {
    for (var i = 0; i < contacts.length; i++) {
      var c = contacts[i];
      var ea = entities.find(e => e.id === c.a);
      var eb = entities.find(e => e.id === c.b);
      if (!ea || !eb) continue;
      var isPlayerA = (ea.tag === 'Player' || ea.tag === 'Character');
      var isPlayerB = (eb.tag === 'Player' || eb.tag === 'Character');
      if (isPlayerA && eb.tag && (eb.tag === 'Coin' || eb.tag.indexOf('Coin') !== -1 || eb.tag === 'TriggerZone')) {
        engine.destroyEntity(eb.id);
        engine.setScore(engine.getScore() + 50);
        engine.playSfx('coin', 1.0);
      } else if (isPlayerB && ea.tag && (ea.tag === 'Coin' || ea.tag.indexOf('Coin') !== -1 || ea.tag === 'TriggerZone')) {
        engine.destroyEntity(ea.id);
        engine.setScore(engine.getScore() + 50);
        engine.playSfx('coin', 1.0);
      }
    }
  }
}
)";
    }

    std::string Engine::captureFrameBase64() {
        if (true) { // Always render on frame capture
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
        if (steps <= 0 || dt <= 0.0f) return;
        // In GUI editor mode, prevent accidental stepping when no play session is active.
        if (!m_headless && !m_simulationSessionActive) return;
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
                    {"roughness", mat.roughness},
                    {"emissive", { mat.emissive.r, mat.emissive.g, mat.emissive.b }},
                    {"emissiveIntensity", mat.emissiveIntensity},
                    {"materialId", mat.materialId},
                    {"albedoTextureId", mat.albedoTextureId},
                    {"normalTextureId", mat.normalTextureId},
                    {"ormTextureId", mat.ormTextureId},
                    {"albedoPath", mat.albedoPath},
                    {"normalPath", mat.normalPath},
                    {"ormPath", mat.ormPath}
                };
            }
            if (m_registry->get().all_of<ecs::LightComponent>(entity)) {
                const auto& lc = m_registry->get().get<ecs::LightComponent>(entity);
                entJson["light"] = {
                    {"type", lc.type},
                    {"color", {lc.color.r, lc.color.g, lc.color.b}},
                    {"intensity", lc.intensity},
                    {"range", lc.range},
                    {"innerConeAngle", lc.innerConeAngle},
                    {"outerConeAngle", lc.outerConeAngle},
                    {"areaSizeX", lc.areaSizeX},
                    {"areaSizeY", lc.areaSizeY}
                };
            }
            if (m_registry->get().all_of<ecs::AudioComponent>(entity)) {
                const auto& ac = m_registry->get().get<ecs::AudioComponent>(entity);
                entJson["audio"] = {
                    {"soundAssetId", ac.soundAssetId},
                    {"volume", ac.volume},
                    {"pitch", ac.pitch},
                    {"is3D", ac.is3D},
                    {"loop", ac.loop},
                    {"autoPlay", ac.autoPlay}
                };
            }
            if (m_registry->get().all_of<ecs::CharacterMovementComponent>(entity)) {
                const auto& mc = m_registry->get().get<ecs::CharacterMovementComponent>(entity);
                entJson["movement"] = {
                    {"maxWalkSpeed", mc.maxWalkSpeed},
                    {"jumpZVelocity", mc.jumpZVelocity},
                    {"gravityScale", mc.gravityScale},
                    {"airControl", mc.airControl}
                };
            }
            if (m_registry->get().all_of<ecs::CameraBoomComponent>(entity)) {
                const auto& bc = m_registry->get().get<ecs::CameraBoomComponent>(entity);
                entJson["cameraBoom"] = {
                    {"targetArmLength", bc.targetArmLength},
                    {"socketOffsetY", bc.socketOffsetY},
                    {"cameraLag", bc.cameraLag},
                    {"lagSpeed", bc.lagSpeed},
                    {"isFirstPerson", bc.isFirstPerson}
                };
            }
            if (m_registry->get().all_of<ecs::ParticleEmitterComponent>(entity)) {
                const auto& pc = m_registry->get().get<ecs::ParticleEmitterComponent>(entity);
                nlohmann::json pObj = {
                    {"type", pc.type},
                    {"color", {pc.color.r, pc.color.g, pc.color.b}},
                    {"count", pc.count},
                    {"speed", pc.speed},
                    {"size", pc.size}
                };
                entJson["particles"] = pObj;
                entJson["particleEmitter"] = pObj;
            }
            snap.push_back(entJson);
        }
        return snap;
    }

    std::string Engine::getTypeDefinitions() {
        return ecs::TypeExporter::generateTypeScriptDefinitions();
    }

    void Engine::setEnvironmentSettings(const EnvironmentSettings& env) {
        m_envSettings = env;
        if (m_renderer) {
            m_renderer->setSkyboxEnabled(env.skyboxEnabled);
            m_renderer->setIblEnabled(env.iblEnabled);
        }
    }

    void Engine::setEnvironmentFromJson(const nlohmann::json& json) {
        if (json.contains("gravity")) {
            m_envSettings.gravity = json["gravity"].get<float>();
            if (m_physicsContext) {
                m_physicsContext->getSystem().SetGravity(JPH::Vec3(0.0f, m_envSettings.gravity, 0.0f));
            }
        }
        if (json.contains("ambientColor")) {
            m_envSettings.ambientColor = glm::vec3(parseColorJson(json["ambientColor"], glm::vec4(0.15f, 0.22f, 0.35f, 1.0f)));
        }
        if (json.contains("ambientIntensity")) {
            m_envSettings.ambientIntensity = json["ambientIntensity"].get<float>();
        }
        if (json.contains("sunColor")) {
            m_envSettings.sunColor = glm::vec3(parseColorJson(json["sunColor"], glm::vec4(1.0f, 0.97f, 0.92f, 1.0f)));
        }
        if (json.contains("sunIntensity")) {
            m_envSettings.sunIntensity = json["sunIntensity"].get<float>();
        }
        if (json.contains("sunDirection") && json["sunDirection"].is_array() && json["sunDirection"].size() >= 3) {
            m_envSettings.sunDirection = glm::vec3(json["sunDirection"][0], json["sunDirection"][1], json["sunDirection"][2]);
        }
        if (json.contains("sunEnabled")) {
            m_envSettings.sunEnabled = json["sunEnabled"].get<bool>();
        }
        if (json.contains("sunDirectionFromPosition")) {
            m_envSettings.sunDirectionFromPosition = json["sunDirectionFromPosition"].get<bool>();
        }
        if (json.contains("fogEnabled")) {
            m_envSettings.fogEnabled = json["fogEnabled"].get<bool>();
        } else if (json.contains("fog")) {
            m_envSettings.fogEnabled = json["fog"].get<bool>();
        }
        if (json.contains("fogColor")) {
            m_envSettings.fogColor = glm::vec3(parseColorJson(json["fogColor"], glm::vec4(0.06f, 0.07f, 0.09f, 1.0f)));
        }
        if (json.contains("fogNear")) {
            m_envSettings.fogNear = json["fogNear"].get<float>();
        }
        if (json.contains("fogFar")) {
            m_envSettings.fogFar = json["fogFar"].get<float>();
        }
        if (json.contains("hdriEnabled")) {
            bool hdri = json["hdriEnabled"].get<bool>();
            m_envSettings.skyboxEnabled = hdri;
            m_envSettings.iblEnabled = hdri;
            if (m_renderer) {
                m_renderer->setSkyboxEnabled(hdri);
                m_renderer->setIblEnabled(hdri);
            }
        } else if (json.contains("hdri")) {
            bool hdri = json["hdri"].get<bool>();
            m_envSettings.skyboxEnabled = hdri;
            m_envSettings.iblEnabled = hdri;
            if (m_renderer) {
                m_renderer->setSkyboxEnabled(hdri);
                m_renderer->setIblEnabled(hdri);
            }
        }
        if (json.contains("skyboxEnabled")) {
            m_envSettings.skyboxEnabled = json["skyboxEnabled"].get<bool>();
            if (m_renderer) m_renderer->setSkyboxEnabled(m_envSettings.skyboxEnabled);
        } else if (json.contains("skybox")) {
            m_envSettings.skyboxEnabled = json["skybox"].get<bool>();
            if (m_renderer) m_renderer->setSkyboxEnabled(m_envSettings.skyboxEnabled);
        }
        if (json.contains("iblEnabled")) {
            m_envSettings.iblEnabled = json["iblEnabled"].get<bool>();
            if (m_renderer) m_renderer->setIblEnabled(m_envSettings.iblEnabled);
        } else if (json.contains("ibl")) {
            m_envSettings.iblEnabled = json["ibl"].get<bool>();
            if (m_renderer) m_renderer->setIblEnabled(m_envSettings.iblEnabled);
        }
        if (json.contains("hdriPath") && json["hdriPath"].is_string()) {
            if (m_renderer) m_renderer->loadEnvironmentHdriFile(json["hdriPath"].get<std::string>());
        }
        if (json.contains("rtDebugMode")) {
            if (m_renderer) m_renderer->setRtDebugMode(static_cast<renderer::RtDebugMode>(json["rtDebugMode"].get<int>()));
        }

        if (json.contains("uiScale")) {
            setUiScale(json["uiScale"].get<float>());
        }
        if (json.contains("resolutionScale")) {
            setResolutionScale(json["resolutionScale"].get<float>());
        }
        if (json.contains("renderResolution") && json["renderResolution"].is_object()) {
            const auto& rr = json["renderResolution"];
            if (rr.contains("width") && rr.contains("height")) {
                setRenderResolution(rr["width"].get<uint32_t>(), rr["height"].get<uint32_t>());
            }
        }
        core::Logger::info("Environment settings updated from Studio.");
    }

    nlohmann::json Engine::getEnvironmentJson() const {
        uint32_t rw = 1920, rh = 1080;
        getRenderResolution(rw, rh);
        bool skyboxOn = m_renderer ? m_renderer->isSkyboxEnabled() : m_envSettings.skyboxEnabled;
        bool iblOn = m_renderer ? m_renderer->isIblEnabled() : m_envSettings.iblEnabled;
        return {
            {"gravity", m_envSettings.gravity},
            {"hdri", skyboxOn || iblOn},
            {"hdriEnabled", skyboxOn || iblOn},
            {"skybox", skyboxOn},
            {"skyboxEnabled", skyboxOn},
            {"ibl", iblOn},
            {"iblEnabled", iblOn},
            {"ambientColor", {m_envSettings.ambientColor.r, m_envSettings.ambientColor.g, m_envSettings.ambientColor.b}},
            {"ambientIntensity", m_envSettings.ambientIntensity},
            {"sunColor", {m_envSettings.sunColor.r, m_envSettings.sunColor.g, m_envSettings.sunColor.b}},
            {"sunIntensity", m_envSettings.sunIntensity},
            {"sunDirection", {m_envSettings.sunDirection.x, m_envSettings.sunDirection.y, m_envSettings.sunDirection.z}},
            {"sunEnabled", m_envSettings.sunEnabled},
            {"sunDirectionFromPosition", m_envSettings.sunDirectionFromPosition},
            {"fog", m_envSettings.fogEnabled},
            {"fogEnabled", m_envSettings.fogEnabled},
            {"fogColor", {m_envSettings.fogColor.r, m_envSettings.fogColor.g, m_envSettings.fogColor.b}},
            {"fogNear", m_envSettings.fogNear},
            {"fogFar", m_envSettings.fogFar},
            {"uiScale", getUiScale()},
            {"resolutionScale", getResolutionScale()},
            {"renderResolution", {{"width", rw}, {"height", rh}}}
        };
    }

    void Engine::setRenderResolution(uint32_t width, uint32_t height) {
        if (m_renderer) {
            if (m_headless) {
                m_renderer->setRenderResolution(width, height);
            } else {
                m_renderer->requestResolution(width, height);
            }
        }
    }

    void Engine::getRenderResolution(uint32_t& width, uint32_t& height) const {
        if (m_renderer) {
            width = m_renderer->getWidth();
            height = m_renderer->getHeight();
        } else {
            width = 1920; height = 1080;
        }
    }

    void Engine::setResolutionScale(float scale) {
        if (m_renderer) m_renderer->setResolutionScale(scale);
    }

    float Engine::getResolutionScale() const {
        return m_renderer ? m_renderer->getResolutionScale() : 1.0f;
    }

    void Engine::setUiScale(float scale) {
        if (m_renderer) m_renderer->setUiScale(scale);
    }

    float Engine::getUiScale() const {
        return m_renderer ? m_renderer->getUiScale() : 1.0f;

    }

    void Engine::applyPreferences() {
        renderer::setEditorLanguage(m_preferences.language);
        setTargetFps(m_preferences.targetFps);
        setVsyncEnabled(m_preferences.vsync);
        if (m_renderer) {
            m_renderer->setUiScale(m_preferences.uiScale);
            m_renderer->setAntiAliasingMode(m_preferences.antiAliasingMode);
            m_renderer->setResolutionScale(m_preferences.resolutionScale);
            m_renderer->setShadowDistance(m_preferences.shadowDistance);
            m_renderer->setBloomEnabled(m_preferences.bloomEnabled);
            m_renderer->setBloomIntensity(m_preferences.bloomIntensity);
            m_renderer->setBloomThreshold(m_preferences.bloomThreshold);
            m_renderer->setGtaoEnabled(m_preferences.gtaoEnabled);
            m_renderer->setGtaoRadius(m_preferences.gtaoRadius);
            m_renderer->setGtaoIntensity(m_preferences.gtaoIntensity);
            m_renderer->setRayTracingEnabled(m_preferences.rayTracingEnabled);
            m_renderer->setExposure(m_preferences.exposure);
        }
    }

    bool Engine::saveUserPreferences(const std::string& path) {
        return m_preferences.saveToFile(path);
    }

    bool Engine::loadUserPreferences(const std::string& path) {
        return m_preferences.loadFromFile(path);
    }

    void Engine::initTestScene() {
        // Directional Light (Sun) - Celestial Sun Light source
        auto sun = m_registry->createEntity();
        m_registry->get().emplace<ecs::TagComponent>(sun, ecs::TagComponent{"DirectionalLight (Sun)"});
        m_registry->get().emplace<ecs::TransformComponent>(sun, glm::vec3(0.0f, 18.0f, 0.0f), glm::vec3(-45.0f, -40.0f, 0.0f), glm::vec3(1.0f));
        ecs::LightComponent sunLight;
        sunLight.type = "directional";
        sunLight.color = glm::vec3(1.0f, 0.98f, 0.92f);
        sunLight.intensity = 3.5f;
        sunLight.range = 0.0f;
        m_registry->get().emplace<ecs::LightComponent>(sun, sunLight);

        // Point Light (Warm Accent)
        auto ptLight = m_registry->createEntity();
        m_registry->get().emplace<ecs::TagComponent>(ptLight, ecs::TagComponent{"PointLight_Warm"});
        m_registry->get().emplace<ecs::TransformComponent>(ptLight, glm::vec3(3.5f, 4.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0.5f));
        ecs::LightComponent pt;
        pt.type = "point";
        pt.color = glm::vec3(1.0f, 0.85f, 0.45f);
        pt.intensity = 4.0f;
        pt.range = 18.0f;
        m_registry->get().emplace<ecs::LightComponent>(ptLight, pt);

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
                dynMesh.meshId = (count % 2 == 0) ? 2 : 3; // 2 = High-grade Cube, 3 = High-grade UV Sphere
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
        if (m_unifiedServer) m_unifiedServer->broadcastTextMessage(rpc.dump());
    }

    void Engine::broadcastSfx(const std::string& type, float volume) {
#ifdef _WIN32
        // Native C++ desktop audio playback directly from speakers
        std::thread([type]() {
            if (type == "hit") {
                MessageBeep(MB_ICONHAND);
            } else if (type == "coin" || type == "score" || type == "pickup") {
                MessageBeep(MB_OK);
            } else if (type == "jump") {
                MessageBeep(MB_ICONASTERISK);
            } else if (type == "laser") {
                MessageBeep(MB_ICONQUESTION);
            } else if (type == "gameover") {
                MessageBeep(MB_ICONEXCLAMATION);
            } else if (type == "explosion") {
                MessageBeep(MB_ICONHAND);
            } else if (type == "victory" || type == "powerup") {
                MessageBeep(MB_ICONASTERISK);
            } else {
                MessageBeep(MB_OK);
            }
        }).detach();
#endif
        if (!m_unifiedServer) return;
        nlohmann::json rpc = {
            {"jsonrpc", "2.0"},
            {"method", "engine.onPlaySfx"},
            {"params", {
                {"type", type},
                {"volume", volume}
            }}
        };
        if (m_unifiedServer) m_unifiedServer->broadcastTextMessage(rpc.dump());
    }

    void Engine::broadcastCameraShake(float intensity, float duration) {
        if (!m_unifiedServer) return;
        nlohmann::json rpc = {
            {"jsonrpc", "2.0"},
            {"method", "engine.onCameraShake"},
            {"params", {
                {"intensity", intensity},
                {"duration", duration}
            }}
        };
        if (m_unifiedServer) m_unifiedServer->broadcastTextMessage(rpc.dump());
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
        if (m_unifiedServer) {
            if (client && client->getReadyState() == ix::ReadyState::Open) {
                try { client->sendText(msg); } catch (...) {}
            }
        }
    }

    bool Engine::saveProjectToDisk(const std::string& folderPath, const nlohmann::json& projectData) {
        try {
            std::filesystem::path dir(folderPath);
            std::string projName = projectData.value("name", dir.filename().string());
            if (projName.empty()) projName = "MyProject";

            // 1. Create Professional Unreal Engine Folder Hierarchy
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

            // 2. Save <ProjectName>.uproject (Unreal Engine Standard Descriptor)
            nlohmann::json uproject = {
                {"FileVersion", 3},
                {"EngineAssociation", "0.1"},
                {"Category", ""},
                {"Description", projectData.value("description", "Lucid Engine Project")},
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
                logFile << "LogInit: Lucid Engine Project saved successfully at: " << folderPath << "\n";
                logFile.close();
            }

            core::Logger::info("Lucid Engine project structure successfully saved to disk: " + folderPath);
            return true;
        } catch (const std::exception& e) {
            core::Logger::error("Failed to save project to disk: " + std::string(e.what()));
            return false;
        }
    }

    nlohmann::json Engine::loadProjectFromDisk(const std::string& folderPath) {
        try {
            std::filesystem::path p(folderPath);
            std::filesystem::path targetFile;

            if (std::filesystem::is_regular_file(p)) {
                targetFile = p;
            } else if (p.extension() == ".uproject" || p.extension() == ".json") {
                targetFile = p;
            } else if (std::filesystem::is_directory(p)) {
                for (const auto& entry : std::filesystem::directory_iterator(p)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".uproject") {
                        targetFile = entry.path();
                        break;
                    }
                }
                if (targetFile.empty() || !std::filesystem::exists(targetFile)) {
                    if (std::filesystem::exists(p / "game.json")) {
                        targetFile = p / "game.json";
                    } else if (std::filesystem::exists(p / "project.json")) {
                        targetFile = p / "project.json";
                    }
                }
            }

            if (targetFile.empty() || !std::filesystem::exists(targetFile)) {
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

    
    std::string Engine::getCurrentScript() const {
        return m_currentScriptCode.empty() ? const_cast<Engine*>(this)->getDefaultScript(m_currentPreset) : m_currentScriptCode;
    }

    void Engine::setCurrentScript(const std::string& code) {
        m_currentScriptCode = code;
    }

    bool Engine::saveCurrentProject(const std::string& folderPath) {
        try {
            std::filesystem::path p(folderPath);
            std::filesystem::path targetDir;
            std::string projName;

            if (p.has_extension() && (p.extension() == ".uproject" || p.extension() == ".json")) {
                targetDir = p.parent_path();
                projName = p.stem().string();
                if (targetDir.empty()) targetDir = ".";
            } else {
                targetDir = p;
                projName = p.filename().string();
            }
            if (projName.empty()) projName = "LucidGame";

            nlohmann::json proj = {
                {"name", projName},
                {"preset", m_currentPreset},
                {"camera", {
                    {"x", m_camPos.x},
                    {"y", m_camPos.y},
                    {"z", m_camPos.z},
                    {"yaw", m_camYaw},
                    {"pitch", m_camPitch},
                    {"fov", m_camFov}
                }},
                {"entities", getSnapshotJson()},
                {"script", m_currentScriptCode.empty() ? getDefaultScript(m_currentPreset) : m_currentScriptCode},
                {"environment", getEnvironmentJson()},
                {"settings", {
                    {"gravity", m_envSettings.gravity}
                }}
            };
            bool res = saveProjectToDisk(targetDir.string(), proj);
            std::filesystem::create_directories(targetDir);
            std::ofstream gj(targetDir / "game.json");
            if (gj.is_open()) {
                gj << proj.dump(2);
                gj.close();
            }
            if (p.has_extension() && p.extension() == ".uproject") {
                nlohmann::json uproject = {
                    {"FileVersion", 3},
                    {"EngineAssociation", "0.1"},
                    {"Category", ""},
                    {"Description", proj.value("description", "Lucid Engine Project")},
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
                    {"lucidEngine", proj}
                };
                std::ofstream upFile(p);
                if (upFile.is_open()) {
                    upFile << uproject.dump(2);
                    upFile.close();
                }
            } else if (p.has_extension() && p.extension() == ".json") {
                std::ofstream jFile(p);
                if (jFile.is_open()) {
                    jFile << proj.dump(2);
                    jFile.close();
                }
            }

            core::Logger::info("saveCurrentProject successful: " + folderPath);
            return res;
        } catch (const std::exception& e) {
            core::Logger::error(std::string("saveCurrentProject failed: ") + e.what());
            return false;
        }
    }

    bool Engine::loadCurrentProject(const std::string& folderPath) {
        try {
            auto proj = loadProjectFromDisk(folderPath);
            if (proj.contains("error")) {
                core::Logger::error("Failed to load project: " + proj["error"].get<std::string>());
                return false;
            }

            // 1. Clean reset previous scene so entities & bodies do not leak or collide
            resetScene("clean");

            // 2. Set preset
            if (proj.contains("preset")) {
                m_currentPreset = proj["preset"].get<std::string>();
            }

            // 3. Restore camera
            if (proj.contains("camera") && proj["camera"].is_object()) {
                const auto& cam = proj["camera"];
                glm::vec3 cPos = m_camPos;
                if (cam.contains("x")) cPos.x = cam["x"].get<float>();
                if (cam.contains("y")) cPos.y = cam["y"].get<float>();
                if (cam.contains("z")) cPos.z = cam["z"].get<float>();
                float cYaw = cam.value("yaw", m_camYaw);
                float cPitch = cam.value("pitch", m_camPitch);
                float cFov = cam.value("fov", m_camFov);
                setCamera(cPos, cYaw, cPitch, cFov);
            }
            if (proj.contains("cameraMode") && proj["cameraMode"].is_string()) {
                m_cameraMode = proj["cameraMode"].get<std::string>();
            }

            // 4. Restore entities
            if (proj.contains("entities") && proj["entities"].is_array()) {
                for (auto& e : proj["entities"]) if (e.is_object()) e.erase("id");
            }
            if (proj.contains("entities")) {
                applySnapshotJson(proj["entities"].dump());
            } else if (proj.contains("scenes") && proj["scenes"].is_array() && !proj["scenes"].empty()) {
                auto& sc = proj["scenes"][0];
                if (sc.contains("entities") && sc["entities"].is_array()) {
                    for (auto& e : sc["entities"]) if (e.is_object()) e.erase("id");
                }
                if (sc.contains("entities")) {
                    applySnapshotJson(sc["entities"].dump());
                }
            }

            // 5. Restore script
            if (proj.contains("script")) {
                loadScript(proj["script"].get<std::string>());
            } else if (proj.contains("scripts") && proj["scripts"].is_array() && !proj["scripts"].empty()) {
                loadScript(proj["scripts"][0].value("code", ""));
            }

            // 6. Restore environment & lighting
            if (proj.contains("environment")) {
                setEnvironmentFromJson(proj["environment"]);
            }
            if (proj.contains("settings") && proj["settings"].contains("gravity")) {
                m_envSettings.gravity = proj["settings"]["gravity"].get<float>();
            }

            // 7. Invalidate denoiser & temporal history so old frame samples don't smudge the new scene
            invalidateRtHistory();

            m_preSimulationSnapshot = getSnapshotJson();
            m_hasPreSimulationSnapshot = true;
            core::Logger::info("loadCurrentProject successful: " + folderPath);
            return true;
        } catch (const std::exception& e) {
            core::Logger::error(std::string("loadCurrentProject failed: ") + e.what());
            return false;
        }
    }

    bool Engine::packageStandaloneGame(const std::string& targetFolder, const std::string& gameName) {
        try {
            std::filesystem::path outDir(targetFolder);
            std::filesystem::path gameDir = outDir;
            std::string outLeaf = outDir.filename().string();
            if (outLeaf == "Windows" || outLeaf == "Release" || outLeaf == "build" || outLeaf == "dist") {
                gameDir = outDir / gameName;
            }
            std::filesystem::create_directories(gameDir);
            std::filesystem::create_directories(gameDir / "assets" / "models");
            std::filesystem::create_directories(gameDir / "assets" / "textures");
            std::filesystem::create_directories(gameDir / "shaders");

            // 1. Copy engine binary -> <gameName>.exe and engine.exe fallback
            std::vector<std::filesystem::path> exeCandidates = {
                "engine/build/Release/engine.exe",
                "engine/build/Debug/engine.exe",
                "engine.exe",
                "build/Release/engine.exe"
            };
            std::filesystem::path exeSource;
            for (const auto& c : exeCandidates) {
                if (std::filesystem::exists(c)) {
                    exeSource = c;
                    break;
                }
            }
            if (!exeSource.empty()) {
                std::filesystem::copy_file(exeSource, gameDir / (gameName + ".exe"), std::filesystem::copy_options::overwrite_existing);
                std::filesystem::copy_file(exeSource, gameDir / "engine.exe", std::filesystem::copy_options::overwrite_existing);
            }

            // 2. Copy ALL compiled SPIR-V shaders (*.spv) to both root and shaders/
            std::vector<std::filesystem::path> shaderSearchDirs = {
                "engine/build/Release",
                "engine/build",
                "engine/build/Debug"
            };
            for (const auto& sDir : shaderSearchDirs) {
                if (std::filesystem::exists(sDir)) {
                    for (const auto& entry : std::filesystem::directory_iterator(sDir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".spv") {
                            std::filesystem::copy_file(entry.path(), gameDir / entry.path().filename(), std::filesystem::copy_options::overwrite_existing);
                            std::filesystem::copy_file(entry.path(), gameDir / "shaders" / entry.path().filename(), std::filesystem::copy_options::overwrite_existing);
                        }
                    }
                }
            }

            // 3. Copy Assets (models, textures, fonts, slate)
            auto copyDirRecursive = [](const std::filesystem::path& src, const std::filesystem::path& dst) {
                if (!std::filesystem::exists(src)) return;
                std::filesystem::create_directories(dst);
                for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
                    auto rel = std::filesystem::relative(entry.path(), src);
                    auto target = dst / rel;
                    if (entry.is_directory()) {
                        std::filesystem::create_directories(target);
                    } else if (entry.is_regular_file()) {
                        std::filesystem::create_directories(target.parent_path());
                        std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing);
                    }
                }
            };

            copyDirRecursive("engine/assets", gameDir / "assets");
            copyDirRecursive("engine/assets", gameDir / "engine" / "assets");
            copyDirRecursive("public/assets", gameDir / "assets");

            // 4. Save game project package descriptor (game.json)
            nlohmann::json gamePackage = {
                {"name", gameName},
                {"engineAssociation", "Lucid Native Vulkan Runtime"},
                {"preset", m_currentPreset},
                {"camera", {
                    {"x", m_camPos.x},
                    {"y", m_camPos.y},
                    {"z", m_camPos.z},
                    {"yaw", m_camYaw},
                    {"pitch", m_camPitch},
                    {"fov", m_camFov}
                }},
                {"cameraMode", m_cameraMode},
                {"entities", getSnapshotJson()},
                {"script", m_currentScriptCode.empty() ? getDefaultScript(m_currentPreset) : m_currentScriptCode},
                {"environment", getEnvironmentJson()},
                {"settings", {
                    {"gravity", m_envSettings.gravity},
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
                        << "title " << gameName << " - Lucid Engine (Vulkan 1.3 PC Game)\n"
                        << "cd /d \"%~dp0\"\n"
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

    void Engine::detonateExplosion(const glm::vec3& center, float radius, float force, float damage) {
        if (m_destructionSystem && m_registry && m_physicsContext) {
            m_destructionSystem->detonateExplosion(m_registry.get(), m_physicsContext.get(), this, center, radius, force, damage);
        }
    }

    void Engine::fractureEntity(uint32_t entityId) {
        if (m_destructionSystem && m_registry && m_physicsContext) {
            auto e = static_cast<entt::entity>(entityId);
            if (m_registry->get().valid(e) && m_registry->get().all_of<ecs::TransformComponent>(e)) {
                auto& t = m_registry->get().get<ecs::TransformComponent>(e);
                m_destructionSystem->fractureEntity(m_registry.get(), m_physicsContext.get(), this, entityId, t.position, glm::vec3(0.0f, 6.0f, 0.0f));
            }
        }
    }

    void Engine::igniteEntity(uint32_t entityId, float temp) {
        if (m_chemistrySystem && m_registry) {
            m_chemistrySystem->ignite(m_registry.get(), entityId, temp);
        }
    }

    void Engine::extinguishEntity(uint32_t entityId) {
        if (m_chemistrySystem && m_registry) {
            m_chemistrySystem->extinguish(m_registry.get(), entityId);
        }
    }

    void Engine::electrifyEntity(uint32_t entityId, float voltage) {
        if (m_chemistrySystem && m_registry) {
            m_chemistrySystem->electrify(m_registry.get(), entityId, voltage);
        }
    }

    void Engine::freezeEntity(uint32_t entityId) {
        if (m_chemistrySystem && m_registry) {
            m_chemistrySystem->freeze(m_registry.get(), entityId);
        }
    }

    std::vector<glm::vec3> Engine::findPath(const glm::vec3& start, const glm::vec3& target) {
        if (m_navigationSystem) {
            return m_navigationSystem->findPath(start, target);
        }
        return { target };
    }

    void Engine::generateProceduralWorld(float areaSize, float resolution) {
        if (m_pcgSystem && m_registry) {
            m_pcgSystem->generateBiomeTerrain(m_registry.get(), areaSize, resolution);
            m_pcgSystem->scatterFoliage(m_registry.get(), areaSize, 40);
            if (m_navigationSystem) {
                m_navigationSystem->buildFromScene(m_registry.get());
            }
        }
    }

    nlohmann::json Engine::getProfilerStats() {
        auto stats = Profiler::get().getStatsJson();
        if (m_registry) {
            stats["activeEntities"] = m_registry->get().storage<entt::entity>().size();
        }
        if (m_worldPartition) {
            stats["loadedCells"] = m_worldPartition->getLoadedCellCount();
            stats["totalCells"] = m_worldPartition->getTotalCellCount();
            auto origin = m_worldPartition->getWorldOriginDouble();
            stats["worldOrigin64"] = { origin.x, origin.y, origin.z };
        }
        return stats;
    }

    bool Engine::liveReloadScript(const std::string& scriptCode) {
        core::Logger::info("Live Coding: Hot-reloading script...");
        if (!m_scriptContext) {
            core::Logger::error("Live Coding failed: QuickJS context is unavailable.");
            return false;
        }

        if (!m_scriptContext->evaluate(scriptCode, "live_reload.js")) {
            core::Logger::error("Live Coding failed; keeping the previous script source.");
            return false;
        }

        m_currentScriptCode = scriptCode;
        return true;
    }

    void Engine::queueSubTickInput(uint32_t buttonMask, float yaw, float pitch) {
        if (m_replicationGraph) {
            network::SubTickInput in;
            in.tick = m_currentTick;
            in.microsecondTimestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()
            ).count();
            in.buttonMask = buttonMask;
            in.lookYaw = yaw;
            in.lookPitch = pitch;
            m_replicationGraph->queueSubTickInput(in);
        }
    }

    nlohmann::json Engine::getReplicationDelta(uint32_t baselineTick) {
        if (m_replicationGraph) {
            return m_replicationGraph->generateDeltaJson(m_currentTick, baselineTick);
        }
        return nlohmann::json::object();
    }
    void Engine::setVsyncEnabled(bool enabled) {
        if (m_vulkanContext) m_vulkanContext->setVsyncEnabled(enabled);
    }
    bool Engine::isVsyncEnabled() const {
        return m_vulkanContext ? m_vulkanContext->isVsyncEnabled() : true;
    }
}
