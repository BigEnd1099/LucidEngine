#include "physics/DestructionSystem.hpp"
#include "core/Engine.hpp"
#include "ecs/Components.hpp"
#include "core/Logger.hpp"
#include <random>
#include <algorithm>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>

namespace engine::physics {

    void DestructionSystem::update(ecs::Registry* registry, PhysicsContext* physics, core::Engine* engine, float dt, const std::vector<ContactPoint>& contacts) {
        if (!registry) return;
        auto& reg = registry->get();

        // 1. Check for entities with health <= 0
        std::vector<entt::entity> toFracture;
        auto view = reg.view<ecs::DestructibleComponent, ecs::TransformComponent>();
        for (auto entity : view) {
            auto& dest = view.get<ecs::DestructibleComponent>(entity);
            if (!dest.isBroken && dest.health <= 0.0f) {
                toFracture.push_back(entity);
            }
        }

        // 2. Check collision impact force from contact points
        for (const auto& contact : contacts) {
            auto e1 = static_cast<entt::entity>(contact.entity1);
            auto e2 = static_cast<entt::entity>(contact.entity2);

            for (auto target : {e1, e2}) {
                if (reg.valid(target) && reg.all_of<ecs::DestructibleComponent, ecs::TransformComponent>(target)) {
                    auto& dest = reg.get<ecs::DestructibleComponent>(target);
                    if (!dest.isBroken) {
                        float speed = 0.0f;
                        if (reg.all_of<ecs::VelocityComponent>(target)) {
                            speed = glm::length(reg.get<ecs::VelocityComponent>(target).velocity);
                        }
                        if (speed > dest.fractureForceThreshold) {
                            toFracture.push_back(target);
                        }
                    }
                }
            }
        }

        // De-duplicate fracture requests
        std::sort(toFracture.begin(), toFracture.end());
        toFracture.erase(std::unique(toFracture.begin(), toFracture.end()), toFracture.end());

        for (auto entity : toFracture) {
            auto& t = reg.get<ecs::TransformComponent>(entity);
            fractureEntity(registry, physics, engine, static_cast<uint32_t>(entity), t.position, glm::vec3(0.0f, 5.0f, 0.0f));
        }
    }

    void DestructionSystem::fractureEntity(ecs::Registry* registry, PhysicsContext* physics, core::Engine* engine, uint32_t entityId, const glm::vec3& impactPoint, const glm::vec3& impulse) {
        if (!registry) return;
        auto& reg = registry->get();
        auto e = static_cast<entt::entity>(entityId);
        if (!reg.valid(e) || !reg.all_of<ecs::DestructibleComponent, ecs::TransformComponent>(e)) return;

        auto& dest = reg.get<ecs::DestructibleComponent>(e);
        if (dest.isBroken) return;
        dest.isBroken = true;

        auto origTransform = reg.get<ecs::TransformComponent>(e);
        float origMass = 1.0f;
        glm::vec3 halfExtents = origTransform.scale * 0.5f;

        if (reg.all_of<ecs::RigidBodyComponent>(e)) {
            auto& rb = reg.get<ecs::RigidBodyComponent>(e);
            origMass = rb.mass;
            halfExtents = rb.halfExtents;
            // Remove the original rigid body from physics
            if (physics && rb.bodyId != 0xFFFFFFFF) {
                JPH::BodyID bId(rb.bodyId);
                physics->getBodyInterface().RemoveBody(bId);
                physics->getBodyInterface().DestroyBody(bId);
                rb.bodyId = 0xFFFFFFFF;
            }
        }

        glm::vec4 shardColor{0.7f, 0.7f, 0.7f, 1.0f};
        if (reg.all_of<ecs::MaterialComponent>(e)) {
            shardColor = reg.get<ecs::MaterialComponent>(e).baseColor;
        }

        uint32_t numShards = std::clamp(dest.shardCount, 4u, 24u);
        float shardMass = std::max(0.1f, origMass / static_cast<float>(numShards));
        glm::vec3 shardExtents = halfExtents / 2.0f;

        std::mt19937 rng(1337 + entityId);
        std::uniform_real_distribution<float> distOffset(-0.4f, 0.4f);
        std::uniform_real_distribution<float> distVelocity(-4.0f, 4.0f);

        // Spawn Voronoi-like fractured debris shards
        for (uint32_t i = 0; i < numShards; ++i) {
            auto shard = reg.create();

            glm::vec3 offset(
                distOffset(rng) * halfExtents.x * 2.0f,
                distOffset(rng) * halfExtents.y * 2.0f,
                distOffset(rng) * halfExtents.z * 2.0f
            );

            ecs::TransformComponent st;
            st.position = origTransform.position + offset;
            st.scale = shardExtents;
            st.rotation = glm::vec3(distVelocity(rng) * 10.0f, distVelocity(rng) * 10.0f, distVelocity(rng) * 10.0f);
            reg.emplace<ecs::TransformComponent>(shard, st);

            ecs::RigidBodyComponent srb;
            srb.mass = shardMass;
            srb.shapeType = 0; // Box shard
            srb.halfExtents = shardExtents;
            srb.restitution = 0.4f;
            srb.friction = 0.6f;
            reg.emplace<ecs::RigidBodyComponent>(shard, srb);

            ecs::VelocityComponent svel;
            svel.velocity = glm::vec3(distVelocity(rng), std::abs(distVelocity(rng)) + 2.0f, distVelocity(rng)) + impulse * 0.1f;
            reg.emplace<ecs::VelocityComponent>(shard, svel);

            ecs::MaterialComponent smat;
            smat.baseColor = shardColor;
            smat.roughness = 0.8f;
            reg.emplace<ecs::MaterialComponent>(shard, smat);

            ecs::TagComponent stag;
            stag.name = "DebrisShard_" + std::to_string(entityId) + "_" + std::to_string(i);
            reg.emplace<ecs::TagComponent>(shard, stag);
        }

        // Hide or destroy the fractured entity
        reg.destroy(e);

        if (engine) {
            engine->broadcastSfx("explosion", 0.8f);
            engine->broadcastCameraShake(0.6f, 0.4f);
        }
        core::Logger::info("Destruction: Entity " + std::to_string(entityId) + " fractured into " + std::to_string(numShards) + " shards.");
    }

    void DestructionSystem::detonateExplosion(ecs::Registry* registry, PhysicsContext* physics, core::Engine* engine, const glm::vec3& center, float radius, float force, float damage) {
        if (!registry) return;
        auto& reg = registry->get();

        auto view = reg.view<ecs::TransformComponent>();
        std::vector<entt::entity> damagedDestructibles;

        for (auto entity : view) {
            const auto& t = view.get<ecs::TransformComponent>(entity);
            float dist = glm::distance(center, t.position);
            if (dist <= radius) {
                float falloff = 1.0f - (dist / radius); // Linear falloff 1.0 -> 0.0
                glm::vec3 dir = (dist > 0.01f) ? glm::normalize(t.position - center) : glm::vec3(0.0f, 1.0f, 0.0f);
                glm::vec3 impulse = dir * (force * falloff) + glm::vec3(0.0f, force * 0.3f * falloff, 0.0f);

                // Apply physics impulse
                if (physics && reg.all_of<ecs::RigidBodyComponent>(entity)) {
                    auto& rb = reg.get<ecs::RigidBodyComponent>(entity);
                    if (rb.bodyId != 0xFFFFFFFF && rb.mass > 0.0f) {
                        JPH::BodyID bId(rb.bodyId);
                        physics->getBodyInterface().AddImpulse(bId, JPH::Vec3(impulse.x, impulse.y, impulse.z));
                    }
                }

                // Apply damage to destructibles
                if (reg.all_of<ecs::DestructibleComponent>(entity)) {
                    auto& dest = reg.get<ecs::DestructibleComponent>(entity);
                    dest.health -= damage * falloff * (1.0f - dest.explosionResistance);
                    if (dest.health <= 0.0f) {
                        damagedDestructibles.push_back(entity);
                    }
                }
            }
        }

        for (auto entity : damagedDestructibles) {
            if (reg.valid(entity)) {
                auto& t = reg.get<ecs::TransformComponent>(entity);
                glm::vec3 impulse = glm::normalize(t.position - center) * (force * 0.2f);
                fractureEntity(registry, physics, engine, static_cast<uint32_t>(entity), center, impulse);
            }
        }

        if (engine) {
            engine->broadcastSfx("explosion", 1.0f);
            engine->broadcastCameraShake(1.0f, 0.5f);
        }
    }

}
