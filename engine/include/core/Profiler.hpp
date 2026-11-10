#pragma once
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>

namespace engine::core {

    class Profiler {
    public:
        static Profiler& get() {
            static Profiler instance;
            return instance;
        }

        void beginFrame();
        void endFrame();

        void beginSample(const std::string& name);
        void endSample(const std::string& name);

        void recordGpuTime(float ms);
        void recordMetric(const std::string& name, double value);

        nlohmann::json getStatsJson();

        class ScopedSample {
        public:
            explicit ScopedSample(const std::string& name)
                : m_name(name), m_start(std::chrono::steady_clock::now()) {}
            ~ScopedSample() noexcept {
                try {
                    const double elapsedMs =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_start).count();
                    Profiler::get().recordSampleDuration(m_name, elapsedMs);
                } catch (...) {
                    // Profiling must never terminate the engine if telemetry allocation fails.
                }
            }
            ScopedSample(const ScopedSample&) = delete;
            ScopedSample& operator=(const ScopedSample&) = delete;
            ScopedSample(ScopedSample&&) = delete;
            ScopedSample& operator=(ScopedSample&&) = delete;

        private:
            using Clock = std::chrono::steady_clock;
            std::string m_name;
            std::chrono::steady_clock::time_point m_start;
        };

    private:
        Profiler() = default;
        void recordSampleDuration(const std::string& name, double elapsedMs);

        using Clock = std::chrono::steady_clock;
        std::mutex m_mutex;
        Clock::time_point m_frameStartTime;
        float m_lastFrameTimeMs{16.6f};
        float m_avgFps{60.0f};
        float m_gpuTimeMs{0.0f};

        struct SampleData {
            Clock::time_point start;
            double elapsedMs{0.0};
            double rollingAvgMs{0.0};
        };

        std::unordered_map<std::string, SampleData> m_samples;
        std::unordered_map<std::string, double> m_customMetrics;
        uint64_t m_frameCounter{0};
    };

}

#define LUCID_PROFILE_SCOPE(name) engine::core::Profiler::ScopedSample _profilerSample##__LINE__(name)
