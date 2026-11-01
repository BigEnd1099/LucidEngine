#pragma once
#include "ecs/Registry.hpp"
#include "physics/PhysicsContext.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace engine::core {
    class Engine;
}

namespace engine::physics {

    class DestructionSystem {
    public:
        void update(ecs::Registry* registry, PhysicsContext* physics, core::Engine* engine, float dt, const std::vector<ContactPoint>& contacts);

        static void fractureEntity(ecs::Registry* registry, PhysicsContext* physics, core::Engine* engine, uint32_t entityId, const glm::vec3& impactPoint, const glm::vec3& impulse);
        static void detonateExplosion(ecs::Registry* registry, PhysicsContext* physics, core::Engine* engine, const glm::vec3& center, float radius, float force, float damage);
    };

}
