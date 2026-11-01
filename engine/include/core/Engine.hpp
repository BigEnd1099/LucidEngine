#pragma once
#include <atomic>
#include <thread>
#include <string>
#include <future>
#include <chrono>
#include "core/ThreadQueue.hpp"
#include "ecs/Registry.hpp"

#include "core/AssetManager.hpp"
#include "network/WebSocketServer.hpp"
#include "network/JsonRpcDispatcher.hpp"
#include "script/QuickJSContext.hpp"
#include "renderer/VulkanContext.hpp"
#include "renderer/VulkanRenderer.hpp"

#include "network/UdpAnimationServer.hpp"
#include "network/McpServer.hpp"
#include "animation/SkeletonStream.hpp"
#include "physics/PhysicsContext.hpp"
#include "physics/PhysicsSystem.hpp"

namespace engine::core {
    class Engine {
    public:
        Engine(int wsPort, bool headless);
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
        void togglePause();
        bool isPaused() const { return m_paused; }
        void broadcastSimulationState();
        void setStandaloneGameMode(bool enabled);
        bool isStandaloneGameMode() const { return m_standaloneGameMode; }
        bool packageStandaloneGame(const std::string& targetFolder, const std::string& gameName = "LucidGame");

        ecs::Registry* getRegistry() { return m_registry.get(); }
        physics::PhysicsContext* getPhysicsContext() { return m_physicsContext.get(); }
        AssetManager* getAssetManager() { return m_assetManager.get(); }

        // Camera API (Synchronized between native application & Web studio)
        glm::vec3 getCameraPosition() const { return m_camPos; }
        float getCameraYaw() const { return m_camYaw; }
        float getCameraPitch() const { return m_camPitch; }
        float getCameraFov() const { return m_camFov; }
        void setCamera(const glm::vec3& pos, float yaw, float pitch, float fov = 45.0f) {
            m_camPos = pos;
            m_camYaw = yaw;
            m_camPitch = pitch;
            m_camFov = fov;
        }

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

    private:
        void update(float dt);
        void render();

        std::atomic<bool> m_running{true};
        bool m_headless;
        bool m_standaloneGameMode{false};
        bool m_paused{true};
        nlohmann::json m_preSimulationSnapshot = nlohmann::json::array();
        bool m_hasPreSimulationSnapshot{false};
        float m_physicsAccumulator{0.0f};
        static constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;

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

        nlohmann::json m_inputState = nlohmann::json::object();

        ThreadQueue<network::JsonRpcDispatcher::Command> m_commandQueue;
        
        std::unique_ptr<ecs::Registry> m_registry;
        std::unique_ptr<script::QuickJSContext> m_scriptContext;
        std::unique_ptr<network::WebSocketServer> m_wsServer;
        std::unique_ptr<network::UdpAnimationServer> m_udpServer;
        std::unique_ptr<network::mcp::McpServer> m_mcpServer;
        
        std::unique_ptr<physics::PhysicsContext> m_physicsContext;
        std::unique_ptr<physics::PhysicsSystem> m_physicsSystem;

        std::unique_ptr<renderer::VulkanContext> m_vulkanContext;
        std::unique_ptr<renderer::VulkanRenderer> m_renderer;
        std::unique_ptr<AssetManager> m_assetManager;

        std::jthread m_networkThread;
        std::jthread m_mcpThread;
    };
}
