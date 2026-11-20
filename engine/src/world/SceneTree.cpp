#include "world/SceneTree.hpp"
#include <sstream>
#include <algorithm>

namespace engine::world {

    entt::entity NodePath::resolve(SceneTree* tree) const {
        if (!tree || !tree->get_registry()) return entt::null;
        if (m_entity != entt::null && tree->get_registry()->get().valid(m_entity)) {
            return m_entity;
        }
        if (!m_path.empty()) {
            Node3D* found = tree->find_node(m_path);
            if (found) return found->get_entity();
        }
        return entt::null;
    }

    SceneTree::SceneTree(ecs::Registry* registry) : m_registry(registry) {
        if (m_registry) {
            Node3D* root = create_node("Root");
            set_root(root);
        }
    }

    SceneTree::~SceneTree() {
        clear();
    }

    void SceneTree::set_root(Node3D* root) {
        if (m_root && m_root != root) {
            _notify_exit_tree_recursive(m_root);
        }
        m_root = root;
        if (m_root) {
            _notify_enter_tree_recursive(m_root);
        }
    }

    Node3D* SceneTree::create_node(const std::string& name) {
        if (!m_registry) return nullptr;
        entt::entity entity = m_registry->createEntity();
        m_registry->get().emplace<ecs::TransformComponent>(entity);
        m_registry->get().emplace<ecs::LocalTransformComponent>(entity);
        m_registry->get().emplace<ecs::TagComponent>(entity, name);

        auto node = std::make_unique<Node3D>(entity, m_registry, this);
        Node3D* raw = node.get();
        m_nodesByEntity[entity] = std::move(node);
        register_node(raw);
        return raw;
    }

    Node3D* SceneTree::wrap_entity(entt::entity entity) {
        if (!m_registry || !m_registry->get().valid(entity)) return nullptr;
        auto it = m_nodesByEntity.find(entity);
        if (it != m_nodesByEntity.end()) {
            return it->second.get();
        }
        auto node = std::make_unique<Node3D>(entity, m_registry, this);
        Node3D* raw = node.get();
        m_nodesByEntity[entity] = std::move(node);
        register_node(raw);
        return raw;
    }

    void SceneTree::destroy_node(Node3D* node) {
        if (!node) return;
        if (m_isIterating) {
            m_pendingRemoval.push_back(node);
            return;
        }
        node->on_destroyed.emit(node);
        _notify_exit_tree_recursive(node);
        unregister_node(node);
        if (node == m_root) {
            m_root = nullptr;
        }
        destroy_entity(node->get_entity());
    }

    void SceneTree::destroy_entity(entt::entity entity) {
        if (!m_registry || !m_registry->get().valid(entity)) return;

        auto it = m_nodesByEntity.find(entity);
        Node3D* raw = (it != m_nodesByEntity.end()) ? it->second.get() : nullptr;
        if (!raw) {
            for (Node3D* ext : m_registeredExternalNodes) {
                if (ext && ext->get_entity() == entity) {
                    raw = ext;
                    break;
                }
            }
        }

        if (m_isIterating && raw) {
            m_pendingRemoval.push_back(raw);
            return;
        }

        if (raw) {
            raw->on_destroyed.emit(raw);
            _notify_exit_tree_recursive(raw);
            unregister_node(raw);
            if (raw == m_root) {
                m_root = nullptr;
            }
        }

        m_registry->destroyEntity(entity);
        if (it != m_nodesByEntity.end()) {
            m_nodesByEntity.erase(it);
        }
    }

    Node3D* SceneTree::find_node(const std::string& path) const {
        if (path.empty() || !m_root) return nullptr;

        std::vector<std::string> segments;
        std::stringstream ss(path);
        std::string segment;
        while (std::getline(ss, segment, '/')) {
            if (!segment.empty()) {
                segments.push_back(segment);
            }
        }

        if (segments.empty()) return m_root;

        size_t startIndex = 0;
        Node3D* current = m_root;

        if (segments[0] == m_root->get_name()) {
            startIndex = 1;
        }

        for (size_t i = startIndex; i < segments.size(); ++i) {
            const std::string& targetName = segments[i];
            bool foundChild = false;
            for (Node3D* child : current->get_children()) {
                if (child && child->get_name() == targetName) {
                    current = child;
                    foundChild = true;
                    break;
                }
            }
            if (!foundChild) {
                return nullptr;
            }
        }

        return current;
    }

    Node3D* SceneTree::find_by_uuid(uint64_t uuid) const {
        if (uuid == 0) return nullptr;
        auto it = m_nodesByUuid.find(uuid);
        if (it != m_nodesByUuid.end()) {
            return it->second;
        }
        return nullptr;
    }

    Node3D* SceneTree::find_by_entity(entt::entity entity) const {
        auto it = m_nodesByEntity.find(entity);
        if (it != m_nodesByEntity.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    void SceneTree::process(float delta) {
        m_isIterating = true;
        std::vector<Node3D*> activeNodes = get_all_nodes();
        for (Node3D* node : activeNodes) {
            if (node && node->is_valid()) {
                if (!node->m_isReady) {
                    node->m_isReady = true;
                    node->_ready();
                }
                node->_process(delta);
            }
        }
        m_isIterating = false;

        for (Node3D* pending : m_pendingRemoval) {
            destroy_node(pending);
        }
        m_pendingRemoval.clear();
    }

    void SceneTree::physics_process(float fixed_delta) {
        m_isIterating = true;
        std::vector<Node3D*> activeNodes = get_all_nodes();
        for (Node3D* node : activeNodes) {
            if (node && node->is_valid()) {
                node->_physics_process(fixed_delta);
            }
        }
        m_isIterating = false;

        for (Node3D* pending : m_pendingRemoval) {
            destroy_node(pending);
        }
        m_pendingRemoval.clear();
    }

    void SceneTree::register_node(Node3D* node) {
        if (!node) return;
        m_registeredExternalNodes.insert(node);
        uint64_t uuid = node->get_uuid();
        if (uuid != 0) {
            auto it = m_nodesByUuid.find(uuid);
            if (it != m_nodesByUuid.end() && it->second != node) {
                // Deduplicate UUID collision (e.g. duplicating nodes or cameras)
                uint64_t newUuid = uuid ^ (static_cast<uint64_t>(node->get_entity()) * 0x9e3779b97f4a7c15ull + 1u);
                if (newUuid == 0) newUuid = 1u;
                node->set_uuid(newUuid);
                uuid = newUuid;
            }
            m_nodesByUuid[uuid] = node;
        }
    }

    void SceneTree::unregister_node(Node3D* node) {
        if (!node) return;
        m_registeredExternalNodes.erase(node);
        uint64_t uuid = node->get_uuid();
        if (uuid != 0) {
            m_nodesByUuid.erase(uuid);
        }
    }

    std::vector<Node3D*> SceneTree::get_all_nodes() const {
        std::vector<Node3D*> result;
        result.reserve(m_nodesByEntity.size() + m_registeredExternalNodes.size());
        for (const auto& [entity, nodePtr] : m_nodesByEntity) {
            if (nodePtr && nodePtr->is_valid()) {
                result.push_back(nodePtr.get());
            }
        }
        for (Node3D* node : m_registeredExternalNodes) {
            if (node && node->is_valid()) {
                if (m_nodesByEntity.find(node->get_entity()) == m_nodesByEntity.end()) {
                    result.push_back(node);
                }
            }
        }
        return result;
    }

    void SceneTree::clear() {
        m_pendingRemoval.clear();
        m_registeredExternalNodes.clear();
        m_nodesByUuid.clear();
        m_nodesByEntity.clear();
        m_root = nullptr;
    }

    void SceneTree::_notify_enter_tree_recursive(Node3D* node) {
        if (!node || !node->is_valid()) return;
        if (!node->m_inTree) {
            node->m_inTree = true;
            node->_enter_tree();
        }
        for (Node3D* child : node->get_children()) {
            _notify_enter_tree_recursive(child);
        }
    }

    void SceneTree::_notify_exit_tree_recursive(Node3D* node) {
        if (!node || !node->is_valid()) return;
        for (Node3D* child : node->get_children()) {
            _notify_exit_tree_recursive(child);
        }
        if (node->m_inTree) {
            node->_exit_tree();
            node->m_inTree = false;
        }
    }

} // namespace engine::world
