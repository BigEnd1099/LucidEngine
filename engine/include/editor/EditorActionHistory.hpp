#pragma once

#include "ecs/Registry.hpp"
#include "ecs/SceneSnapshot.hpp"
#include "ecs/Components.hpp"
#include <vector>
#include <optional>
#include <string>

namespace engine::core {
    class Engine;
}

namespace engine::editor {

    struct ClipboardEntity {
        std::string tag{"Actor_Copy"};
        ecs::TransformComponent transform{};
        std::optional<ecs::MeshComponent> mesh;
        std::optional<ecs::MaterialComponent> material;
        std::optional<ecs::LightComponent> light;
        std::optional<ecs::RigidBodyComponent> rigidBody;
        std::optional<ecs::ParticleEmitterComponent> particleEmitter;
    };

    class EditorActionHistory {
    public:
        static EditorActionHistory& get() {
            static EditorActionHistory instance;
            return instance;
        }

        void recordSnapshot(ecs::Registry& registry) {
            m_undoStack.push_back(ecs::SceneSnapshot::capture(registry));
            if (m_undoStack.size() > MAX_HISTORY) {
                m_undoStack.erase(m_undoStack.begin());
            }
            m_redoStack.clear();
        }

        bool canUndo() const { return !m_undoStack.empty(); }
        bool canRedo() const { return !m_redoStack.empty(); }

        bool undo(ecs::Registry& registry, physics::PhysicsContext* physics) {
            if (m_undoStack.empty()) return false;
            m_redoStack.push_back(ecs::SceneSnapshot::capture(registry));
            auto snapshot = std::move(m_undoStack.back());
            m_undoStack.pop_back();
            ecs::SceneSnapshot::restore(registry, physics, snapshot);
            return true;
        }

        bool redo(ecs::Registry& registry, physics::PhysicsContext* physics) {
            if (m_redoStack.empty()) return false;
            m_undoStack.push_back(ecs::SceneSnapshot::capture(registry));
            auto snapshot = std::move(m_redoStack.back());
            m_redoStack.pop_back();
            ecs::SceneSnapshot::restore(registry, physics, snapshot);
            return true;
        }

        void copyEntity(ecs::Registry& registry, entt::entity src) {
            if (src == entt::null || !registry.get().valid(src)) return;
            auto& reg = registry.get();

            m_clipboard = ClipboardEntity{};
            if (reg.all_of<ecs::TagComponent>(src)) {
                m_clipboard.tag = reg.get<ecs::TagComponent>(src).name;
            } else {
                m_clipboard.tag = "Actor_" + std::to_string(static_cast<uint32_t>(src));
            }

            if (reg.all_of<ecs::TransformComponent>(src)) {
                m_clipboard.transform = reg.get<ecs::TransformComponent>(src);
            }

            if (reg.all_of<ecs::MeshComponent>(src)) {
                m_clipboard.mesh = reg.get<ecs::MeshComponent>(src);
            }
            if (reg.all_of<ecs::MaterialComponent>(src)) {
                m_clipboard.material = reg.get<ecs::MaterialComponent>(src);
            }
            if (reg.all_of<ecs::LightComponent>(src)) {
                m_clipboard.light = reg.get<ecs::LightComponent>(src);
            }
            if (reg.all_of<ecs::RigidBodyComponent>(src)) {
                m_clipboard.rigidBody = reg.get<ecs::RigidBodyComponent>(src);
            }
            if (reg.all_of<ecs::ParticleEmitterComponent>(src)) {
                m_clipboard.particleEmitter = reg.get<ecs::ParticleEmitterComponent>(src);
            }

            m_hasClipboard = true;
        }

        bool hasClipboard() const { return m_hasClipboard; }

        entt::entity pasteEntity(ecs::Registry& registry, core::Engine* engine,
                                const glm::vec3& offset = glm::vec3(1.0f, 0.0f, 1.0f)) {
            (void)engine;
            if (!m_hasClipboard) return entt::null;
            recordSnapshot(registry);

            auto dst = registry.createEntity();
            auto& reg = registry.get();

            reg.emplace<ecs::TagComponent>(dst, ecs::TagComponent{m_clipboard.tag + "_Copy"});
            auto tc = m_clipboard.transform;
            tc.position += offset;
            reg.emplace<ecs::TransformComponent>(dst, tc);

            if (m_clipboard.mesh.has_value()) {
                reg.emplace<ecs::MeshComponent>(dst, *m_clipboard.mesh);
            }
            if (m_clipboard.material.has_value()) {
                reg.emplace<ecs::MaterialComponent>(dst, *m_clipboard.material);
            }
            if (m_clipboard.light.has_value()) {
                reg.emplace<ecs::LightComponent>(dst, *m_clipboard.light);
            }
            if (m_clipboard.particleEmitter.has_value()) {
                reg.emplace<ecs::ParticleEmitterComponent>(dst, *m_clipboard.particleEmitter);
            }
            if (m_clipboard.rigidBody.has_value()) {
                auto rb = *m_clipboard.rigidBody;
                rb.bodyId = 0xFFFFFFFF;
                reg.emplace<ecs::RigidBodyComponent>(dst, rb);
            }

            return dst;
        }

        void clearHistory() {
            m_undoStack.clear();
            m_redoStack.clear();
        }

    private:
        EditorActionHistory() = default;
        static constexpr size_t MAX_HISTORY = 64;
        std::vector<std::vector<ecs::EntityBinaryRecord>> m_undoStack;
        std::vector<std::vector<ecs::EntityBinaryRecord>> m_redoStack;
        ClipboardEntity m_clipboard;
        bool m_hasClipboard{false};
    };

} // namespace engine::editor
