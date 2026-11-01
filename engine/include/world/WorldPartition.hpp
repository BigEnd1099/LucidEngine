#pragma once
#include "ecs/Registry.hpp"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace engine::physics {
    class PhysicsContext;
}

namespace engine::world {

    struct CellCoord {
        int x{0};
        int y{0};
        int z{0};

        bool operator==(const CellCoord& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct CellCoordHash {
        std::size_t operator()(const CellCoord& c) const {
            return std::hash<int>()(c.x) ^ (std::hash<int>()(c.y) << 1) ^ (std::hash<int>()(c.z) << 2);
        }
    };

    struct WorldCell {
        CellCoord coord;
        std::vector<entt::entity> entities;
        bool isLoaded{false};
        float lastActiveTime{0.0f};
    };

    class WorldPartition {
    public:
        explicit WorldPartition(float cellSize = 64.0f, float streamRadius = 128.0f);

        void update(ecs::Registry* registry, physics::PhysicsContext* physics, const glm::vec3& cameraPos, float dt);

        void registerEntity(entt::entity e, const glm::vec3& pos);
        void unregisterEntity(entt::entity e);

        // [Unigine-style 64-bit Precision Floating Origin]
        bool checkAndRebaseOrigin(ecs::Registry* registry, physics::PhysicsContext* physics, glm::vec3& cameraPos, float maxThreshold = 2000.0f);
        glm::dvec3 getWorldOriginDouble() const { return m_worldOriginDouble; }

        size_t getLoadedCellCount() const;
        size_t getTotalCellCount() const { return m_cells.size(); }

        CellCoord toCellCoord(const glm::vec3& pos) const;

    private:
        float m_cellSize{64.0f};
        float m_streamRadius{128.0f};
        glm::dvec3 m_worldOriginDouble{0.0, 0.0, 0.0};

        std::unordered_map<CellCoord, WorldCell, CellCoordHash> m_cells;
        std::unordered_map<uint32_t, CellCoord> m_entityToCell;
    };

}
