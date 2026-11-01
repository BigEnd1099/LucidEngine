#pragma once
#include <entt/entt.hpp>
#include "Components.hpp"

namespace engine::ecs {
    class Registry {
    public:
        entt::registry& get() { return m_registry; }
        const entt::registry& get() const { return m_registry; }

        entt::entity createEntity() {
            return m_registry.create();
        }

        void destroyEntity(entt::entity e) {
            m_registry.destroy(e);
        }

        void clear() {
            m_registry.clear();
        }

    private:
        entt::registry m_registry;
    };
}
