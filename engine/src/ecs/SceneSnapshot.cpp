#include "ecs/SceneSnapshot.hpp"
#include "physics/PhysicsContext.hpp"
#include "core/Logger.hpp"
#include <unordered_map>
#include <algorithm>

namespace engine::ecs {

    std::vector<EntityBinaryRecord> SceneSnapshot::capture(Registry& registry) {
        std::vector<EntityBinaryRecord> records;
        auto& reg = registry.get();
        auto view = reg.view<TransformComponent>();
        records.reserve(view.size());

        for (auto [entity, transform] : view.each()) {
            EntityBinaryRecord rec{};
            rec.entityId = static_cast<uint32_t>(entity);
            rec.hasTransform = true;
            rec.transform = transform;

            if (reg.all_of<LocalTransformComponent>(entity)) {
                rec.hasLocalTransform = true;
                rec.localTransform = reg.get<LocalTransformComponent>(entity);
            }

            if (reg.all_of<HierarchyComponent>(entity)) {
                const auto& h = reg.get<HierarchyComponent>(entity);
                rec.hasHierarchy = true;
                rec.hierarchyParent = (h.parent != entt::null && reg.valid(h.parent))
                    ? static_cast<uint32_t>(h.parent) : 0xFFFFFFFF;
            }

            if (reg.all_of<TagComponent>(entity)) {
                rec.hasTag = true;
                const auto& tag = reg.get<TagComponent>(entity);
                size_t copyLen = std::min(tag.name.size(), sizeof(rec.tag) - 1);
                std::memcpy(rec.tag, tag.name.data(), copyLen);
                rec.tag[copyLen] = '\0';
            }

            if (reg.all_of<MeshComponent>(entity)) {
                rec.hasMesh = true;
                rec.mesh = reg.get<MeshComponent>(entity);
            }

            if (reg.all_of<RigidBodyComponent>(entity)) {
                rec.hasRigidBody = true;
                rec.rigidBody = reg.get<RigidBodyComponent>(entity);
            }

            if (reg.all_of<VelocityComponent>(entity)) {
                rec.hasVelocity = true;
                rec.velocity = reg.get<VelocityComponent>(entity);
            }

            records.push_back(rec);
        }

        return records;
    }

    void SceneSnapshot::restore(Registry& registry, physics::PhysicsContext* physicsContext, const std::vector<EntityBinaryRecord>& snapshot) {
        auto& reg = registry.get();

        // 1. Destroy existing entities to restore pristine snapshot state
        std::vector<entt::entity> existing;
        auto view = reg.view<TransformComponent>();
        for (auto [entity, _] : view.each()) {
            existing.push_back(entity);
        }
        for (auto e : existing) {
            registry.destroyEntity(e);
        }
        reg.clear();

        // 2. Map snapshot entityId to newly recreated entt::entity
        std::unordered_map<uint32_t, entt::entity> entityMap;
        entityMap.reserve(snapshot.size());

        for (const auto& rec : snapshot) {
            entt::entity newEntity = reg.create();
            entityMap[rec.entityId] = newEntity;

            if (rec.hasTransform) {
                reg.emplace<TransformComponent>(newEntity, rec.transform);
            }
            if (rec.hasLocalTransform) {
                reg.emplace<LocalTransformComponent>(newEntity, rec.localTransform);
            }
            if (rec.hasTag) {
                reg.emplace<TagComponent>(newEntity, std::string(rec.tag));
            }
            if (rec.hasMesh) {
                reg.emplace<MeshComponent>(newEntity, rec.mesh);
            }
            if (rec.hasRigidBody) {
                reg.emplace<RigidBodyComponent>(newEntity, rec.rigidBody);
            }
            if (rec.hasVelocity) {
                reg.emplace<VelocityComponent>(newEntity, rec.velocity);
            }
        }

        // 3. Restore hierarchy relations
        for (const auto& rec : snapshot) {
            if (rec.hasHierarchy && rec.hierarchyParent != 0xFFFFFFFF) {
                auto childIt = entityMap.find(rec.entityId);
                auto parentIt = entityMap.find(rec.hierarchyParent);
                if (childIt != entityMap.end() && parentIt != entityMap.end()) {
                    registry.setParent(childIt->second, parentIt->second);
                }
            }
        }

        // 4. Update world transforms across the hierarchy
        registry.updateWorldTransforms();

        // 5. Synchronize with Jolt Physics simulation state
        if (physicsContext) {
            auto& bodyInterface = physicsContext->getBodyInterface();
            for (const auto& rec : snapshot) {
                if (rec.hasRigidBody && rec.rigidBody.bodyId != 0xFFFFFFFF) {
                    JPH::BodyID bodyId(rec.rigidBody.bodyId);
                    if (!bodyId.IsInvalid()) {
                        bodyInterface.SetPositionAndRotation(
                            bodyId,
                            JPH::RVec3(rec.transform.position.x, rec.transform.position.y, rec.transform.position.z),
                            JPH::Quat(rec.transform.orientation.x, rec.transform.orientation.y, rec.transform.orientation.z, rec.transform.orientation.w),
                            JPH::EActivation::Activate);

                        if (rec.hasVelocity) {
                            bodyInterface.SetLinearAndAngularVelocity(
                                bodyId,
                                JPH::Vec3(rec.velocity.velocity.x, rec.velocity.velocity.y, rec.velocity.velocity.z),
                                JPH::Vec3(0.0f, 0.0f, 0.0f));
                        }
                    }
                }
            }
        }

        core::Logger::info("SceneSnapshot: Restored " + std::to_string(snapshot.size()) + " entities with Jolt physics sync.");
    }

} // namespace engine::ecs
