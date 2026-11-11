#include "network/Netcode/ReplicationGraph.hpp"
#include "ecs/Components.hpp"
#include "core/Logger.hpp"
#include <cmath>
#include <chrono>

namespace engine::network {

    ReplicationGraph::ReplicationGraph(size_t historyCapacity) : m_capacity(historyCapacity) {}

    void ReplicationGraph::recordSnapshot(uint32_t tick, ecs::Registry* registry) {
        if (!registry) return;

        WorldSnapshot snap;
        snap.tick = tick;
        snap.timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();

        auto& reg = registry->get();
        auto view = reg.view<ecs::TransformComponent>();

        for (auto entity : view) {
            const auto& t = view.get<ecs::TransformComponent>(entity);
            EntityNetworkState ens;
            ens.entityId = static_cast<uint32_t>(entity);
            ens.position = t.position;
            ens.orientation = t.orientation;

            if (reg.all_of<ecs::VelocityComponent>(entity)) {
                ens.velocity = reg.get<ecs::VelocityComponent>(entity).velocity;
            }
            snap.entities.push_back(ens);
        }

        m_snapshotHistory.push_back(std::move(snap));
        if (m_snapshotHistory.size() > m_capacity) {
            m_snapshotHistory.pop_front();
        }
    }

    const WorldSnapshot* ReplicationGraph::getSnapshot(uint32_t tick) const {
        for (const auto& snap : m_snapshotHistory) {
            if (snap.tick == tick) return &snap;
        }
        return nullptr;
    }

    void ReplicationGraph::queueSubTickInput(const SubTickInput& input) {
        m_inputQueue.push_back(input);
        if (m_inputQueue.size() > m_capacity * 2) {
            m_inputQueue.pop_front();
        }
    }

    bool ReplicationGraph::verifyAndRollback(uint32_t authoritativeTick, const WorldSnapshot& serverSnapshot, ecs::Registry* registry, float tolerance) {
        if (!registry) return false;
        const WorldSnapshot* localSnap = getSnapshot(authoritativeTick);
        if (!localSnap) return false;

        bool mispredicted = false;

        // Compare positions between predicted local snapshot and authoritative server snapshot
        for (const auto& serverEnt : serverSnapshot.entities) {
            for (const auto& localEnt : localSnap->entities) {
                if (serverEnt.entityId == localEnt.entityId) {
                    float dist = glm::distance(serverEnt.position, localEnt.position);
                    if (dist > tolerance) {
                        mispredicted = true;
                        break;
                    }
                }
            }
            if (mispredicted) break;
        }

        if (mispredicted) {
            // Roll back state to authoritative server snapshot
            auto& reg = registry->get();
            for (const auto& serverEnt : serverSnapshot.entities) {
                auto e = static_cast<entt::entity>(serverEnt.entityId);
                if (reg.valid(e) && reg.all_of<ecs::TransformComponent>(e)) {
                    auto& t = reg.get<ecs::TransformComponent>(e);
                    t.position = serverEnt.position;
                    t.orientation = serverEnt.orientation;
                    if (reg.all_of<ecs::VelocityComponent>(e)) {
                        reg.get<ecs::VelocityComponent>(e).velocity = serverEnt.velocity;
                    }
                }
            }

            core::Logger::warn("SubTick Netcode: Prediction error at tick " + std::to_string(authoritativeTick) + 
                               ". Rolled back to authoritative state.");
            return true;
        }

        return false;
    }

    nlohmann::json ReplicationGraph::generateDeltaJson(uint32_t currentTick, uint32_t baselineTick) const {
        const WorldSnapshot* cur = getSnapshot(currentTick);
        const WorldSnapshot* base = getSnapshot(baselineTick);

        nlohmann::json delta = nlohmann::json::object();
        delta["tick"] = currentTick;
        delta["baseTick"] = baselineTick;

        nlohmann::json changedEntities = nlohmann::json::array();
        if (!cur) return delta;

        for (const auto& ent : cur->entities) {
            bool changed = true;
            if (base) {
                for (const auto& baseEnt : base->entities) {
                    if (baseEnt.entityId == ent.entityId) {
                        if (glm::distance(baseEnt.position, ent.position) < 0.001f &&
                            glm::distance(baseEnt.velocity, ent.velocity) < 0.001f) {
                            changed = false;
                        }
                        break;
                    }
                }
            }

            if (changed) {
                changedEntities.push_back({
                    {"id", ent.entityId},
                    {"pos", {ent.position.x, ent.position.y, ent.position.z}},
                    {"vel", {ent.velocity.x, ent.velocity.y, ent.velocity.z}}
                });
            }
        }

        delta["changed"] = changedEntities;
        return delta;
    }

    void ReplicationGraph::prioritizeEntities(ecs::Registry&, const glm::vec3&) {
        // Spatial importance sorting for network bandwidth optimization
    }

}
