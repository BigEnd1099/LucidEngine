#include "physics/ChemistrySystem.hpp"
#include "ecs/Components.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <cmath>

namespace engine::physics {

    void ChemistrySystem::update(ecs::Registry* registry, float dt, const std::vector<ContactPoint>& contacts) {
        if (!registry) return;
        auto& reg = registry->get();
        auto view = reg.view<ecs::ChemistryComponent, ecs::TransformComponent>();

        // 1. Process individual thermodynamic states
        for (auto entity : view) {
            auto& chem = view.get<ecs::ChemistryComponent>(entity);
            auto& transform = view.get<ecs::TransformComponent>(entity);

            // Ambient temperature regulation (returns towards 20°C slowly)
            if (!chem.isBurning) {
                float tempDiff = 20.0f - chem.temperature;
                chem.temperature += tempDiff * dt * 0.1f;
            } else {
                chem.temperature = std::max(chem.temperature, 500.0f);
                chem.burnDuration += dt;

                // Wetness suppresses fire
                if (chem.wetness > 0.3f) {
                    chem.isBurning = false;
                    chem.wetness = std::max(0.0f, chem.wetness - 0.2f);
                    chem.temperature = 80.0f; // Cools down to warm steam
                }

                // If burning, apply damage to DestructibleComponent if attached
                if (reg.all_of<ecs::DestructibleComponent>(entity)) {
                    auto& dest = reg.get<ecs::DestructibleComponent>(entity);
                    dest.health -= 15.0f * dt;
                }
            }

            // Wetness naturally dries out over time
            if (chem.wetness > 0.0f && !chem.isFrozen) {
                float dryRate = (chem.temperature > 50.0f) ? 0.2f : 0.02f;
                chem.wetness = std::max(0.0f, chem.wetness - dryRate * dt);
            }

            // Electric charge dissipates rapidly
            if (chem.electricCharge > 0.0f) {
                chem.electricCharge = std::max(0.0f, chem.electricCharge - 150.0f * dt);
            }

            // Freezing check (< 0°C on water or wet objects)
            if (chem.temperature <= 0.0f && (chem.primaryElement == ecs::ElementType::WATER || chem.wetness > 0.5f)) {
                chem.isFrozen = true;
                // If it has a rigid body, make it ultra slippery
                if (reg.all_of<ecs::RigidBodyComponent>(entity)) {
                    auto& rb = reg.get<ecs::RigidBodyComponent>(entity);
                    rb.friction = 0.02f;
                }
            } else if (chem.temperature > 10.0f && chem.isFrozen) {
                chem.isFrozen = false;
                chem.wetness = 1.0f;
                if (reg.all_of<ecs::RigidBodyComponent>(entity)) {
                    auto& rb = reg.get<ecs::RigidBodyComponent>(entity);
                    rb.friction = 0.4f;
                }
            }
        }

        // 2. Process chemical interactions via physical contacts
        for (const auto& contact : contacts) {
            auto e1 = static_cast<entt::entity>(contact.entity1);
            auto e2 = static_cast<entt::entity>(contact.entity2);

            if (!reg.valid(e1) || !reg.valid(e2)) continue;
            if (!reg.all_of<ecs::ChemistryComponent>(e1) || !reg.all_of<ecs::ChemistryComponent>(e2)) continue;

            auto& chem1 = reg.get<ecs::ChemistryComponent>(e1);
            auto& chem2 = reg.get<ecs::ChemistryComponent>(e2);

            // A. Fire propagation: Burning object touches flammable object
            if (chem1.isBurning && !chem2.isBurning && chem2.flammability > 0.1f && chem2.wetness < 0.3f) {
                chem2.temperature += 200.0f * dt;
                if (chem2.temperature >= 250.0f) {
                    chem2.isBurning = true;
                }
            } else if (chem2.isBurning && !chem1.isBurning && chem1.flammability > 0.1f && chem1.wetness < 0.3f) {
                chem1.temperature += 200.0f * dt;
                if (chem1.temperature >= 250.0f) {
                    chem1.isBurning = true;
                }
            }

            // B. Water extinguishes fire
            if (chem1.primaryElement == ecs::ElementType::WATER || chem1.wetness > 0.5f) {
                if (chem2.isBurning) {
                    chem2.isBurning = false;
                    chem2.temperature = 60.0f;
                    chem2.wetness = 1.0f;
                }
            }
            if (chem2.primaryElement == ecs::ElementType::WATER || chem2.wetness > 0.5f) {
                if (chem1.isBurning) {
                    chem1.isBurning = false;
                    chem1.temperature = 60.0f;
                    chem1.wetness = 1.0f;
                }
            }

            // C. Electrical conduction: Electricity passes through conductors or wet objects
            float maxCharge = std::max(chem1.electricCharge, chem2.electricCharge);
            if (maxCharge > 10.0f) {
                bool cond1 = chem1.conductivity > 0.4f || chem1.wetness > 0.3f || chem1.primaryElement == ecs::ElementType::METAL;
                bool cond2 = chem2.conductivity > 0.4f || chem2.wetness > 0.3f || chem2.primaryElement == ecs::ElementType::METAL;

                if (cond1 && cond2) {
                    chem1.electricCharge = maxCharge * 0.95f;
                    chem2.electricCharge = maxCharge * 0.95f;
                }
            }
        }
    }

    void ChemistrySystem::ignite(ecs::Registry* registry, uint32_t entityId, float initialTemp) {
        if (!registry) return;
        auto e = static_cast<entt::entity>(entityId);
        if (registry->get().valid(e) && registry->get().all_of<ecs::ChemistryComponent>(e)) {
            auto& chem = registry->get().get<ecs::ChemistryComponent>(e);
            chem.isBurning = true;
            chem.temperature = std::max(chem.temperature, initialTemp);
            chem.wetness = 0.0f;
        }
    }

    void ChemistrySystem::extinguish(ecs::Registry* registry, uint32_t entityId) {
        if (!registry) return;
        auto e = static_cast<entt::entity>(entityId);
        if (registry->get().valid(e) && registry->get().all_of<ecs::ChemistryComponent>(e)) {
            auto& chem = registry->get().get<ecs::ChemistryComponent>(e);
            chem.isBurning = false;
            chem.wetness = 1.0f;
            chem.temperature = 30.0f;
        }
    }

    void ChemistrySystem::electrify(ecs::Registry* registry, uint32_t entityId, float voltage) {
        if (!registry) return;
        auto e = static_cast<entt::entity>(entityId);
        if (registry->get().valid(e) && registry->get().all_of<ecs::ChemistryComponent>(e)) {
            auto& chem = registry->get().get<ecs::ChemistryComponent>(e);
            chem.electricCharge = voltage;
        }
    }

    void ChemistrySystem::freeze(ecs::Registry* registry, uint32_t entityId) {
        if (!registry) return;
        auto e = static_cast<entt::entity>(entityId);
        if (registry->get().valid(e) && registry->get().all_of<ecs::ChemistryComponent>(e)) {
            auto& chem = registry->get().get<ecs::ChemistryComponent>(e);
            chem.isFrozen = true;
            chem.isBurning = false;
            chem.temperature = -20.0f;
        }
    }

}
