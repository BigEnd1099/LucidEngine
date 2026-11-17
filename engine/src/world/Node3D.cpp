#include "world/Node3D.hpp"
#include "world/SceneTree.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace engine::world {

    Node3D::Node3D(entt::entity entity, ecs::Registry* registry, SceneTree* tree)
        : m_entity(entity), m_registry(registry), m_tree(tree) {
    }

    bool Node3D::is_valid() const {
        return m_registry != nullptr && m_registry->get().valid(m_entity);
    }

    std::string Node3D::get_name() const {
        if (!is_valid()) return "";
        if (m_registry->get().all_of<ecs::TagComponent>(m_entity)) {
            return m_registry->get().get<ecs::TagComponent>(m_entity).name;
        }
        return "Entity_" + std::to_string(static_cast<uint32_t>(m_entity));
    }

    void Node3D::set_name(const std::string& name) {
        if (!is_valid()) return;
        m_registry->get().get_or_emplace<ecs::TagComponent>(m_entity).name = name;
    }

    uint64_t Node3D::get_uuid() const {
        if (!is_valid()) return 0;
        if (m_registry->get().all_of<ecs::UUIDComponent>(m_entity)) {
            return m_registry->get().get<ecs::UUIDComponent>(m_entity).uuid;
        }
        return 0;
    }

    void Node3D::set_uuid(uint64_t uuid) {
        if (!is_valid()) return;
        m_registry->get().get_or_emplace<ecs::UUIDComponent>(m_entity).uuid = uuid;
    }

    bool Node3D::add_child(Node3D* child) {
        if (!is_valid() || !child || !child->is_valid()) return false;
        bool ok = m_registry->setParent(child->get_entity(), m_entity);
        if (ok) {
            if (m_tree) {
                child->set_tree(m_tree);
                m_tree->register_node(child);
            }
            if ((m_inTree || (m_tree && this == m_tree->get_root())) && !child->m_inTree) {
                child->m_inTree = true;
                child->_enter_tree();
                if (!child->m_isReady) {
                    child->m_isReady = true;
                    child->_ready();
                }
            }
        }
        return ok;
    }

    bool Node3D::remove_child(Node3D* child) {
        if (!is_valid() || !child || !child->is_valid()) return false;
        if (child->get_parent() != this) return false;
        return child->remove_from_parent();
    }

    bool Node3D::remove_from_parent() {
        if (!is_valid()) return false;
        return m_registry->clearParent(m_entity);
    }

    Node3D* Node3D::get_parent() const {
        if (!is_valid() || !m_tree) return nullptr;
        if (m_registry->get().all_of<ecs::HierarchyComponent>(m_entity)) {
            entt::entity parentEntity = m_registry->get().get<ecs::HierarchyComponent>(m_entity).parent;
            if (parentEntity != entt::null && m_registry->get().valid(parentEntity)) {
                return m_tree->wrap_entity(parentEntity);
            }
        }
        return nullptr;
    }

    std::vector<Node3D*> Node3D::get_children() const {
        std::vector<Node3D*> result;
        if (!is_valid() || !m_tree) return result;
        if (m_registry->get().all_of<ecs::HierarchyComponent>(m_entity)) {
            const auto& children = m_registry->get().get<ecs::HierarchyComponent>(m_entity).children;
            result.reserve(children.size());
            for (entt::entity childEntity : children) {
                if (m_registry->get().valid(childEntity)) {
                    result.push_back(m_tree->wrap_entity(childEntity));
                }
            }
        }
        return result;
    }

    void Node3D::set_position(const glm::vec3& pos) {
        if (!is_valid()) return;
        auto& local = m_registry->get().get_or_emplace<ecs::LocalTransformComponent>(m_entity);
        local.position = pos;
        m_registry->markTransformDirty(m_entity);
    }

    void Node3D::set_rotation(const glm::vec3& eulerDegrees) {
        if (!is_valid()) return;
        auto& local = m_registry->get().get_or_emplace<ecs::LocalTransformComponent>(m_entity);
        local.rotation = eulerDegrees;
        local.orientation = glm::quat(glm::radians(eulerDegrees));
        m_registry->markTransformDirty(m_entity);
    }

    void Node3D::set_orientation(const glm::quat& orientation) {
        if (!is_valid()) return;
        auto& local = m_registry->get().get_or_emplace<ecs::LocalTransformComponent>(m_entity);
        local.orientation = glm::normalize(orientation);
        local.rotation = glm::degrees(glm::eulerAngles(local.orientation));
        m_registry->markTransformDirty(m_entity);
    }

    void Node3D::set_scale(const glm::vec3& scale) {
        if (!is_valid()) return;
        auto& local = m_registry->get().get_or_emplace<ecs::LocalTransformComponent>(m_entity);
        local.scale = scale;
        m_registry->markTransformDirty(m_entity);
    }

    glm::vec3 Node3D::get_position() const {
        if (!is_valid()) return glm::vec3(0.0f);
        if (m_registry->get().all_of<ecs::LocalTransformComponent>(m_entity)) {
            return m_registry->get().get<ecs::LocalTransformComponent>(m_entity).position;
        }
        if (m_registry->get().all_of<ecs::TransformComponent>(m_entity)) {
            return m_registry->get().get<ecs::TransformComponent>(m_entity).position;
        }
        return glm::vec3(0.0f);
    }

    glm::vec3 Node3D::get_rotation() const {
        if (!is_valid()) return glm::vec3(0.0f);
        if (m_registry->get().all_of<ecs::LocalTransformComponent>(m_entity)) {
            return m_registry->get().get<ecs::LocalTransformComponent>(m_entity).rotation;
        }
        if (m_registry->get().all_of<ecs::TransformComponent>(m_entity)) {
            return m_registry->get().get<ecs::TransformComponent>(m_entity).rotation;
        }
        return glm::vec3(0.0f);
    }

    glm::quat Node3D::get_orientation() const {
        if (!is_valid()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        if (m_registry->get().all_of<ecs::LocalTransformComponent>(m_entity)) {
            return m_registry->get().get<ecs::LocalTransformComponent>(m_entity).orientation;
        }
        if (m_registry->get().all_of<ecs::TransformComponent>(m_entity)) {
            return m_registry->get().get<ecs::TransformComponent>(m_entity).orientation;
        }
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    glm::vec3 Node3D::get_scale() const {
        if (!is_valid()) return glm::vec3(1.0f);
        if (m_registry->get().all_of<ecs::LocalTransformComponent>(m_entity)) {
            return m_registry->get().get<ecs::LocalTransformComponent>(m_entity).scale;
        }
        if (m_registry->get().all_of<ecs::TransformComponent>(m_entity)) {
            return m_registry->get().get<ecs::TransformComponent>(m_entity).scale;
        }
        return glm::vec3(1.0f);
    }

    glm::vec3 Node3D::get_world_position() const {
        if (!is_valid()) return glm::vec3(0.0f);
        if (m_registry->get().all_of<ecs::TransformComponent>(m_entity)) {
            return m_registry->get().get<ecs::TransformComponent>(m_entity).position;
        }
        return get_position();
    }

    glm::vec3 Node3D::get_world_rotation() const {
        if (!is_valid()) return glm::vec3(0.0f);
        if (m_registry->get().all_of<ecs::TransformComponent>(m_entity)) {
            return m_registry->get().get<ecs::TransformComponent>(m_entity).rotation;
        }
        return get_rotation();
    }

    glm::quat Node3D::get_world_orientation() const {
        if (!is_valid()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        if (m_registry->get().all_of<ecs::TransformComponent>(m_entity)) {
            return m_registry->get().get<ecs::TransformComponent>(m_entity).orientation;
        }
        return get_orientation();
    }

    glm::vec3 Node3D::get_world_scale() const {
        if (!is_valid()) return glm::vec3(1.0f);
        if (m_registry->get().all_of<ecs::TransformComponent>(m_entity)) {
            return m_registry->get().get<ecs::TransformComponent>(m_entity).scale;
        }
        return get_scale();
    }

    glm::mat4 Node3D::get_world_matrix() const {
        if (!is_valid()) return glm::mat4(1.0f);
        glm::vec3 pos = get_world_position();
        glm::quat rot = get_world_orientation();
        glm::vec3 scl = get_world_scale();
        return glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), scl);
    }

} // namespace engine::world
