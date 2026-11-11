#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
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
            std::string m_name;
            std::chrono::steady_clock::time_point m_start;
        };

    private:
        Profiler() = default;
        void recordSampleDuration(const std::string& name, double elapsedMs);

        using Clock = std::chrono::steady_clock;
        static constexpr size_t FRAME_HISTORY_CAPACITY = 180;

        std::mutex m_mutex;
        Clock::time_point m_frameStartTime{};
        bool m_frameActive{false};
        float m_lastFrameTimeMs{16.6f};
        float m_avgFps{0.0f};
        float m_gpuTimeMs{0.0f};
        double m_totalFrameTimeMs{0.0};
        uint64_t m_frameTimeSampleCount{0};
        std::array<float, FRAME_HISTORY_CAPACITY> m_frameTimeHistory{};
        size_t m_frameHistoryCount{0};
        size_t m_frameHistoryWriteIndex{0};

        struct SampleData {
            Clock::time_point start{};
            bool active{false};
            double elapsedMs{0.0};
            double totalMs{0.0};
            double rollingAvgMs{0.0};
            double minMs{0.0};
            double maxMs{0.0};
            uint64_t sampleCount{0};
        };

        std::unordered_map<std::string, SampleData> m_samples;
        std::unordered_map<std::string, double> m_customMetrics;
        uint64_t m_frameCounter{0};
    };

}

#define LUCID_DETAIL_CONCAT_INNER(a, b) a##b
#define LUCID_DETAIL_CONCAT(a, b) LUCID_DETAIL_CONCAT_INNER(a, b)
#define LUCID_PROFILE_SCOPE(name) \
    ::engine::core::Profiler::ScopedSample LUCID_DETAIL_CONCAT(_profilerSample_, __LINE__)(name)
