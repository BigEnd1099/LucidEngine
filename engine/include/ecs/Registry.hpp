#pragma once
#include <entt/entt.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <unordered_set>
#include "Components.hpp"

namespace engine::ecs {
    class Registry {
    public:
        entt::registry& get() { return m_registry; }
        const entt::registry& get() const { return m_registry; }

        entt::entity createEntity() { return m_registry.create(); }

        // Establish a real parent-child relation. TransformComponent stores the
        // evaluated world transform; LocalTransformComponent stores the authored
        // transform relative to the parent.
        bool setParent(entt::entity child, entt::entity parent) {
            if (!m_registry.valid(child) || child == parent) return false;
            if (parent != entt::null && !m_registry.valid(parent)) return false;

            for (entt::entity cursor = parent; cursor != entt::null;) {
                if (cursor == child) return false; // prevent hierarchy cycles
                if (!m_registry.valid(cursor) || !m_registry.all_of<HierarchyComponent>(cursor)) break;
                cursor = m_registry.get<HierarchyComponent>(cursor).parent;
            }

            detachFromParent(child);
            auto& childHierarchy = m_registry.get_or_emplace<HierarchyComponent>(child);
            childHierarchy.parent = parent;
            if (parent != entt::null) {
                auto& parentHierarchy = m_registry.get_or_emplace<HierarchyComponent>(parent);
                if (std::find(parentHierarchy.children.begin(), parentHierarchy.children.end(), child) == parentHierarchy.children.end())
                    parentHierarchy.children.push_back(child);
            }
            return true;
        }

        bool reparentEntity(entt::entity child, entt::entity parent) {
            if (!m_registry.valid(child) || !m_registry.all_of<TransformComponent>(child)) return false;
            const TransformComponent worldBefore = m_registry.get<TransformComponent>(child);
            const bool hadLocalTransform = m_registry.all_of<LocalTransformComponent>(child);
            if (!hadLocalTransform) {
                LocalTransformComponent local{};
                local.position = worldBefore.position;
                local.rotation = worldBefore.rotation;
                local.scale = worldBefore.scale;
                local.orientation = worldBefore.orientation;
                m_registry.emplace<LocalTransformComponent>(child, local);
            }
            if (!setParent(child, parent)) {
                if (!hadLocalTransform) m_registry.remove<LocalTransformComponent>(child);
                return false;
            }
            m_registry.get<TransformComponent>(child) = worldBefore;
            syncLocalTransformFromWorld(child);
            return true;
        }

        bool clearParent(entt::entity child) {
            if (!m_registry.valid(child)) return false;
            detachFromParent(child);
            if (m_registry.all_of<LocalTransformComponent, TransformComponent>(child))
                syncLocalTransformFromWorld(child);
            return true;
        }

        // Convert an edited world-space TransformComponent back to the entity's
        // parent-relative local transform (used by the viewport gizmo).
        void syncLocalTransformFromWorld(entt::entity entity) {
            if (!m_registry.valid(entity) ||
                !m_registry.all_of<LocalTransformComponent, TransformComponent>(entity)) return;
            const auto& worldTransform = m_registry.get<TransformComponent>(entity);
            const glm::quat worldRotation = glm::length(worldTransform.orientation) > 1e-6f
                ? glm::normalize(worldTransform.orientation) : glm::quat(glm::radians(worldTransform.rotation));
            const glm::mat4 world = glm::translate(glm::mat4(1.0f), worldTransform.position) *
                glm::mat4_cast(worldRotation) * glm::scale(glm::mat4(1.0f), worldTransform.scale);

            glm::mat4 local = world;
            if (m_registry.all_of<HierarchyComponent>(entity)) {
                const entt::entity parent = m_registry.get<HierarchyComponent>(entity).parent;
                if (parent != entt::null && m_registry.valid(parent) &&
                    m_registry.all_of<TransformComponent>(parent)) {
                    const auto& parentTransform = m_registry.get<TransformComponent>(parent);
                    const glm::quat parentRotation = glm::length(parentTransform.orientation) > 1e-6f
                        ? glm::normalize(parentTransform.orientation) : glm::quat(glm::radians(parentTransform.rotation));
                    const glm::mat4 parentWorld = glm::translate(glm::mat4(1.0f), parentTransform.position) *
                        glm::mat4_cast(parentRotation) * glm::scale(glm::mat4(1.0f), parentTransform.scale);
                    local = glm::inverse(parentWorld) * world;
                }
            }

            glm::vec3 skew(0.0f), translation(0.0f), scale(1.0f);
            glm::vec4 perspective(0.0f);
            glm::quat orientation(1.0f, 0.0f, 0.0f, 0.0f);
            if (glm::decompose(local, scale, orientation, translation, skew, perspective)) {
                auto& localTransform = m_registry.get<LocalTransformComponent>(entity);
                localTransform.position = translation;
                localTransform.scale = scale;
                localTransform.orientation = glm::normalize(orientation);
                localTransform.rotation = glm::degrees(glm::eulerAngles(localTransform.orientation));
            }
        }

        void updateWorldTransforms() {
            // Hierarchy propagation is unnecessary for ordinary scenes. Avoid
            // constructing a visited set and walking every Transform when no
            // entity participates in a hierarchy.
            const auto hierarchyView = m_registry.view<HierarchyComponent>();
            if (hierarchyView.empty()) return;

            m_worldTransformVisited.clear();
            auto view = m_registry.view<TransformComponent>();
            m_worldTransformVisited.reserve(view.size());
            for (auto entity : view) {
                if (!m_registry.all_of<HierarchyComponent>(entity) ||
                    m_registry.get<HierarchyComponent>(entity).parent == entt::null)
                    updateWorldRecursive(entity, glm::mat4(1.0f), m_worldTransformVisited);
            }
            // Recover gracefully from malformed/orphaned hierarchy data.
            for (auto entity : view) {
                if (!m_worldTransformVisited.contains(entity))
                    updateWorldRecursive(entity, glm::mat4(1.0f), m_worldTransformVisited);
            }
        }

        void destroyEntity(entt::entity entity) {
            if (!m_registry.valid(entity)) return;
            if (m_registry.all_of<HierarchyComponent>(entity)) {
                // Detaching children mutates this vector, so iterate over a snapshot.
                const auto children = m_registry.get<HierarchyComponent>(entity).children;
                for (entt::entity child : children) {
                    if (!m_registry.valid(child)) continue;
                    detachFromParent(child);
                    // Keep the child's currently evaluated world transform when
                    // the former parent is deleted.
                    m_registry.remove<LocalTransformComponent>(child);
                }
            }
            detachFromParent(entity);
            m_registry.destroy(entity);
        }

        void clear() { m_registry.clear(); }

    private:
        // Retain hash buckets across frames instead of allocating a fresh set
        // every time world transforms are propagated.
        std::unordered_set<entt::entity> m_worldTransformVisited;

        void detachFromParent(entt::entity child) {
            if (!m_registry.valid(child) || !m_registry.all_of<HierarchyComponent>(child)) return;
            auto& hierarchy = m_registry.get<HierarchyComponent>(child);
            const entt::entity parent = hierarchy.parent;
            if (parent != entt::null && m_registry.valid(parent) && m_registry.all_of<HierarchyComponent>(parent)) {
                auto& siblings = m_registry.get<HierarchyComponent>(parent).children;
                siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
            }
            hierarchy.parent = entt::null;
        }

        void updateWorldRecursive(entt::entity entity, const glm::mat4& parentWorld,
                                  std::unordered_set<entt::entity>& visited) {
            if (!m_registry.valid(entity) || !visited.insert(entity).second) return;
            glm::mat4 world = parentWorld;
            if (m_registry.all_of<LocalTransformComponent, TransformComponent, RigidBodyComponent>(entity) &&
                m_registry.get<RigidBodyComponent>(entity).bodyId != 0xFFFFFFFF) {
                // Physics writes world-space transforms; fold those changes back into
                // local space before evaluating the hierarchy for this frame.
                syncLocalTransformFromWorld(entity);
            }
            if (m_registry.all_of<LocalTransformComponent, TransformComponent>(entity)) {
                const auto& local = m_registry.get<LocalTransformComponent>(entity);
                const glm::quat rotation = glm::length(local.orientation) > 1e-6f
                    ? glm::normalize(local.orientation) : glm::quat(glm::radians(local.rotation));
                const glm::mat4 localMatrix = glm::translate(glm::mat4(1.0f), local.position) *
                    glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), local.scale);
                world = parentWorld * localMatrix;

                glm::vec3 skew(0.0f), translation(0.0f), scale(1.0f);
                glm::vec4 perspective(0.0f);
                glm::quat orientation(1.0f, 0.0f, 0.0f, 0.0f);
                if (glm::decompose(world, scale, orientation, translation, skew, perspective)) {
                    auto& evaluated = m_registry.get<TransformComponent>(entity);
                    evaluated.position = translation;
                    evaluated.scale = scale;
                    evaluated.orientation = glm::normalize(orientation);
                    evaluated.rotation = glm::degrees(glm::eulerAngles(evaluated.orientation));
                }
            } else if (m_registry.all_of<TransformComponent>(entity)) {
                // A detached child keeps its last evaluated world transform after
                // its former parent is destroyed.
                const auto& transform = m_registry.get<TransformComponent>(entity);
                const glm::quat rotation = glm::length(transform.orientation) > 1e-6f
                    ? glm::normalize(transform.orientation) : glm::quat(glm::radians(transform.rotation));
                world = glm::translate(glm::mat4(1.0f), transform.position) *
                    glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), transform.scale);
            }
            if (m_registry.all_of<HierarchyComponent>(entity)) {
                const auto children = m_registry.get<HierarchyComponent>(entity).children;
                for (entt::entity child : children)
                    updateWorldRecursive(child, world, visited);
            }
        }

        entt::registry m_registry;
    };
}
