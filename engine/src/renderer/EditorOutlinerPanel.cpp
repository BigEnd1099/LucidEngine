#include "renderer/EditorOutlinerPanel.hpp"
#include "renderer/EditorLocalization.hpp"
#include "renderer/EditorUiStyle.hpp"
#include "ecs/Components.hpp"
#include "core/Engine.hpp"
#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <functional>
#include <cctype>

#ifdef USE_GLFW
namespace engine::renderer {

void drawEditorOutlinerPanel(ecs::Registry& registry,
                             core::Engine* engine,
                             entt::entity& selectedEntity,
                             bool immersivePlay,
                             bool& showOutliner,
                             bool& showDetails,
                             float uiScale,
                             float leftPanelW,
                             float centerViewportW,
                             float rightPanelW,
                             float topTotalH,
                             float outlinerH,
                             const glm::vec3& cameraPosition,
                             const glm::vec3& cameraForward,
                             float cameraYaw,
                             float cameraPitch,
                             float cameraFov) {
    if (!immersivePlay && showOutliner) {
        ImGui::SetNextWindowPos(ImVec2(leftPanelW + centerViewportW, topTotalH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(rightPanelW, outlinerH), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.055f, 0.086f, 1.0f));
        ImGui::Begin("OutlinerPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        const bool outlinerHasKeyboardFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        ImDrawList* odl = ImGui::GetWindowDrawList();
        ImVec2 oHdrPos = ImGui::GetCursorScreenPos();
        drawEditorVectorIcon(odl, ImVec2(oHdrPos.x + 8.0f * uiScale, oHdrPos.y + 9.0f * uiScale), 5.5f * uiScale, VectorIconType::Folder, IM_COL32(245, 158, 11, 255));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f * uiScale);
        ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), editorText("ui.scene_outliner", "Scene Outliner"));

        auto viewAll = registry.get().view<ecs::TransformComponent>();
        ImGui::SameLine(ImGui::GetWindowWidth() - 95.0f * uiScale);
        ImGui::TextDisabled("[%zu]", viewAll.size());
        ImGui::SameLine(ImGui::GetWindowWidth() - 24.0f * uiScale);
        if (ImGui::SmallButton(">")) { showOutliner = false; showDetails = false; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("ui.collapse_the_inspector_panel", "Collapse the inspector panel"));

        static int s_outlinerCategory = 0; // 0: Wszystko, 1: Siatki, 2: Swiatla, 3: Fizyka, 4: Gracz
        auto drawFilterChip = [&](const char* name, int cIdx) {
            const bool active = (s_outlinerCategory == cIdx);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.44f, 0.95f, 1.0f));
            else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
            if (ImGui::SmallButton(name)) s_outlinerCategory = cIdx;
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 3.0f * uiScale);
        };
        drawFilterChip("Wszystko", 0);
        drawFilterChip("Siatki", 1);
        drawFilterChip("Swiatla", 2);
        drawFilterChip("Fizyka", 3);
        drawFilterChip("Gracz", 4);
        ImGui::NewLine();

        static char outlinerFilter[64] = "";
        const bool hasFilter = (outlinerFilter[0] != '\0');
        const float searchExtraW = (hasFilter ? 84.0f : 62.0f) * uiScale;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - searchExtraW);
        ImGui::InputTextWithHint("##OutlinerSearch", editorText("ui.search_scene", "Search scene..."), outlinerFilter, sizeof(outlinerFilter));
        if (hasFilter) {
            ImGui::SameLine();
            if (ImGui::SmallButton("x##ClearOutlinerFilter")) {
                outlinerFilter[0] = '\0';
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("ui.clear_filter", "Clear filter"));
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(engine && engine->isSimulationSessionActive());
        if (ImGui::Button(editorText("ui.spawn", "+ Spawn"), ImVec2(58.0f * uiScale, 0))) {
            auto newEnt = registry.createEntity();
            glm::vec3 sp = cameraPosition + cameraForward * 6.0f;
            if (sp.y < 0.5f) sp.y = 0.5f;
            registry.get().emplace<ecs::TagComponent>(newEnt, ecs::TagComponent{"NowyAktor_" + std::to_string(static_cast<uint32_t>(newEnt))});
            registry.get().emplace<ecs::TransformComponent>(newEnt, sp, glm::vec3(0.0f), glm::vec3(1.0f));
            registry.get().emplace<ecs::MeshComponent>(newEnt, ecs::MeshComponent{2});
            selectedEntity = newEnt;
            if (engine) engine->setSelectedEntity(selectedEntity);
        }
        ImGui::EndDisabled();
        ImGui::Separator();

        // Blender duplicate entity helper
        auto duplicateEntity = [&](entt::entity src) -> entt::entity {
            if (src == entt::null || !registry.get().valid(src)) return entt::null;
            auto dst = registry.createEntity();
            std::string srcTag = "Actor";
            if (registry.get().all_of<ecs::TagComponent>(src)) {
                srcTag = registry.get().get<ecs::TagComponent>(src).name;
            }
            registry.get().emplace<ecs::TagComponent>(dst, ecs::TagComponent{srcTag + "_Kopia"});
            if (registry.get().all_of<ecs::TransformComponent>(src)) {
                auto tc = registry.get().get<ecs::TransformComponent>(src);
                tc.position += glm::vec3(1.0f, 0.0f, 1.0f);
                registry.get().emplace<ecs::TransformComponent>(dst, tc);
            }
            if (registry.get().all_of<ecs::MeshComponent>(src)) {
                registry.get().emplace<ecs::MeshComponent>(dst, registry.get().get<ecs::MeshComponent>(src));
            }
            if (registry.get().all_of<ecs::MaterialComponent>(src)) {
                registry.get().emplace<ecs::MaterialComponent>(dst, registry.get().get<ecs::MaterialComponent>(src));
            }
            if (registry.get().all_of<ecs::LightComponent>(src)) {
                registry.get().emplace<ecs::LightComponent>(dst, registry.get().get<ecs::LightComponent>(src));
            }
            if (registry.get().all_of<ecs::RigidBodyComponent>(src)) {
                auto rb = registry.get().get<ecs::RigidBodyComponent>(src);
                rb.bodyId = 0xFFFFFFFF;
                registry.get().emplace<ecs::RigidBodyComponent>(dst, rb);
                if (engine) engine->syncEntityTransformPhysics(dst);
            }
            if (registry.get().all_of<ecs::ParticleEmitterComponent>(src)) {
                registry.get().emplace<ecs::ParticleEmitterComponent>(dst, registry.get().get<ecs::ParticleEmitterComponent>(src));
            }
            return dst;
        };

        // Hotkey [Ctrl+D] Duplikuj zaznaczony obiekt
        if (!(engine && engine->isSimulationSessionActive()) &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput &&
            !ImGui::IsAnyItemActive() &&
            ImGui::IsKeyPressed(ImGuiKey_D) && ImGui::GetIO().KeyCtrl &&
            selectedEntity != entt::null && registry.get().valid(selectedEntity)) {
            selectedEntity = duplicateEntity(selectedEntity);
            if (engine) engine->setSelectedEntity(selectedEntity);
        }

        ImGui::BeginChild("OutlinerTreeScroll", ImVec2(0, 0), false);
        auto view = registry.get().view<ecs::TransformComponent>();
        int visibleEntityCount = 0;
        auto entityLabel = [&](entt::entity entity) {
            std::string label = "Entity_" + std::to_string(static_cast<uint32_t>(entity));
            if (registry.get().all_of<ecs::TagComponent>(entity))
                label = registry.get().get<ecs::TagComponent>(entity).name;
            return label;
        };
        auto categoryMatches = [&](entt::entity entity) {
            const auto& r = registry.get();
            switch (s_outlinerCategory) {
            case 1: return r.all_of<ecs::MeshComponent>(entity);
            case 2: return r.all_of<ecs::LightComponent>(entity);
            case 3: return r.any_of<ecs::RigidBodyComponent, ecs::VelocityComponent,
                                    ecs::ChemistryComponent, ecs::DestructibleComponent>(entity);
            case 4: return r.any_of<ecs::CharacterMovementComponent, ecs::CameraBoomComponent,
                                    ecs::NavAgentComponent, ecs::IKFootPlacementComponent>(entity);
            default: return true;
            }
        };
        auto textMatches = [&](entt::entity entity) {
            if (outlinerFilter[0] == '\0') return true;
            std::string filter = outlinerFilter;
            std::string label = entityLabel(entity);
            const auto toLower = [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); };
            std::transform(filter.begin(), filter.end(), filter.begin(), toLower);
            std::transform(label.begin(), label.end(), label.begin(), toLower);
            return label.find(filter) != std::string::npos;
        };
        std::function<bool(entt::entity, int)> hasVisibleDescendant =
            [&](entt::entity entity, int depth) -> bool {
                if (depth > 256 || !registry.get().valid(entity) ||
                    !registry.get().all_of<ecs::HierarchyComponent>(entity)) return false;
                const auto children = registry.get().get<ecs::HierarchyComponent>(entity).children;
                for (entt::entity child : children) {
                    if (!registry.get().valid(child) ||
                        !registry.get().all_of<ecs::TransformComponent>(child)) continue;
                    if ((categoryMatches(child) && textMatches(child)) ||
                        hasVisibleDescendant(child, depth + 1)) return true;
                }
                return false;
            };

        std::function<void(entt::entity, int)> drawEntityNode =
            [&](entt::entity entity, int depth) {
                if (depth > 256 || !registry.get().valid(entity) ||
                    !registry.get().all_of<ecs::TransformComponent>(entity)) return;

                const bool ownMatch = categoryMatches(entity) && textMatches(entity);
                const bool childMatch = hasVisibleDescendant(entity, depth);
                if (!ownMatch && !childMatch) return;

                const bool hasChildren = registry.get().all_of<ecs::HierarchyComponent>(entity) &&
                    !registry.get().get<ecs::HierarchyComponent>(entity).children.empty();
                const bool hasMesh = registry.get().all_of<ecs::MeshComponent>(entity);
                const bool isVisible = hasMesh ? registry.get().get<ecs::MeshComponent>(entity).visible : true;
                const bool isSelected = selectedEntity == entity;
                const std::string label = entityLabel(entity);
                ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity)));

                const ImVec2 eyePos = ImGui::GetCursorScreenPos();
                const float eyeSize = 16.0f * uiScale;
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.25f, 0.35f, 0.4f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.15f, 0.25f, 0.6f));
                ImGui::BeginDisabled(engine && engine->isSimulationSessionActive());
                if (ImGui::Button("##Visibility", ImVec2(eyeSize, eyeSize)) && hasMesh)
                    registry.get().get<ecs::MeshComponent>(entity).visible = !isVisible;
                ImGui::EndDisabled();
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(isVisible ? "Visible in viewport (click to hide)" : "Hidden in viewport (click to show)");
                drawEditorVectorIcon(ImGui::GetWindowDrawList(),
                    ImVec2(eyePos.x + eyeSize * 0.5f, eyePos.y + eyeSize * 0.5f),
                    4.2f * uiScale, isVisible ? VectorIconType::Eye : VectorIconType::EyeClosed,
                    isVisible ? IM_COL32(200, 215, 235, 240) : IM_COL32(110, 115, 125, 150));
                ImGui::SameLine(0.0f, 3.0f * uiScale);

                VectorIconType typeIcon = VectorIconType::Shapes;
                ImU32 iconColor = IM_COL32(160, 175, 195, 230);
                if (registry.get().all_of<ecs::LightComponent>(entity)) {
                    typeIcon = VectorIconType::Light; iconColor = IM_COL32(251, 191, 36, 255);
                } else if (registry.get().all_of<ecs::ParticleEmitterComponent>(entity)) {
                    typeIcon = VectorIconType::Sparkles; iconColor = IM_COL32(236, 72, 153, 255);
                } else if (registry.get().all_of<ecs::CharacterMovementComponent>(entity)) {
                    typeIcon = VectorIconType::Character; iconColor = IM_COL32(34, 197, 94, 255);
                } else if (hasMesh) {
                    typeIcon = VectorIconType::Cube; iconColor = IM_COL32(56, 189, 248, 255);
                }
                const ImVec2 iconPos = ImGui::GetCursorScreenPos();
                drawEditorVectorIcon(ImGui::GetWindowDrawList(),
                    ImVec2(iconPos.x + 8.0f * uiScale, iconPos.y + 9.0f * uiScale),
                    4.5f * uiScale, typeIcon, iconColor);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f * uiScale);

                ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
                if (isSelected) treeFlags |= ImGuiTreeNodeFlags_Selected;
                if (!hasChildren) treeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (childMatch && outlinerFilter[0] != '\0') ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                const bool opened = ImGui::TreeNodeEx(label.c_str(), treeFlags);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
                    selectedEntity = entity;
                    if (engine) engine->setSelectedEntity(entity);
                }

                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("LUCID_ENTITY_HIERARCHY", &entity, sizeof(entity));
                    ImGui::TextUnformatted(label.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LUCID_ENTITY_HIERARCHY")) {
                        if (payload->Data && payload->DataSize == sizeof(entt::entity) &&
                            !(engine && engine->isSimulationSessionActive())) {
                            const entt::entity draggedEntity = *static_cast<const entt::entity*>(payload->Data);
                            if (registry.reparentEntity(draggedEntity, entity))
                                registry.updateWorldTransforms();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (ImGui::BeginPopupContextItem("EntityRowCtxMenu")) {
                    selectedEntity = entity;
                    if (engine) engine->setSelectedEntity(entity);
                    ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f),
                        "Entity #%u: %s", static_cast<uint32_t>(entity), label.c_str());
                    ImGui::Separator();
                    if (ImGui::MenuItem("Focus camera [F]")) {
                        const auto& transform = registry.get().get<ecs::TransformComponent>(entity);
                        if (engine) engine->setCamera(transform.position -
                            cameraForward * std::max(5.0f, glm::length(transform.scale) * 3.5f),
                            cameraYaw, cameraPitch, cameraFov);
                    }
                    ImGui::BeginDisabled(engine && engine->isSimulationSessionActive());
                    if (ImGui::MenuItem("Reset Position")) {
                        if (auto* local = registry.get().try_get<ecs::LocalTransformComponent>(entity))
                            local->position = glm::vec3(0.0f);
                        else
                            registry.get().get<ecs::TransformComponent>(entity).position = glm::vec3(0.0f);
                        registry.updateWorldTransforms();
                        if (engine) engine->syncEntityTransformPhysics(entity);
                    }
                    if (ImGui::MenuItem("Duplicate Object [Ctrl+D]")) {
                        selectedEntity = duplicateEntity(entity);
                        if (engine) engine->setSelectedEntity(selectedEntity);
                    }
                    if (ImGui::MenuItem("Delete Object [Delete]")) {
                        if (engine) engine->destroyEntity(static_cast<uint32_t>(entity));
                        else registry.destroyEntity(entity);
                        if (selectedEntity == entity) selectedEntity = entt::null;
                        if (engine) engine->setSelectedEntity(entt::null);
                        ImGui::EndDisabled();
                        ImGui::EndPopup();
                        ImGui::PopID();
                        return;
                    }
                    ImGui::EndDisabled();
                    ImGui::EndPopup();
                }

                if (hasChildren && opened) {
                    const auto children = registry.get().get<ecs::HierarchyComponent>(entity).children;
                    for (entt::entity child : children) drawEntityNode(child, depth + 1);
                    ImGui::TreePop();
                }
                if (ownMatch) ++visibleEntityCount;
                ImGui::PopID();
            };

        for (auto entity : view) {
            const bool hasParent = registry.get().all_of<ecs::HierarchyComponent>(entity) &&
                registry.get().get<ecs::HierarchyComponent>(entity).parent != entt::null &&
                registry.get().valid(registry.get().get<ecs::HierarchyComponent>(entity).parent);
            if (!hasParent) drawEntityNode(entity, 0);
        }
        if (visibleEntityCount == 0) {
            ImGui::Spacing();
            ImGui::TextDisabled(view.empty() ? editorText("ui.scene_is_empty", "Scene is empty.") : editorText("ui.no_entities_match_the_current_filters", "No entities match the current filters."));
            if (!view.empty() && hasFilter && ImGui::SmallButton(editorText("ui.clear_search_filter", "Clear search filter"))) {
                outlinerFilter[0] = 0;
            }
        }
        ImGui::EndChild();

        // Delete follows the Outliner selection, not the scene viewport hover state.
        if (outlinerHasKeyboardFocus &&
            !ImGui::GetIO().WantTextInput &&
            !ImGui::IsAnyItemActive() &&
            ImGui::IsKeyPressed(ImGuiKey_Delete) &&
            !(engine && engine->isSimulationSessionActive()) &&
            selectedEntity != entt::null &&
            registry.get().valid(selectedEntity)) {
            const entt::entity entityToDelete = selectedEntity;
            if (engine) engine->destroyEntity(static_cast<uint32_t>(entityToDelete));
            else registry.destroyEntity(entityToDelete);
            selectedEntity = entt::null;
            if (engine) engine->setSelectedEntity(entt::null);
        }

        ImGui::End();
        ImGui::PopStyleColor();
    }
}

} // namespace engine::renderer
#endif
