#include "core/Profiler.hpp"
#include <algorithm>

namespace engine::core {

    void Profiler::beginFrame() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frameStartTime = Clock::now();
        m_frameCounter++;
    }

    void Profiler::endFrame() {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto now = Clock::now();
        const double frameDurationMs =
            std::chrono::duration<double, std::milli>(now - m_frameStartTime).count();
        m_lastFrameTimeMs = static_cast<float>(frameDurationMs);

        const float currentFps = frameDurationMs > 0.001
            ? static_cast<float>(1000.0 / frameDurationMs)
            : 60.0f;
        m_avgFps = (m_avgFps * 0.95f) + (currentFps * 0.05f);
    }

    void Profiler::beginSample(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_samples[name].start = Clock::now();
    }

    void Profiler::endSample(const std::string& name) {
        const auto now = Clock::now();
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_samples.find(name);
        if (it == m_samples.end()) return;

        const double ms =
            std::chrono::duration<double, std::milli>(now - it->second.start).count();
        it->second.elapsedMs = ms;
        it->second.rollingAvgMs = (it->second.rollingAvgMs * 0.9) + (ms * 0.1);
    }

    void Profiler::recordSampleDuration(const std::string& name, double elapsedMs) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& sample = m_samples[name];
        sample.elapsedMs = elapsedMs;
        sample.rollingAvgMs = (sample.rollingAvgMs * 0.9) + (elapsedMs * 0.1);
    }

    void Profiler::recordGpuTime(float ms) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_gpuTimeMs = ms;
    }

    void Profiler::recordMetric(const std::string& name, double value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_customMetrics[name] = value;
    }

    nlohmann::json Profiler::getStatsJson() {
        std::lock_guard<std::mutex> lock(m_mutex);
        nlohmann::json json;
        json["frameCounter"] = m_frameCounter;
        json["frameTimeMs"] = m_lastFrameTimeMs;
        json["fps"] = m_avgFps;
        json["gpuTimeMs"] = m_gpuTimeMs;

        nlohmann::json samplesObj = nlohmann::json::object();
        for (const auto& [name, data] : m_samples) {
            samplesObj[name] = {
                {"lastMs", data.elapsedMs},
                {"avgMs", data.rollingAvgMs}
            };
        }
        json["samples"] = samplesObj;

        nlohmann::json metricsObj = nlohmann::json::object();
        for (const auto& [name, val] : m_customMetrics) {
            metricsObj[name] = val;
        }
        json["metrics"] = metricsObj;

        return json;
    }

}
