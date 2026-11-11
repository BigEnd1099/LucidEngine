#include "renderer/RenderGraph.hpp"
#include "core/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace engine::renderer {

    void RenderGraph::addPass(
        const std::string& name,
        const std::vector<std::string>& inputs,
        const std::vector<std::string>& outputs,
        std::function<void(VkCommandBuffer)> callback,
        const std::vector<std::string>& dependsOn) {
        m_passes.push_back({ name, inputs, outputs, std::move(callback), dependsOn });
        m_isCompiled = false;
        m_executionOrder.clear();
        m_lastError.clear();
    }

    void RenderGraph::clear() {
        m_passes.clear();
        m_executionOrder.clear();
        m_lastError.clear();
        m_isCompiled = false;
    }

    bool RenderGraph::compile() {
        m_isCompiled = false;
        m_executionOrder.clear();
        m_lastError.clear();

        const size_t passCount = m_passes.size();
        std::unordered_map<std::string, size_t> passByName;
        passByName.reserve(passCount);

        for (size_t i = 0; i < passCount; ++i) {
            const auto& pass = m_passes[i];
            if (pass.name.empty()) {
                m_lastError = "RenderGraph contains a pass with an empty name.";
                return false;
            }
            if (!pass.executeCallback) {
                m_lastError = "RenderGraph pass '" + pass.name + "' has no execute callback.";
                return false;
            }
            if (!passByName.emplace(pass.name, i).second) {
                m_lastError = "RenderGraph contains duplicate pass name '" + pass.name + "'.";
                return false;
            }
        }

        std::vector<std::unordered_set<size_t>> edges(passCount);
        std::vector<size_t> indegree(passCount, 0);
        auto addEdge = [&](size_t from, size_t to) {
            if (from == to) return;
            if (edges[from].insert(to).second) ++indegree[to];
        };

        // Explicit pass dependencies allow independent passes to be scheduled
        // independently of declaration order.
        for (size_t i = 0; i < passCount; ++i) {
            for (const auto& dependency : m_passes[i].dependsOn) {
                const auto it = passByName.find(dependency);
                if (it == passByName.end()) {
                    m_lastError = "RenderGraph pass '" + m_passes[i].name +
                        "' depends on unknown pass '" + dependency + "'.";
                    return false;
                }
                if (it->second == i) {
                    m_lastError = "RenderGraph pass '" + m_passes[i].name +
                        "' cannot depend on itself.";
                    return false;
                }
                addEdge(it->second, i);
            }
        }

        // Resolve explicit constraints first. Resource hazards are inferred in this
        // valid order so an explicitly-declared producer can appear after its consumer
        // in source code without being misclassified as an imported resource.
        auto stableTopologicalOrder = [&](const std::vector<size_t>& sourceIndegree) {
            std::vector<size_t> indegrees = sourceIndegree;
            std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> queue;
            std::vector<size_t> order;
            order.reserve(passCount);
            for (size_t i = 0; i < passCount; ++i) {
                if (indegrees[i] == 0) queue.push(i);
            }
            while (!queue.empty()) {
                const size_t current = queue.top();
                queue.pop();
                order.push_back(current);
                for (const size_t next : edges[current]) {
                    if (--indegrees[next] == 0) queue.push(next);
                }
            }
            return order;
        };

        const std::vector<size_t> explicitOrder = stableTopologicalOrder(indegree);
        if (explicitOrder.size() != passCount) {
            m_lastError = "RenderGraph explicit pass dependency cycle detected.";
            return false;
        }

        struct ResourceState {
            bool hasWriter{false};
            size_t lastWriter{0};
            std::vector<size_t> readers;
        };
        std::unordered_map<std::string, ResourceState> resources;

        // Infer RAW, WAW and WAR hazards in the explicit dependency order.
        // Unwritten inputs are treated as externally-owned/imported resources.
        for (const size_t i : explicitOrder) {
            const auto& pass = m_passes[i];
            std::unordered_set<std::string> uniqueInputs;
            for (const auto& resource : pass.inputs) {
                if (resource.empty()) {
                    m_lastError = "RenderGraph pass '" + pass.name + "' declares an empty input resource.";
                    return false;
                }
                if (!uniqueInputs.insert(resource).second) continue;

                auto& state = resources[resource];
                if (state.hasWriter) addEdge(state.lastWriter, i);
                state.readers.push_back(i);
            }

            std::unordered_set<std::string> uniqueOutputs;
            for (const auto& resource : pass.outputs) {
                if (resource.empty()) {
                    m_lastError = "RenderGraph pass '" + pass.name + "' declares an empty output resource.";
                    return false;
                }
                if (!uniqueOutputs.insert(resource).second) continue;

                auto& state = resources[resource];
                if (state.hasWriter) addEdge(state.lastWriter, i);
                for (const size_t reader : state.readers) addEdge(reader, i);
                state.readers.clear();
                state.hasWriter = true;
                state.lastWriter = i;
            }
        }

        // Stable topological sort: when multiple passes are ready, preserve their
        // original declaration order to keep captures and editor behavior deterministic.
        std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> ready;
        for (size_t i = 0; i < passCount; ++i) {
            if (indegree[i] == 0) ready.push(i);
        }

        m_executionOrder.reserve(passCount);
        while (!ready.empty()) {
            const size_t current = ready.top();
            ready.pop();
            m_executionOrder.push_back(current);

            for (const size_t next : edges[current]) {
                if (--indegree[next] == 0) ready.push(next);
            }
        }

        if (m_executionOrder.size() != passCount) {
            m_executionOrder.clear();
            m_lastError = "RenderGraph dependency cycle detected.";
            return false;
        }

        m_isCompiled = true;
        core::Logger::info("RenderGraph compiled " + std::to_string(passCount) +
            " passes with " + std::to_string(resources.size()) + " tracked resources.");
        return true;
    }

    void RenderGraph::execute(VkCommandBuffer cmd) {
        if (!m_isCompiled && !compile()) {
            throw std::runtime_error("RenderGraph compilation failed: " + m_lastError);
        }

        m_passTelemetry.clear();
        m_passTelemetry.reserve(m_executionOrder.size());

        for (const size_t index : m_executionOrder) {
            const auto& pass = m_passes[index];
            const auto start = std::chrono::high_resolution_clock::now();
            pass.executeCallback(cmd);
            const auto end = std::chrono::high_resolution_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(end - start).count();

            m_passTelemetry.push_back({
                pass.name,
                ms,
                1 // Recorded dispatch item
            });
        }
    }

    nlohmann::json RenderGraph::getPassTelemetryJson() const {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& item : m_passTelemetry) {
            arr.push_back({
                {"pass", item.passName},
                {"durationMs", item.durationMs},
                {"drawCount", item.drawCount}
            });
        }
        return arr;
    }
}
