#include "core/Profiler.hpp"
#include <algorithm>
#include <cmath>

namespace engine::core {

    void Profiler::beginFrame() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frameStartTime = Clock::now();
        m_frameActive = true;
        ++m_frameCounter;
    }

    void Profiler::endFrame() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_frameActive) return;

        const auto now = Clock::now();
        const double frameDurationMs =
            std::chrono::duration<double, std::milli>(now - m_frameStartTime).count();
        m_frameActive = false;
        if (!std::isfinite(frameDurationMs) || frameDurationMs <= 0.0) return;

        m_lastFrameTimeMs = static_cast<float>(frameDurationMs);
        m_totalFrameTimeMs += frameDurationMs;
        ++m_frameTimeSampleCount;

        m_frameTimeHistory[m_frameHistoryWriteIndex] = m_lastFrameTimeMs;
        m_frameHistoryWriteIndex = (m_frameHistoryWriteIndex + 1) % FRAME_HISTORY_CAPACITY;
        m_frameHistoryCount = std::min(m_frameHistoryCount + 1, FRAME_HISTORY_CAPACITY);

        const float currentFps = static_cast<float>(1000.0 / frameDurationMs);
        // Seed the EMA from the first real frame instead of biasing it toward an assumed 60 FPS.
        m_avgFps = (m_frameTimeSampleCount == 1)
            ? currentFps
            : (m_avgFps * 0.95f) + (currentFps * 0.05f);
    }

    void Profiler::beginSample(const std::string& name) {
        if (name.empty()) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& sample = m_samples[name];
        sample.start = Clock::now();
        sample.active = true;
    }

    void Profiler::endSample(const std::string& name) {
        const auto now = Clock::now();
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_samples.find(name);
        if (it == m_samples.end() || !it->second.active) return;

        auto& sample = it->second;
        const double ms = std::chrono::duration<double, std::milli>(now - sample.start).count();
        sample.active = false;
        if (!std::isfinite(ms) || ms < 0.0) return;

        sample.elapsedMs = ms;
        sample.totalMs += ms;
        ++sample.sampleCount;
        sample.minMs = (sample.sampleCount == 1) ? ms : std::min(sample.minMs, ms);
        sample.maxMs = std::max(sample.maxMs, ms);
        sample.rollingAvgMs = (sample.sampleCount == 1)
            ? ms
            : (sample.rollingAvgMs * 0.9) + (ms * 0.1);
    }

    void Profiler::recordSampleDuration(const std::string& name, double elapsedMs) {
        if (name.empty() || !std::isfinite(elapsedMs) || elapsedMs < 0.0) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& sample = m_samples[name];
        sample.elapsedMs = elapsedMs;
        sample.totalMs += elapsedMs;
        ++sample.sampleCount;
        sample.minMs = (sample.sampleCount == 1) ? elapsedMs : std::min(sample.minMs, elapsedMs);
        sample.maxMs = std::max(sample.maxMs, elapsedMs);
        sample.rollingAvgMs = (sample.sampleCount == 1)
            ? elapsedMs
            : (sample.rollingAvgMs * 0.9) + (elapsedMs * 0.1);
    }

    void Profiler::recordGpuTime(float ms) {
        if (!std::isfinite(ms) || ms < 0.0f) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_gpuTimeMs = ms;
    }

    void Profiler::recordMetric(const std::string& name, double value) {
        if (name.empty() || !std::isfinite(value)) return;
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
        json["frameTimeAvgMs"] = m_frameTimeSampleCount > 0
            ? (m_totalFrameTimeMs / static_cast<double>(m_frameTimeSampleCount))
            : 0.0;

        std::vector<float> frameTimes;
        frameTimes.reserve(m_frameHistoryCount);
        for (size_t i = 0; i < m_frameHistoryCount; ++i) {
            frameTimes.push_back(m_frameTimeHistory[i]);
        }
        if (!frameTimes.empty()) {
            std::sort(frameTimes.begin(), frameTimes.end());
            const size_t p95Index = static_cast<size_t>(
                std::ceil(0.95 * static_cast<double>(frameTimes.size()))) - 1;
            json["frameTimeP95Ms"] = frameTimes[std::min(p95Index, frameTimes.size() - 1)];
            json["frameTimeHistorySamples"] = frameTimes.size();
        } else {
            json["frameTimeP95Ms"] = 0.0;
            json["frameTimeHistorySamples"] = 0;
        }

        nlohmann::json samplesObj = nlohmann::json::object();
        for (const auto& [name, data] : m_samples) {
            const double average = data.sampleCount > 0
                ? data.totalMs / static_cast<double>(data.sampleCount)
                : 0.0;
            samplesObj[name] = {
                {"lastMs", data.elapsedMs},
                {"avgMs", average},
                {"rollingAvgMs", data.rollingAvgMs},
                {"minMs", data.sampleCount > 0 ? data.minMs : 0.0},
                {"maxMs", data.maxMs},
                {"sampleCount", data.sampleCount}
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
