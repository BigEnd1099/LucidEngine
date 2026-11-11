#pragma once
#include <atomic>
#include <thread>
#include <string>
#include <future>
#include <chrono>
#include <memory>
#include <vector>
#include "core/ThreadQueue.hpp"
#include "ecs/Registry.hpp"

#include "core/AssetManager.hpp"
#include "network/WebSocketServer.hpp"
#include "network/SharedStateRing.hpp"
#include "network/MpscCommandQueue.hpp"
#include "network/SinglePortUnifiedServer.hpp"
#include "network/JsonRpcDispatcher.hpp"
#include "script/QuickJSContext.hpp"
#include "renderer/VulkanContext.hpp"
#include "core/EditorPreferences.hpp"
#include "renderer/VulkanRenderer.hpp"

#include "network/UdpAnimationServer.hpp"
#include "network/McpServer.hpp"
#include "animation/SkeletonStream.hpp"
#include "physics/PhysicsContext.hpp"
#include "physics/PhysicsSystem.hpp"

// AAA Subsystems
#include "core/Profiler.hpp"
#include "physics/ChemistrySystem.hpp"
#include "physics/DestructionSystem.hpp"
#include "ai/NavigationSystem.hpp"
#include "ai/BehaviorTree.hpp"
#include "animation/System/MotionMatching.hpp"
#include "world/WorldPartition.hpp"
#include "world/PcgSystem.hpp"
#include "network/Netcode/ReplicationGraph.hpp"
#include "audio/SpatialAudioEngine.hpp"

namespace engine::core {
    struct EnvironmentSettings {
        float gravity{-9.81f};
        glm::vec3 ambientColor{0.15f, 0.22f, 0.35f};
        float ambientIntensity{1.0f};
        glm::vec3 sunColor{1.0f, 0.97f, 0.92f};
        float sunIntensity{3.4f};
        glm::vec3 sunDirection{0.55f, 0.85f, 0.40f};
        bool sunEnabled{true};
        bool sunDirectionFromPosition{false};
        bool fogEnabled{false};
        glm::vec3 fogColor{0.06f, 0.07f, 0.09f};
        float fogNear{20.0f};
        float fogFar{100.0f}; bool skyboxEnabled{true}; bool iblEnabled{true};
    };

    class Engine {
    public:
        Engine(int wsPort = 9000, bool headless = false, bool enableRemote = true, bool benchmarkMode = false);
        ~Engine();

        void run();
        void stop();
        bool isRunning() const { return m_running.load(); }

        // JSON-RPC & AI Actions
        bool loadScript(const std::string& code);
        uint32_t spawnEntityFromJson(const nlohmann::json& components);
        bool destroyEntity(uint32_t id);
        void resetScene(const std::string& preset = "physics");
        void applySnapshotJson(const std::string& jsonStr);
        std::string captureFrameBase64();
        std::string captureAovBase64(const std::string& aovName, nlohmann::json& outMetadata);
        void stepSimulation(int steps, float dt);
        nlohmann::json getSnapshotJson();
        std::string getTypeDefinitions();
        void initTestScene();
        void exportTypeDefinitions();
        void broadcastScriptError(const std::string& type, const std::string& msg, const std::string& stack);
        void sendWsMessage(ix::WebSocket* client, const std::string& msg);

        // Disk Project Management
        bool saveProjectToDisk(const std::string& folderPath, const nlohmann::json& projectData);
        nlohmann::json loadProjectFromDisk(const std::string& folderPath);
        nlohmann::json listDiskProjects(const std::string& baseDir = "projects");
        bool saveCurrentProject(const std::string& folderPath = "projects/MyGame");
        bool loadCurrentProject(const std::string& folderPath = "projects/MyGame");
        void requestLoadProject(const std::string& path);
        std::string getCurrentScript() const;
        void setCurrentScript(const std::string& code);

        template <typename F>
        auto executeOnMainThread(F&& func) -> decltype(func(this)) {
            using ReturnType = decltype(func(this));
            auto promise = std::make_shared<std::promise<ReturnType>>();
            auto future = promise->get_future();
            m_commandQueue.push([promise, func = std::forward<F>(func)](Engine* eng) {
                try {
                    if constexpr (std::is_void_v<ReturnType>) {
                        func(eng);
                        promise->set_value();
                    } else {
                        promise->set_value(func(eng));
                    }
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            });
            if (future.wait_for(std::chrono::seconds(10)) == std::future_status::ready) {
                return future.get();
            }
            throw std::runtime_error("Command execution on main thread timed out");
        }

        void setPaused(bool paused);
        void playSimulation();
        void stopSimulation();
        void restartSimulation();
        void togglePause();
        bool isPaused() const { return m_paused; }
        bool isSimulationSessionActive() const { return m_simulationSessionActive; }
        void broadcastSimulationState();
        void setStandaloneGameMode(bool enabled);
        bool isStandaloneGameMode() const { return m_standaloneGameMode; }
        bool packageStandaloneGame(const std::string& targetFolder, const std::string& gameName = "LucidGame");

        ecs::Registry* getRegistry() { return m_registry.get(); }
        physics::PhysicsContext* getPhysicsContext() { return m_physicsContext.get(); }
        AssetManager* getAssetManager() { return m_assetManager.get(); }

        // AAA Subsystems Accessors
        physics::ChemistrySystem* getChemistrySystem() { return m_chemistrySystem.get(); }
        physics::DestructionSystem* getDestructionSystem() { return m_destructionSystem.get(); }
        ai::NavigationSystem* getNavigationSystem() { return m_navigationSystem.get(); }
        anim::MotionMatching* getMotionMatching() { return m_motionMatching.get(); }
        world::WorldPartition* getWorldPartition() { return m_worldPartition.get(); }
        world::PcgSystem* getPcgSystem() { return m_pcgSystem.get(); }
        network::ReplicationGraph* getReplicationGraph() { return m_replicationGraph.get(); }
        audio::SpatialAudioEngine* getAudioEngine() { return m_audioEngine.get(); }

        // Camera API (Synchronized between native application & Web studio)
        glm::vec3 getCameraPosition() const { return m_camPos; }
        float getCameraYaw() const { return m_camYaw; }
        float getCameraPitch() const { return m_camPitch; }
        float getCameraFov() const { return m_camFov; }
        void setCameraFov(float fov) { m_camFov = fov; }
        void setCamera(const glm::vec3& pos, float yaw, float pitch, float fov = 45.0f) {
            m_camPos = pos;
            m_camYaw = yaw;
            m_camPitch = pitch;
            m_camFov = fov;
        }
        renderer::VulkanRenderer* getRenderer() { return m_renderer.get(); }
        void invalidateMaterialDescriptor(uint32_t materialId = 0) {
            if (m_renderer) m_renderer->invalidateMaterialDescriptorSet(materialId);
        }
        void invalidateRtHistory() {
            if (m_renderer) m_renderer->invalidateRtHistory();
        }

        // Camera Mode API (editor, thirdPerson, topDown, firstPerson, sideScroller)
        void setCameraMode(const std::string& mode) { m_cameraMode = mode; }
        std::string getCameraMode() const { return m_cameraMode; }

        // Gameplay Input API (Keyboard & Controller)
        void setKeyInput(const std::string& key, bool pressed) {
            if (!m_inputState.contains("keys") || !m_inputState["keys"].is_object()) {
                m_inputState["keys"] = nlohmann::json::object();
            }
            m_inputState["keys"][key] = pressed;
        }

        // Environment & Lighting API
        void setEnvironmentSettings(const EnvironmentSettings& env);
        void setGravity(float g) { m_envSettings.gravity = g; }
        float getGravity() const { return m_envSettings.gravity; }
        const EnvironmentSettings& getEnvironmentSettings() const { return m_envSettings; }
        void setEnvironmentFromJson(const nlohmann::json& json);
        nlohmann::json getEnvironmentJson() const;

        // Selection & Physics Sync API
        entt::entity getSelectedEntity() const { return m_selectedEntity; }
        void setSelectedEntity(entt::entity e) { m_selectedEntity = e; }
        void syncEntityTransformPhysics(entt::entity entity);

        // Resolution, FPS Limiter & VSync APIs
        void setRenderResolution(uint32_t width, uint32_t height);
        void getRenderResolution(uint32_t& width, uint32_t& height) const;
        void setResolutionScale(float scale);
        float getResolutionScale() const;
        void setUiScale(float scale);
        void setTargetFps(int fps) { m_targetFps = fps; }
        int getTargetFps() const { return m_targetFps; }
        void setVsyncEnabled(bool enabled);
        bool isVsyncEnabled() const;
        float getUiScale() const;

        // User Preferences & Config API
        EditorPreferences& getPreferences() { return m_preferences; }
        const EditorPreferences& getPreferences() const { return m_preferences; }
        void applyPreferences();
        bool saveUserPreferences(const std::string& path = "");
        bool loadUserPreferences(const std::string& path = "");

        // Input API (Gamepad, Keyboard, Mouse from Studio)
        void setInputState(const nlohmann::json& input) { m_inputState = input; }
        nlohmann::json getInputState() const { return m_inputState; }

        // Game State, Health & Audio API
        void setScore(int score) { m_score.store(score); }
        int getScore() const { return m_score.load(); }
        void setHealth(int hp) { m_health.store(hp); }
        int getHealth() const { return m_health.load(); }
        void setGameStatus(const std::string& status) { m_gameStatus = status; }
        std::string getGameStatus() const { return m_gameStatus; }
        void broadcastSfx(const std::string& type, float volume = 1.0f);
        void broadcastCameraShake(float intensity = 0.5f, float duration = 0.3f);
        void setGameState(const nlohmann::json& state) { m_gameState = state; }
        nlohmann::json getGameState() const { return m_gameState; }
        const std::vector<physics::ContactPoint>& getLastContacts() const { return m_lastContacts; }

        // Preset and Default Script API
        std::string getDefaultScript(const std::string& preset);
        std::string getCurrentPreset() const { return m_currentPreset; }
        void setCurrentPreset(const std::string& p) { m_currentPreset = p; }

        // Generative Asset / Animation APIs
        uint32_t loadGlbFromBase64(const std::string& base64Str);
        void applyPbrMaterial(uint32_t entityId, const nlohmann::json& args);
        void updateEntityBones(uint32_t entityId, const std::vector<anim::BoneTransformRaw>& bones);

        // AAA High-Level Gameplay & Physics APIs
        void detonateExplosion(const glm::vec3& center, float radius, float force, float damage);
        void fractureEntity(uint32_t entityId);
        void igniteEntity(uint32_t entityId, float temp = 400.0f);
        void extinguishEntity(uint32_t entityId);
        void electrifyEntity(uint32_t entityId, float voltage = 100.0f);
        void freezeEntity(uint32_t entityId);
        std::vector<glm::vec3> findPath(const glm::vec3& start, const glm::vec3& target);
        void generateProceduralWorld(float areaSize = 120.0f, float resolution = 4.0f);
        nlohmann::json getProfilerStats();
        bool liveReloadScript(const std::string& scriptCode);
        void queueSubTickInput(uint32_t buttonMask, float yaw, float pitch);
        nlohmann::json getReplicationDelta(uint32_t baselineTick);

        // Dual-Transport UI Shell Accessors
        network::SharedStateRingBuffer* getSharedStateRing() const { return m_sharedStateRing.get(); }
        network::MpscCommandQueue* getMpscCommandQueue() const { return m_mpscCommandQueue.get(); }
        network::SinglePortUnifiedServer* getUnifiedServer() const { return m_unifiedServer.get(); }

    private:
        void update(float dt);
        void render();

        std::atomic<bool> m_running{true};
        bool m_headless;
        bool m_standaloneGameMode{false};
        bool m_paused{true};
        bool m_simulationSessionActive{false};
        int m_targetFps{0}; // 0 = unlimited, or 30, 60, 120, 240
        nlohmann::json m_preSimulationSnapshot = nlohmann::json::array();
        bool m_hasPreSimulationSnapshot{false};
        glm::vec3 m_preSimulationCamPos{0.0f, 15.0f, 30.0f};
        float m_preSimulationCamYaw{-90.0f};
        float m_preSimulationCamPitch{-20.0f};
        float m_preSimulationCamFov{45.0f};
        std::string m_preSimulationCamMode{"editor"};
        std::string m_pendingProjectLoadPath;
        bool m_hasPendingProjectLoad{false};
        float m_physicsAccumulator{0.0f};
        static constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;
        uint32_t m_currentTick{0};

        std::atomic<int> m_score{0};
        std::atomic<int> m_health{100};
        std::string m_gameStatus{"PLAYING"};
        std::string m_currentPreset{"breakout"};
        std::string m_currentScriptCode{""};
        nlohmann::json m_gameState = nlohmann::json::object();
        std::vector<physics::ContactPoint> m_lastContacts;

        glm::vec3 m_camPos{0.0f, 15.0f, 30.0f};
        float m_camYaw{-90.0f};
        float m_camPitch{-20.0f};
        float m_camFov{45.0f};
        std::string m_cameraMode{"editor"};
        entt::entity m_selectedEntity{entt::null};
        EnvironmentSettings m_envSettings;
        EditorPreferences m_preferences;

        nlohmann::json m_inputState = nlohmann::json::object();

        ThreadQueue<network::JsonRpcDispatcher::Command> m_commandQueue;
        
        std::unique_ptr<ecs::Registry> m_registry;
        std::unique_ptr<script::QuickJSContext> m_scriptContext;
        std::unique_ptr<network::WebSocketServer> m_wsServer;
        std::unique_ptr<network::SharedStateRingBuffer> m_sharedStateRing;
        std::unique_ptr<network::MpscCommandQueue> m_mpscCommandQueue;
        std::unique_ptr<network::SinglePortUnifiedServer> m_unifiedServer;
        std::unique_ptr<network::UdpAnimationServer> m_udpServer;
        std::unique_ptr<network::mcp::McpServer> m_mcpServer;
        
        std::unique_ptr<physics::PhysicsContext> m_physicsContext;
        std::unique_ptr<physics::PhysicsSystem> m_physicsSystem;

        // AAA Subsystems
        std::unique_ptr<physics::ChemistrySystem> m_chemistrySystem;
        std::unique_ptr<physics::DestructionSystem> m_destructionSystem;
        std::unique_ptr<ai::NavigationSystem> m_navigationSystem;
        std::unique_ptr<anim::MotionMatching> m_motionMatching;
        std::unique_ptr<world::WorldPartition> m_worldPartition;
        std::unique_ptr<world::PcgSystem> m_pcgSystem;
        std::unique_ptr<network::ReplicationGraph> m_replicationGraph;
        std::unique_ptr<audio::SpatialAudioEngine> m_audioEngine;

        std::unique_ptr<renderer::VulkanContext> m_vulkanContext;
        std::unique_ptr<renderer::VulkanRenderer> m_renderer;
        std::unique_ptr<AssetManager> m_assetManager;

        std::jthread m_networkThread;
        std::jthread m_mcpThread;
    };
}
