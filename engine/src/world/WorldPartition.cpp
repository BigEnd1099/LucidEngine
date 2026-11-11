#include "world/WorldPartition.hpp"
#include "ecs/Components.hpp"
#include "physics/PhysicsContext.hpp"
#include "core/Logger.hpp"
#include <cmath>
#include <algorithm>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>

namespace engine::world {

    WorldPartition::WorldPartition(float cellSize, float streamRadius)
        : m_cellSize(cellSize), m_streamRadius(streamRadius) {}

    CellCoord WorldPartition::toCellCoord(const glm::vec3& pos) const {
        return CellCoord{
            static_cast<int>(std::floor(pos.x / m_cellSize)),
            static_cast<int>(std::floor(pos.y / m_cellSize)),
            static_cast<int>(std::floor(pos.z / m_cellSize))
        };
    }

    void WorldPartition::registerEntity(entt::entity e, const glm::vec3& pos) {
        CellCoord coord = toCellCoord(pos);
        m_entityToCell[static_cast<uint32_t>(e)] = coord;
        auto& cell = m_cells[coord];
        cell.coord = coord;
        cell.entities.push_back(e);
    }

    void WorldPartition::unregisterEntity(entt::entity e) {
        uint32_t id = static_cast<uint32_t>(e);
        auto it = m_entityToCell.find(id);
        if (it != m_entityToCell.end()) {
            auto cellIt = m_cells.find(it->second);
            if (cellIt != m_cells.end()) {
                auto& ents = cellIt->second.entities;
                ents.erase(std::remove(ents.begin(), ents.end(), e), ents.end());
            }
            m_entityToCell.erase(it);
        }
    }

    size_t WorldPartition::getLoadedCellCount() const {
        size_t count = 0;
        for (const auto& [coord, cell] : m_cells) {
            if (cell.isLoaded) count++;
        }
        return count;
    }

    void WorldPartition::update(ecs::Registry* registry, physics::PhysicsContext* physics, const glm::vec3& cameraPos, float) {
        if (!registry) return;

        CellCoord camCell = toCellCoord(cameraPos);

        for (auto& [coord, cell] : m_cells) {
            float dx = (coord.x - camCell.x) * m_cellSize;
            float dy = (coord.y - camCell.y) * m_cellSize;
            float dz = (coord.z - camCell.z) * m_cellSize;
            float distSq = dx * dx + dy * dy + dz * dz;

            bool inRange = distSq <= (m_streamRadius * m_streamRadius);

            if (inRange && !cell.isLoaded) {
                cell.isLoaded = true;
                cell.lastActiveTime = 0.0f;
                // Activate entities in cell
                for (auto ent : cell.entities) {
                    if (registry->get().valid(ent) && registry->get().all_of<ecs::RigidBodyComponent>(ent)) {
                        auto& rb = registry->get().get<ecs::RigidBodyComponent>(ent);
                        if (physics && rb.bodyId != 0xFFFFFFFF) {
                            JPH::BodyID bId(rb.bodyId);
                            physics->getBodyInterface().ActivateBody(bId);
                        }
                    }
                }
            } else if (!inRange && cell.isLoaded) {
                cell.isLoaded = false;
                // Deactivate entities to conserve CPU/GPU
                for (auto ent : cell.entities) {
                    if (registry->get().valid(ent) && registry->get().all_of<ecs::RigidBodyComponent>(ent)) {
                        auto& rb = registry->get().get<ecs::RigidBodyComponent>(ent);
                        if (physics && rb.bodyId != 0xFFFFFFFF) {
                            JPH::BodyID bId(rb.bodyId);
                            physics->getBodyInterface().DeactivateBody(bId);
                        }
                    }
                }
            }
        }
    }

    bool WorldPartition::checkAndRebaseOrigin(ecs::Registry* registry, physics::PhysicsContext* physics, glm::vec3& cameraPos, float maxThreshold) {
        if (!registry) return false;
        float distFromOrigin = glm::length(cameraPos);
        if (distFromOrigin < maxThreshold) return false;

        glm::vec3 shiftOffset = cameraPos;
        m_worldOriginDouble += glm::dvec3(shiftOffset.x, shiftOffset.y, shiftOffset.z);
        cameraPos -= shiftOffset;

        auto& reg = registry->get();
        auto view = reg.view<ecs::TransformComponent>();

        for (auto entity : view) {
            auto& t = view.get<ecs::TransformComponent>(entity);
            t.position -= shiftOffset;

            if (physics && reg.all_of<ecs::RigidBodyComponent>(entity)) {
                auto& rb = reg.get<ecs::RigidBodyComponent>(entity);
                if (rb.bodyId != 0xFFFFFFFF) {
                    JPH::BodyID bId(rb.bodyId);
                    JPH::RVec3 curPos = physics->getBodyInterface().GetPosition(bId);
                    physics->getBodyInterface().SetPosition(bId, JPH::RVec3(curPos.GetX() - shiftOffset.x, curPos.GetY() - shiftOffset.y, curPos.GetZ() - shiftOffset.z), JPH::EActivation::DontActivate);
                }
            }
        }

        // Re-bucket cells after origin shift
        std::unordered_map<CellCoord, WorldCell, CellCoordHash> newCells;
        std::unordered_map<uint32_t, CellCoord> newEntityToCell;

        for (auto entity : view) {
            const auto& t = view.get<ecs::TransformComponent>(entity);
            CellCoord coord = toCellCoord(t.position);
            newEntityToCell[static_cast<uint32_t>(entity)] = coord;
            auto& cell = newCells[coord];
            cell.coord = coord;
            cell.entities.push_back(entity);
            cell.isLoaded = true;
        }

        m_cells = std::move(newCells);
        m_entityToCell = std::move(newEntityToCell);

        core::Logger::info("WorldPartition: Rebased origin by (" + std::to_string(shiftOffset.x) + ", " + 
                           std::to_string(shiftOffset.y) + ", " + std::to_string(shiftOffset.z) + "). Total 64-bit Origin: (" +
                           std::to_string(m_worldOriginDouble.x) + ", " + std::to_string(m_worldOriginDouble.y) + ", " +
                           std::to_string(m_worldOriginDouble.z) + ")");
        return true;
    }

}
