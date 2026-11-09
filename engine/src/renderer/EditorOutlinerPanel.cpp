#include "renderer/EditorOutlinerPanel.hpp"
#include "renderer/EditorUiStyle.hpp"
#include "ecs/Components.hpp"
#include "core/Engine.hpp"
#include "imgui.h"

#include <algorithm>
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
                ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Hierarchia Sceny");

                auto viewAll = registry.get().view<ecs::TransformComponent>();
                ImGui::SameLine(ImGui::GetWindowWidth() - 95.0f * uiScale);
                ImGui::TextDisabled("[%zu]", viewAll.size());
                ImGui::SameLine(ImGui::GetWindowWidth() - 24.0f * uiScale);
                if (ImGui::SmallButton(">")) { showOutliner = false; showDetails = false; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zwin prawy panel inspektora");

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
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 62.0f * uiScale);
                ImGui::InputTextWithHint("##OutlinerSearch", "Filtruj scene...", outlinerFilter, sizeof(outlinerFilter));
                ImGui::SameLine();
                if (ImGui::Button("+ Spawn", ImVec2(58.0f * uiScale, 0))) {
                    auto newEnt = registry.createEntity();
                    glm::vec3 sp = cameraPosition + cameraForward * 6.0f;
                    if (sp.y < 0.5f) sp.y = 0.5f;
                    registry.get().emplace<ecs::TagComponent>(newEnt, ecs::TagComponent{"NowyAktor_" + std::to_string(static_cast<uint32_t>(newEnt))});
                    registry.get().emplace<ecs::TransformComponent>(newEnt, sp, glm::vec3(0.0f), glm::vec3(1.0f));
                    registry.get().emplace<ecs::MeshComponent>(newEnt, ecs::MeshComponent{2});
                    selectedEntity = newEnt;
                    if (engine) engine->setSelectedEntity(selectedEntity);
                }
                ImGui::Separator();

                ImGui::BeginChild("OutlinerTreeScroll", ImVec2(0, 0), false);
                auto view = registry.get().view<ecs::TransformComponent>();
                for (auto entity : view) {
                    // Category chips are functional filters, not merely visual tabs.
                    const auto& entityRegistry = registry.get();
                    switch (s_outlinerCategory) {
                    case 1: // Meshes / renderable geometry
                        if (!entityRegistry.all_of<ecs::MeshComponent>(entity)) continue;
                        break;
                    case 2: // Lights
                        if (!entityRegistry.all_of<ecs::LightComponent>(entity)) continue;
                        break;
                    case 3: // Physics / simulation
                        if (!entityRegistry.any_of<ecs::RigidBodyComponent, ecs::VelocityComponent,
                                                   ecs::ChemistryComponent, ecs::DestructibleComponent>(entity)) continue;
                        break;
                    case 4: // Player / gameplay actors
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
                        std::transform(f.begin(), f.end(), f.begin(), ::tolower);
                        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
                        if (l.find(f) == std::string::npos) continue;
                    }
                    const bool isSelected = (selectedEntity == entity);
                    ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity)));
                    if (ImGui::Selectable(label.c_str(), isSelected)) {
                        selectedEntity = entity;
                        if (engine) engine->setSelectedEntity(selectedEntity);
                    }

                    if (ImGui::BeginPopupContextItem("EntityRowCtxMenu")) {
                        selectedEntity = entity;
                        if (engine) engine->setSelectedEntity(selectedEntity);
                        ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "Encja #%u: %s", static_cast<uint32_t>(entity), label.c_str());
                        ImGui::Separator();
                        if (ImGui::MenuItem("Skup kamere [F]")) {
                            if (registry.get().all_of<ecs::TransformComponent>(entity)) {
                                const auto& tc = registry.get().get<ecs::TransformComponent>(entity);
                                if (engine) engine->setCamera(tc.position - cameraForward * 8.0f, cameraYaw, cameraPitch, cameraFov);
                            }
                        }
                        if (ImGui::MenuItem("Zresetuj pozycje (0, 0, 0)")) {
                            if (registry.get().all_of<ecs::TransformComponent>(entity)) {
                                auto& tc = registry.get().get<ecs::TransformComponent>(entity);
                                tc.position = glm::vec3(0.0f);
                                if (engine) engine->syncEntityTransformPhysics(entity);
                            }
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Usun obiekt [Delete]")) {
                            registry.get().destroy(entity);
                            if (selectedEntity == entity) selectedEntity = entt::null;
                            if (engine) engine->setSelectedEntity(entt::null);
                            ImGui::EndPopup();
                            ImGui::PopID();
                            break;
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::EndChild();
                ImGui::End();
                ImGui::PopStyleColor();
            }

}
} // namespace engine::renderer
#endif
