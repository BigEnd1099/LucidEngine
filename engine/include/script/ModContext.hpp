#pragma once
#include <string>
#include <functional>
#include <chrono>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

struct JSRuntime;
struct JSContext;

namespace engine::script {

    class ModContext {
    public:
        explicit ModContext(const std::string& modName,
                            size_t memoryLimitBytes = 16 * 1024 * 1024,
                            size_t stackLimitBytes = 256 * 1024);
        ~ModContext();

        const std::string& get_name() const { return m_modName; }

        bool load_script(const std::string& code, const std::string& filename = "mod.js");
        bool execute_with_timeout(const std::string& code, uint32_t timeoutMs = 100);

        void set_on_error(std::function<void(const std::string&, const std::string&)> cb) {
            m_onError = std::move(cb);
        }

        const std::string& get_last_error() const { return m_lastError; }
        bool has_crashed() const { return m_crashed; }
        void reset_crash() { m_crashed = false; m_lastError.clear(); }

    private:
        void _install_allowlist_api();

        std::string m_modName;
        JSRuntime* m_rt{nullptr};
        JSContext* m_ctx{nullptr};
        bool m_interrupted{false};
        bool m_hasDeadline{false};
        bool m_crashed{false};
        std::chrono::steady_clock::time_point m_deadline;
        std::string m_lastError;
        std::function<void(const std::string&, const std::string&)> m_onError;
    };

} // namespace engine::script
