#pragma once
#include <entt/entt.hpp>
#include <string>
#include <vector>

namespace engine::world {

    class SceneTree;

    // Safe generational node path reference resistant to deletion and rebinding
    class NodePath {
    public:
        NodePath() = default;
        explicit NodePath(const std::string& path) : m_path(path) {}
        explicit NodePath(entt::entity entity, const std::string& path = "")
            : m_entity(entity), m_path(path) {}

        const std::string& get_path() const { return m_path; }
        entt::entity get_cached_entity() const { return m_entity; }

        bool empty() const { return m_path.empty() && m_entity == entt::null; }

        entt::entity resolve(SceneTree* tree) const;

    private:
        entt::entity m_entity{entt::null};
        std::string m_path;
    };

} // namespace engine::world
