#include "ai/NavigationSystem.hpp"
#include "ecs/Components.hpp"
#include <queue>
#include <cmath>
#include <algorithm>

namespace engine::ai {

    struct AStarNode {
        int x{0};
        int z{0};
        float gCost{0.0f};
        float hCost{0.0f};
        float fCost() const { return gCost + hCost; }
        int parentX{-1};
        int parentZ{-1};
    };

    struct CompareAStarNode {
        bool operator()(const AStarNode* a, const AStarNode* b) const {
            return a->fCost() > b->fCost();
        }
    };

    NavigationSystem::NavigationSystem(float cellSize, int gridWidth, int gridDepth)
        : m_cellSize(cellSize), m_gridWidth(gridWidth), m_gridDepth(gridDepth) {
        m_grid.resize(gridWidth * gridDepth);
    }

    int NavigationSystem::toGridX(float worldX) const {
        int x = static_cast<int>(std::floor((worldX + (m_gridWidth * m_cellSize * 0.5f)) / m_cellSize));
        return std::clamp(x, 0, m_gridWidth - 1);
    }

    int NavigationSystem::toGridZ(float worldZ) const {
        int z = static_cast<int>(std::floor((worldZ + (m_gridDepth * m_cellSize * 0.5f)) / m_cellSize));
        return std::clamp(z, 0, m_gridDepth - 1);
    }

    glm::vec3 NavigationSystem::toWorldPos(int gridX, int gridZ, float elevation) const {
        float x = (static_cast<float>(gridX) + 0.5f) * m_cellSize - (m_gridWidth * m_cellSize * 0.5f);
        float z = (static_cast<float>(gridZ) + 0.5f) * m_cellSize - (m_gridDepth * m_cellSize * 0.5f);
        return glm::vec3(x, elevation, z);
    }

    void NavigationSystem::setObstacle(const glm::vec3& worldPos, bool blocked) {
        int gx = toGridX(worldPos.x);
        int gz = toGridZ(worldPos.z);
        m_grid[gz * m_gridWidth + gx].walkable = !blocked;
    }

    void NavigationSystem::buildFromScene(ecs::Registry* registry) {
        if (!registry) return;
        // Reset grid to walkable
        for (auto& cell : m_grid) {
            cell.walkable = true;
            cell.cost = 1.0f;
            cell.elevation = 0.0f;
        }

        auto& reg = registry->get();
        auto view = reg.view<ecs::TransformComponent, ecs::RigidBodyComponent>();
        for (auto entity : view) {
            const auto& t = view.get<ecs::TransformComponent>(entity);
            const auto& rb = view.get<ecs::RigidBodyComponent>(entity);

            // Static or heavy immovable obstacles mark cells as non-walkable
            if (rb.mass <= 0.0f && !rb.isSensor) {
                float halfX = rb.halfExtents.x * t.scale.x;
                float halfZ = rb.halfExtents.z * t.scale.z;

                int minX = toGridX(t.position.x - halfX);
                int maxX = toGridX(t.position.x + halfX);
                int minZ = toGridZ(t.position.z - halfZ);
                int maxZ = toGridZ(t.position.z + halfZ);

                for (int z = minZ; z <= maxZ; ++z) {
                    for (int x = minX; x <= maxX; ++x) {
                        m_grid[z * m_gridWidth + x].walkable = false;
                    }
                }
            }
        }
    }

    std::vector<glm::vec3> NavigationSystem::findPath(const glm::vec3& start, const glm::vec3& target) {
        int startX = toGridX(start.x);
        int startZ = toGridZ(start.z);
        int targetX = toGridX(target.x);
        int targetZ = toGridZ(target.z);

        if (startX == targetX && startZ == targetZ) {
            return { target };
        }

        std::vector<std::vector<AStarNode>> nodeGrid(m_gridWidth, std::vector<AStarNode>(m_gridDepth));
        std::vector<std::vector<bool>> closedSet(m_gridWidth, std::vector<bool>(m_gridDepth, false));
        std::priority_queue<AStarNode*, std::vector<AStarNode*>, CompareAStarNode> openSet;

        for (int x = 0; x < m_gridWidth; ++x) {
            for (int z = 0; z < m_gridDepth; ++z) {
                nodeGrid[x][z].x = x;
                nodeGrid[x][z].z = z;
                nodeGrid[x][z].gCost = 1e9f;
                nodeGrid[x][z].hCost = 0.0f;
            }
        }

        nodeGrid[startX][startZ].gCost = 0.0f;
        nodeGrid[startX][startZ].hCost = std::hypot(static_cast<float>(targetX - startX), static_cast<float>(targetZ - startZ));
        openSet.push(&nodeGrid[startX][startZ]);

        static const int dx[] = { 1, -1, 0, 0, 1, 1, -1, -1 };
        static const int dz[] = { 0, 0, 1, -1, 1, -1, 1, -1 };
        static const float moveCosts[] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.414f, 1.414f, 1.414f, 1.414f };

        bool found = false;

        while (!openSet.empty()) {
            AStarNode* current = openSet.top();
            openSet.pop();

            if (current->x == targetX && current->z == targetZ) {
                found = true;
                break;
            }

            if (closedSet[current->x][current->z]) continue;
            closedSet[current->x][current->z] = true;

            for (int i = 0; i < 8; ++i) {
                int nx = current->x + dx[i];
                int nz = current->z + dz[i];

                if (nx < 0 || nx >= m_gridWidth || nz < 0 || nz >= m_gridDepth) continue;
                if (!m_grid[nz * m_gridWidth + nx].walkable) continue;
                if (closedSet[nx][nz]) continue;

                float stepCost = moveCosts[i] * m_grid[nz * m_gridWidth + nx].cost;
                float tentativeGCost = current->gCost + stepCost;

                if (tentativeGCost < nodeGrid[nx][nz].gCost) {
                    nodeGrid[nx][nz].gCost = tentativeGCost;
                    nodeGrid[nx][nz].hCost = std::hypot(static_cast<float>(targetX - nx), static_cast<float>(targetZ - nz));
                    nodeGrid[nx][nz].parentX = current->x;
                    nodeGrid[nx][nz].parentZ = current->z;
                    openSet.push(&nodeGrid[nx][nz]);
                }
            }
        }

        std::vector<glm::vec3> path;
        if (!found) {
            // Direct line fallback if unreachable
            path.push_back(target);
            return path;
        }

        // Backtrack path
        int curX = targetX;
        int curZ = targetZ;
        while (curX != startX || curZ != startZ) {
            path.push_back(toWorldPos(curX, curZ, target.y));
            int px = nodeGrid[curX][curZ].parentX;
            int pz = nodeGrid[curX][curZ].parentZ;
            if (px == -1 || pz == -1) break;
            curX = px;
            curZ = pz;
        }
        std::reverse(path.begin(), path.end());
        return path;
    }

    void NavigationSystem::update(ecs::Registry* registry, float dt) {
        if (!registry) return;
        auto& reg = registry->get();
        auto view = reg.view<ecs::NavAgentComponent, ecs::TransformComponent>();

        for (auto entity : view) {
            auto& agent = view.get<ecs::NavAgentComponent>(entity);
            auto& transform = view.get<ecs::TransformComponent>(entity);

            if (agent.hasTarget && (agent.waypoints.empty() || agent.currentWaypointIndex >= agent.waypoints.size())) {
                agent.waypoints = findPath(transform.position, agent.targetPosition);
                agent.currentWaypointIndex = 0;
            }

            if (!agent.waypoints.empty() && agent.currentWaypointIndex < agent.waypoints.size()) {
                glm::vec3 targetWaypoint = agent.waypoints[agent.currentWaypointIndex];
                glm::vec3 diff = targetWaypoint - transform.position;
                diff.y = 0.0f; // Horizontal movement
                float dist = glm::length(diff);

                if (dist <= agent.stoppingDistance) {
                    agent.currentWaypointIndex++;
                    if (agent.currentWaypointIndex >= agent.waypoints.size()) {
                        agent.isMoving = false;
                        agent.hasTarget = false;
                        continue;
                    }
                }

                agent.isMoving = true;
                glm::vec3 moveDir = glm::normalize(diff);
                transform.position += moveDir * agent.moveSpeed * dt;

                // Face movement direction
                float targetYaw = glm::degrees(std::atan2(moveDir.x, moveDir.z));
                transform.rotation.y = targetYaw;
            } else {
                agent.isMoving = false;
            }
        }
    }

}
