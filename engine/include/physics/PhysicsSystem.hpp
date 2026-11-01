#pragma once
#include "ecs/Registry.hpp"
#include "physics/PhysicsContext.hpp"

namespace engine::physics {
    class PhysicsSystem {
    public:
        PhysicsSystem(PhysicsContext* context);
        void update(ecs::Registry* registry, float dt);
    private:
        PhysicsContext* m_context;
    };
}
