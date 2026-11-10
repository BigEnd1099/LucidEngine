#include "renderer/EditorOutlinerPanel.hpp"
#include "renderer/EditorLocalization.hpp"
#include "renderer/EditorUiStyle.hpp"
#include "ecs/Components.hpp"
#include "core/Engine.hpp"
#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>

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
            ImGui::IsKeyPressed(ImGuiKey_D) && ImGui::GetIO().KeyCtrl &&
            selectedEntity != entt::null && registry.get().valid(selectedEntity)) {
            selectedEntity = duplicateEntity(selectedEntity);
            if (engine) engine->setSelectedEntity(selectedEntity);
        }

        ImGui::BeginChild("OutlinerTreeScroll", ImVec2(0, 0), false);
        auto view = registry.get().view<ecs::TransformComponent>();
        int visibleEntityCount = 0;
        for (auto entity : view) {
            const auto& entityRegistry = registry.get();
            switch (s_outlinerCategory) {
            case 1:
                if (!entityRegistry.all_of<ecs::MeshComponent>(entity)) continue;
                break;
            case 2:
                if (!entityRegistry.all_of<ecs::LightComponent>(entity)) continue;
                break;
            case 3:
                if (!entityRegistry.any_of<ecs::RigidBodyComponent, ecs::VelocityComponent,
                                           ecs::ChemistryComponent, ecs::DestructibleComponent>(entity)) continue;
                break;
            case 4:
                if (!entityRegistry.any_of<ecs::CharacterMovementComponent, ecs::CameraBoomComponent,
                                           ecs::NavAgentComponent, ecs::IKFootPlacementComponent>(entity)) continue;
                break;
            default:
                break;
            }

            std::string label = "Entity_" + std::to_string(static_cast<uint32_t>(entity));
            if (registry.get().all_of<ecs::TagComponent>(entity)) {
                label = registry.get().get<ecs::TagComponent>(entity).name;
            }
            if (outlinerFilter[0] != '\0') {
                std::string f = outlinerFilter, l = label;
                const auto toLower = [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); };
                std::transform(f.begin(), f.end(), f.begin(), toLower);
                std::transform(l.begin(), l.end(), l.begin(), toLower);
                if (l.find(f) == std::string::npos) continue;
            }
            ++visibleEntityCount;
            const bool isSelected = (selectedEntity == entity);
            ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity)));

            // 1. Blender-style Eye (Visibility) Toggle
            const bool hasMesh = registry.get().all_of<ecs::MeshComponent>(entity);
            const bool isVis = hasMesh ? registry.get().get<ecs::MeshComponent>(entity).visible : true;
            ImVec2 eyeP = ImGui::GetCursorScreenPos();
            const float eyeSz = 16.0f * uiScale;
            char eyeBtnId[32];
            std::snprintf(eyeBtnId, sizeof(eyeBtnId), "##Eye_%u", static_cast<uint32_t>(entity));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.25f, 0.35f, 0.4f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.15f, 0.25f, 0.6f));
            ImGui::BeginDisabled(engine && engine->isSimulationSessionActive());
            if (ImGui::Button(eyeBtnId, ImVec2(eyeSz, eyeSz))) {
                if (hasMesh) {
                    auto& mc = registry.get().get<ecs::MeshComponent>(entity);
                    mc.visible = !mc.visible;
                }
            }
            ImGui::PopStyleColor(3);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(isVis ? editorText("ui.visible_in_the_3d_viewport_click_to_hide", "Visible in the 3D viewport (click to hide)") : editorText("ui.hidden_in_the_3d_viewport_click_to_show", "Hidden in the 3D viewport (click to show)"));
            }
            const ImU32 eyeCol = isVis ? IM_COL32(200, 215, 235, 240) : IM_COL32(110, 115, 125, 150);
            drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(eyeP.x + eyeSz * 0.5f, eyeP.y + eyeSz * 0.5f), 4.2f * uiScale, isVis ? VectorIconType::Eye : VectorIconType::EyeClosed, eyeCol);
            ImGui::SameLine(0.0f, 3.0f * uiScale);

            // 2. Entity Type Vector Icon
            VectorIconType typeIcon = VectorIconType::Shapes;
            ImU32 iconCol = IM_COL32(160, 175, 195, 230);
            if (registry.get().all_of<ecs::LightComponent>(entity)) {
                typeIcon = VectorIconType::Light;
                iconCol = IM_COL32(251, 191, 36, 255);
            } else if (registry.get().all_of<ecs::ParticleEmitterComponent>(entity)) {
                typeIcon = VectorIconType::Sparkles;
                iconCol = IM_COL32(236, 72, 153, 255);
            } else if (registry.get().all_of<ecs::CharacterMovementComponent>(entity)) {
                typeIcon = VectorIconType::Character;
                iconCol = IM_COL32(34, 197, 94, 255);
            } else if (hasMesh) {
                typeIcon = VectorIconType::Cube;
                iconCol = IM_COL32(56, 189, 248, 255);
            }
            ImVec2 icPos = ImGui::GetCursorScreenPos();
            drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(icPos.x + 8.0f * uiScale, icPos.y + 9.0f * uiScale), 4.5f * uiScale, typeIcon, iconCol);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f * uiScale);

            // 3. Entity Row Selectable
            if (ImGui::Selectable(label.c_str(), isSelected)) {
                selectedEntity = entity;
                if (engine) engine->setSelectedEntity(selectedEntity);
            }

            // 4. Context Menu
            if (ImGui::BeginPopupContextItem("EntityRowCtxMenu")) {
                selectedEntity = entity;
                if (engine) engine->setSelectedEntity(selectedEntity);
                ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "Encja #%u: %s", static_cast<uint32_t>(entity), label.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("Skup kamere [F]")) {
                    if (registry.get().all_of<ecs::TransformComponent>(entity)) {
                        const auto& tc = registry.get().get<ecs::TransformComponent>(entity);
                        if (engine) engine->setCamera(tc.position - cameraForward * std::max(5.0f, glm::length(tc.scale) * 3.5f), cameraYaw, cameraPitch, cameraFov);
                    }
                }
                ImGui::BeginDisabled(engine && engine->isSimulationSessionActive());
                if (ImGui::MenuItem(editorText("ui.reset_position_context", "Reset Position (0, 0, 0)"))) {
                    if (registry.get().all_of<ecs::TransformComponent>(entity)) {
                        auto& tc = registry.get().get<ecs::TransformComponent>(entity);
                        tc.position = glm::vec3(0.0f);
                        if (engine) engine->syncEntityTransformPhysics(entity);
                    }
                }
                if (ImGui::MenuItem(editorText("ui.duplicate_entity_context", "Duplicate Object [Ctrl+D]"))) {
                    selectedEntity = duplicateEntity(entity);
                    if (engine) engine->setSelectedEntity(selectedEntity);
                }
                ImGui::Separator();
                ImGui::EndDisabled();
                ImGui::BeginDisabled(engine && engine->isSimulationSessionActive());
                if (ImGui::MenuItem(editorText("ui.delete_entity_context", "Delete Object [Delete]"))) {
                    if (engine) engine->destroyEntity(static_cast<uint32_t>(entity)); else registry.get().destroy(entity);
                    if (selectedEntity == entity) selectedEntity = entt::null;
                    if (engine) engine->setSelectedEntity(entt::null);
                    ImGui::EndPopup();
                    ImGui::PopID();
                    break;
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (visibleEntityCount == 0) {
            ImGui::Spacing();
            ImGui::TextDisabled(view.empty() ? editorText("ui.scene_is_empty", "Scene is empty.") : editorText("ui.no_entities_match_the_current_filters", "No entities match the current filters."));
            if (!view.empty() && hasFilter && ImGui::SmallButton(editorText("ui.clear_search_filter", "Clear search filter"))) {
                outlinerFilter[0] = 0;
            }
        }
        ImGui::EndChild();
        ImGui::End();
        ImGui::PopStyleColor();
    }
}

} // namespace engine::renderer
#endif
