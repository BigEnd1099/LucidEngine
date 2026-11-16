#pragma once
#include <string>
#include <vector>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

namespace engine::core {

    enum class AccessMode {
        Read,
        Write
    };

    struct ComponentAccess {
        std::type_index type;
        AccessMode mode;
    };

    struct SystemDesc {
        std::string name;
        std::vector<ComponentAccess> component_access;
        std::function<void(float dt)> execute;
        std::vector<std::string> explicit_after;
        bool allow_parallel{true};
    };

    struct SystemTiming {
        std::string name;
        double durationMs{0.0};
    };

    class SystemScheduler {
    public:
        explicit SystemScheduler(size_t threadCount = 0);
        ~SystemScheduler();

        void register_system(SystemDesc desc);
        bool build_graph();
        void execute(float dt);

        void set_single_threaded(bool enabled) { m_singleThreaded = enabled; }
        bool is_single_threaded() const { return m_singleThreaded; }

        std::vector<SystemTiming> get_system_timings() const;
        void clear();

    private:
        bool _has_conflict(const SystemDesc& a, const SystemDesc& b) const;

        std::vector<SystemDesc> m_systems;
        std::vector<std::vector<size_t>> m_stages; // Batches of systems that can run concurrently
        std::unordered_map<std::string, double> m_timings;
        mutable std::mutex m_timingsMutex;

        bool m_singleThreaded{false};
        bool m_compiled{false};
        size_t m_threadCount{4};

        // Worker Thread Pool
        std::vector<std::jthread> m_workers;
        std::vector<std::function<void()>> m_taskQueue;
        std::mutex m_queueMutex;
        std::condition_variable m_cvTask;
        std::condition_variable m_cvDone;
        std::atomic<size_t> m_activeTasks{0};
        std::atomic<bool> m_stop{false};
    };

} // namespace engine::core
