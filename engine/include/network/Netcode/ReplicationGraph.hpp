#pragma once
#include "ecs/Registry.hpp"
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
namespace engine::network {
    class ReplicationGraph {
    public:
        void prioritizeEntities(ecs::Registry& registry, const glm::vec3& observerPos);
        std::vector<uint8_t> deltaCompressState();
    };
}
