#include "core/SystemScheduler.hpp"
#include <algorithm>
#include <queue>

namespace engine::core {

    SystemScheduler::SystemScheduler(size_t threadCount) {
        if (threadCount == 0) {
            unsigned int hw = std::thread::hardware_concurrency();
            m_threadCount = hw > 0 ? static_cast<size_t>(hw) : 4;
        } else {
            m_threadCount = threadCount;
        }

        m_workers.reserve(m_threadCount);
        for (size_t i = 0; i < m_threadCount; ++i) {
            m_workers.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(m_queueMutex);
                        m_cvTask.wait(lock, [this]() {
                            return m_stop.load() || !m_taskQueue.empty();
                        });
                        if (m_stop.load() && m_taskQueue.empty()) {
                            return;
                        }
                        task = std::move(m_taskQueue.back());
                        m_taskQueue.pop_back();
                    }
                    task();
                    if (--m_activeTasks == 0) {
                        m_cvDone.notify_all();
                    }
                }
            });
        }
    }

    SystemScheduler::~SystemScheduler() {
        m_stop.store(true);
        m_cvTask.notify_all();
        m_workers.clear();
    }

    void SystemScheduler::register_system(SystemDesc desc) {
        m_systems.push_back(std::move(desc));
        m_compiled = false;
    }

    bool SystemScheduler::_has_conflict(const SystemDesc& a, const SystemDesc& b) const {
        if (!a.allow_parallel || !b.allow_parallel) return true;

        for (const auto& accA : a.component_access) {
            for (const auto& accB : b.component_access) {
                if (accA.type == accB.type) {
                    if (accA.mode == AccessMode::Write || accB.mode == AccessMode::Write) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool SystemScheduler::build_graph() {
        m_stages.clear();
        const size_t N = m_systems.size();
        if (N == 0) {
            m_compiled = true;
            return true;
        }

        std::unordered_map<std::string, size_t> nameToIndex;
        for (size_t i = 0; i < N; ++i) {
            if (!m_systems[i].name.empty()) {
                nameToIndex[m_systems[i].name] = i;
            }
        }

        std::vector<std::unordered_set<size_t>> edges(N);
        std::vector<size_t> inDegree(N, 0);

        auto addEdge = [&](size_t from, size_t to) {
            if (from == to) return;
            if (edges[from].insert(to).second) {
                inDegree[to]++;
            }
        };

        // Explicit dependencies
        for (size_t i = 0; i < N; ++i) {
            for (const auto& depName : m_systems[i].explicit_after) {
                auto it = nameToIndex.find(depName);
                if (it != nameToIndex.end()) {
                    addEdge(it->second, i);
                }
            }
        }

        // Auto-detected read/write hazards
        for (size_t i = 0; i < N; ++i) {
            for (size_t j = i + 1; j < N; ++j) {
                if (_has_conflict(m_systems[i], m_systems[j])) {
                    addEdge(i, j);
                }
            }
        }

        // Compute parallel stages using level-based topological ordering
        std::vector<size_t> remainingInDegree = inDegree;
        std::vector<bool> scheduled(N, false);
        size_t scheduledCount = 0;

        while (scheduledCount < N) {
            std::vector<size_t> currentStage;
            for (size_t i = 0; i < N; ++i) {
                if (!scheduled[i] && remainingInDegree[i] == 0) {
                    currentStage.push_back(i);
                }
            }

            if (currentStage.empty()) {
                // Dependency cycle detected, fallback to remaining in declaration order
                for (size_t i = 0; i < N; ++i) {
                    if (!scheduled[i]) {
                        currentStage.push_back(i);
                        break;
                    }
                }
            }

            for (size_t idx : currentStage) {
                scheduled[idx] = true;
                scheduledCount++;
                for (size_t dependent : edges[idx]) {
                    if (remainingInDegree[dependent] > 0) {
                        remainingInDegree[dependent]--;
                    }
                }
            }

            m_stages.push_back(std::move(currentStage));
        }

        m_compiled = true;
        return true;
    }

    void SystemScheduler::execute(float dt) {
        if (!m_compiled) {
            build_graph();
        }

        for (const auto& stage : m_stages) {
            if (m_singleThreaded || stage.size() <= 1) {
                for (size_t idx : stage) {
                    const auto& sys = m_systems[idx];
                    auto start = std::chrono::high_resolution_clock::now();
                    sys.execute(dt);
                    auto end = std::chrono::high_resolution_clock::now();
                    double ms = std::chrono::duration<double, std::milli>(end - start).count();

                    std::lock_guard<std::mutex> lock(m_timingsMutex);
                    m_timings[sys.name] = ms;
                }
            } else {
                // Dispatch stage tasks concurrently
                m_activeTasks.store(stage.size());
                {
                    std::unique_lock<std::mutex> lock(m_queueMutex);
                    for (size_t idx : stage) {
                        m_taskQueue.push_back([this, idx, dt]() {
                            const auto& sys = m_systems[idx];
                            auto start = std::chrono::high_resolution_clock::now();
                            sys.execute(dt);
                            auto end = std::chrono::high_resolution_clock::now();
                            double ms = std::chrono::duration<double, std::milli>(end - start).count();

                            std::lock_guard<std::mutex> lock(m_timingsMutex);
                            m_timings[sys.name] = ms;
                        });
                    }
                }
                m_cvTask.notify_all();

                // Wait for all systems in current stage to finish
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_cvDone.wait(lock, [this]() {
                    return m_activeTasks.load() == 0;
                });
            }
        }
    }

    std::vector<SystemTiming> SystemScheduler::get_system_timings() const {
        std::lock_guard<std::mutex> lock(m_timingsMutex);
        std::vector<SystemTiming> result;
        result.reserve(m_timings.size());
        for (const auto& [name, duration] : m_timings) {
            result.push_back({name, duration});
        }
        return result;
    }

    void SystemScheduler::clear() {
        m_stages.clear();
        m_systems.clear();
        m_compiled = false;
        std::lock_guard<std::mutex> lock(m_timingsMutex);
        m_timings.clear();
    }

} // namespace engine::core
