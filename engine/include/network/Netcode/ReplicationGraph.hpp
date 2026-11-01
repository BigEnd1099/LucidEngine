#pragma once
#include "ecs/Registry.hpp"
#include <vector>
#include <cstdint>
#include <deque>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

namespace engine::network {

    struct SubTickInput {
        uint32_t tick{0};
        uint64_t microsecondTimestamp{0};
        uint32_t buttonMask{0}; // Bit 0=W, 1=A, 2=S, 3=D, 4=Jump, 5=Fire
        float lookYaw{0.0f};
        float lookPitch{0.0f};
    };

    struct EntityNetworkState {
        uint32_t entityId{0};
        glm::vec3 position{0.0f};
        glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 velocity{0.0f};
    };

    struct WorldSnapshot {
        uint32_t tick{0};
        uint64_t timestampUs{0};
        std::vector<EntityNetworkState> entities;
    };

    class ReplicationGraph {
    public:
        explicit ReplicationGraph(size_t historyCapacity = 128);

        void recordSnapshot(uint32_t tick, ecs::Registry* registry);
        const WorldSnapshot* getSnapshot(uint32_t tick) const;

        void queueSubTickInput(const SubTickInput& input);
        const std::deque<SubTickInput>& getPendingInputs() const { return m_inputQueue; }

        // [Source 2 / CS2 Sub-Tick Prediction & Rollback Verification]
        bool verifyAndRollback(uint32_t authoritativeTick, const WorldSnapshot& serverSnapshot, ecs::Registry* registry, float tolerance = 0.05f);

        // Delta Compression: compares current state against baseline tick
        nlohmann::json generateDeltaJson(uint32_t currentTick, uint32_t baselineTick) const;

        void prioritizeEntities(ecs::Registry& registry, const glm::vec3& observerPos);

    private:
        size_t m_capacity{128};
        std::deque<WorldSnapshot> m_snapshotHistory;
        std::deque<SubTickInput> m_inputQueue;
    };

}
