#pragma once
#include "ecs/Registry.hpp"
namespace engine::anim {
    class MotionMatching {
    public:
        void update(ecs::Registry& registry, float dt);
    };
}
