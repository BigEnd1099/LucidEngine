#pragma once
#include <string>
#include <functional>
#include <vector>
#include <nlohmann/json.hpp>
#include "physics/PhysicsContext.hpp"

// Forward declarations
struct JSRuntime;
struct JSContext;

namespace engine::core { class Engine; }
namespace engine::ecs { class Registry; }

namespace engine::script {
    struct ScriptErrorInfo {
        std::string filename;
        int line;
        std::string message;
        std::string stackTrace;
    };

    class QuickJSContext {
    public:
        QuickJSContext();
        ~QuickJSContext();

        void init(core::Engine* engineInstance);
        bool evaluate(const std::string& code, const std::string& filename = "script.js");
        void setMemoryLimit(size_t bytes);
        
        void setOnError(std::function<void(const std::string&, const std::string&, const std::string&)> cb) { m_onError = cb; }
        std::string tick(const std::string& stateJson, float dt, const std::string& inputJson = "{}");

        // Fast zero-copy direct tick avoiding JSON serialization/deserialization every frame
        void tickDirect(engine::ecs::Registry* registry, engine::physics::PhysicsContext* physics, float dt, const nlohmann::json& inputState, const std::vector<engine::physics::ContactPoint>& contacts = {});

        // To handle interrupts (watchdog)
        void interrupt();
        void resetInterrupt();

    private:
        static void bindEngineAPI(JSContext* ctx, core::Engine* engineInstance);

        JSRuntime* m_rt{nullptr};
        JSContext* m_ctx{nullptr};
        bool m_interrupted{false};
        std::function<void(const std::string&, const std::string&, const std::string&)> m_onError;
    };
}
