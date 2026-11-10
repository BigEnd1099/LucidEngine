#pragma once
#include "ecs/Registry.hpp"
#include "physics/PhysicsContext.hpp"
#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace engine::physics {
    class PhysicsSystem {
    public:
        PhysicsSystem(PhysicsContext* context);
        void update(ecs::Registry* registry, float dt);
        static JPH::ShapeRefC createShape(const ecs::RigidBodyComponent& rb, const glm::vec3& scale);
    private:
        PhysicsContext* m_context;
    };
}
