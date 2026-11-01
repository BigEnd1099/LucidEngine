#pragma once
#include "ecs/Registry.hpp"
#include "physics/PhysicsContext.hpp"
#include <vector>

namespace engine::physics {

    class ChemistrySystem {
    public:
        void update(ecs::Registry* registry, float dt, const std::vector<ContactPoint>& contacts);

        static void ignite(ecs::Registry* registry, uint32_t entityId, float initialTemp = 400.0f);
        static void extinguish(ecs::Registry* registry, uint32_t entityId);
        static void electrify(ecs::Registry* registry, uint32_t entityId, float voltage = 100.0f);
        static void freeze(ecs::Registry* registry, uint32_t entityId);
    };

}
