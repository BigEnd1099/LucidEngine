#pragma once
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <memory>
#include "ecs/Registry.hpp"
#include "ecs/Components.hpp"
#include "core/Signal.hpp"

namespace engine::world {

    class SceneTree;

    // Node3D is an object-oriented facade over EnTT entity & components.
    // The ECS registry remains the single source of truth for all transforms,
    // mesh components, and physics bodies.
    class Node3D {
    public:
        Node3D(entt::entity entity, ecs::Registry* registry, SceneTree* tree = nullptr);
        virtual ~Node3D() = default;

        // --- Lifecycle Hooks (override in subclasses / scripts) ---
        virtual void _enter_tree() {}
        virtual void _ready() {}
        virtual void _process(float delta) { (void)delta; }
        virtual void _physics_process(float fixed_delta) { (void)fixed_delta; }
        virtual void _exit_tree() {}

        // --- Identity ---
        entt::entity get_entity() const { return m_entity; }
        ecs::Registry* get_registry() const { return m_registry; }
        SceneTree* get_tree() const { return m_tree; }
        void set_tree(SceneTree* tree) { m_tree = tree; }
        bool is_valid() const;

        std::string get_name() const;
        void set_name(const std::string& name);

        uint64_t get_uuid() const;
        void set_uuid(uint64_t uuid);

        // --- Hierarchy Operations (delegated to ecs::Registry) ---
        bool add_child(Node3D* child);
        bool remove_child(Node3D* child);
        bool remove_from_parent();
        Node3D* get_parent() const;
        std::vector<Node3D*> get_children() const;

        // --- Transform Operations (LocalTransformComponent & TransformComponent) ---
        void set_position(const glm::vec3& pos);
        void set_rotation(const glm::vec3& eulerDegrees);
        void set_orientation(const glm::quat& orientation);
        void set_scale(const glm::vec3& scale);

        glm::vec3 get_position() const;
        glm::vec3 get_rotation() const;
        glm::quat get_orientation() const;
        glm::vec3 get_scale() const;

        glm::vec3 get_world_position() const;
        glm::vec3 get_world_rotation() const;
        glm::quat get_world_orientation() const;
        glm::vec3 get_world_scale() const;
        glm::mat4 get_world_matrix() const;

        // --- Component Access Helpers ---
        template<typename T>
        bool has_component() const {
            return is_valid() && m_registry->get().all_of<T>(m_entity);
        }

        template<typename T>
        T& get_component() {
            return m_registry->get().get<T>(m_entity);
        }

        template<typename T>
        const T& get_component() const {
            return m_registry->get().get<T>(m_entity);
        }

        template<typename T, typename... Args>
        T& add_component(Args&&... args) {
            return m_registry->get().get_or_emplace<T>(m_entity, std::forward<Args>(args)...);
        }

        template<typename T>
        void remove_component() {
            if (is_valid() && has_component<T>()) {
                m_registry->get().remove<T>(m_entity);
            }
        }

        // --- Signal Bus ---
        core::Signal<Node3D*> on_destroyed;

    protected:
        entt::entity m_entity{entt::null};
        ecs::Registry* m_registry{nullptr};
        SceneTree* m_tree{nullptr};
        bool m_inTree{false};
        bool m_isReady{false};

        friend class SceneTree;
    };

} // namespace engine::world
