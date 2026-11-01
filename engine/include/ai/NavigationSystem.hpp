#pragma once
#include "ecs/Registry.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>

namespace engine::ai {

    struct NavGridCell {
        bool walkable{true};
        float cost{1.0f};
        float elevation{0.0f};
    };

    class NavigationSystem {
    public:
        NavigationSystem(float cellSize = 1.0f, int gridWidth = 100, int gridDepth = 100);

        void buildFromScene(ecs::Registry* registry);
        void update(ecs::Registry* registry, float dt);

        std::vector<glm::vec3> findPath(const glm::vec3& start, const glm::vec3& target);
        void setObstacle(const glm::vec3& worldPos, bool blocked);

    private:
        int toGridX(float worldX) const;
        int toGridZ(float worldZ) const;
        glm::vec3 toWorldPos(int gridX, int gridZ, float elevation = 0.0f) const;

        float m_cellSize{1.0f};
        int m_gridWidth{100};
        int m_gridDepth{100};
        std::vector<NavGridCell> m_grid;
    };

}
