#pragma once
#include "ecs/Registry.hpp"
#include <glm/glm.hpp>
namespace engine::world {
    class WorldPartition {
    public:
        void loadCellsAround(const glm::vec3& position, float radius);
        void unloadDistantCells(const glm::vec3& position, float radius);
    };
}
