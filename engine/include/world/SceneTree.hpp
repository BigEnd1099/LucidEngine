#pragma once
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <entt/entt.hpp>
#include "ecs/Registry.hpp"
#include "world/Node3D.hpp"
#include "world/NodePath.hpp"

namespace engine::world {

    class SceneTree {
    public:
        explicit SceneTree(ecs::Registry* registry);
        ~SceneTree();

        ecs::Registry* get_registry() const { return m_registry; }

        Node3D* get_root() const { return m_root; }
        void set_root(Node3D* root);

        Node3D* create_node(const std::string& name = "Node3D");
        Node3D* wrap_entity(entt::entity entity);
        void destroy_node(Node3D* node);
        void destroy_entity(entt::entity entity);

        Node3D* find_node(const std::string& path) const;
        Node3D* find_by_uuid(uint64_t uuid) const;
        Node3D* find_by_entity(entt::entity entity) const;

        void process(float delta);
        void physics_process(float fixed_delta);

        void register_node(Node3D* node);
        void unregister_node(Node3D* node);

        std::vector<Node3D*> get_all_nodes() const;
        size_t node_count() const { return m_nodesByEntity.size(); }

        void clear();

    private:
        void _notify_enter_tree_recursive(Node3D* node);
        void _notify_exit_tree_recursive(Node3D* node);

        ecs::Registry* m_registry{nullptr};
        Node3D* m_root{nullptr};
        std::unordered_map<entt::entity, std::unique_ptr<Node3D>> m_nodesByEntity;
        std::unordered_map<uint64_t, Node3D*> m_nodesByUuid;
        std::unordered_set<Node3D*> m_registeredExternalNodes;
        bool m_isIterating{false};
        std::vector<Node3D*> m_pendingRemoval;
    };

} // namespace engine::world
