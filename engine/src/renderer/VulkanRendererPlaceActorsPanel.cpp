#include "renderer/VulkanRenderer.hpp"
#include "renderer/EditorUiStyle.hpp"
#include "renderer/EditorLocalization.hpp"
#include "core/Engine.hpp"
#include "ecs/Components.hpp"
#include "imgui.h"

#include <algorithm>
#include <string>
#include <cctype>

#ifdef USE_GLFW
namespace engine::renderer {

void VulkanRenderer::drawEditorPlaceActorsPanel(ecs::Registry& registry, core::Engine* engine,
                                                bool immersivePlay, bool& showPlaceActors, bool& showDetails,
                                                float uiScale, float topTotalH, float contentH,
                                                float leftPanelW, const glm::vec3& cameraPosition,
                                                const glm::vec3& cameraForward,
                                                entt::entity& selectedEntity) {
            // 4A. FAR-LEFT TOOL DOCK & 4B. PLACE ACTORS PALETTE
            const float toolDockW = (!immersivePlay && showPlaceActors) ? (36.0f * uiScale) : 0.0f;
            if (!immersivePlay && showPlaceActors) {
                const bool simulationSessionActive = engine && engine->isSimulationSessionActive();
                ImGui::SetNextWindowPos(ImVec2(0, topTotalH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(toolDockW, contentH), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.055f, 0.075f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f * uiScale, 8.0f * uiScale));
                ImGui::Begin("FarLeftToolDock", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
                ImGui::BeginDisabled(simulationSessionActive);

                static int s_activeToolMode = 1;
                const VectorIconType dockIcons[6] = {
                    VectorIconType::Pointer, VectorIconType::Cube, VectorIconType::Mountain,
                    VectorIconType::Leaf, VectorIconType::Brush, VectorIconType::Gear
                };
                const float itemSz = 26.0f * uiScale;
                for (int i = 0; i < 6; ++i) {
                    ImGui::PushID(200 + i);
                    const bool active = (s_activeToolMode == i);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.0f, 0.44f, 0.95f, 1.0f) : ImVec4(0.086f, 0.106f, 0.133f, 1.0f));
                    if (ImGui::Button("##DockBtn", ImVec2(itemSz, itemSz))) {
                        s_activeToolMode = i;
                        if (i == 5) { m_detailsTab = 1; showDetails = true; }
                    }
                    ImGui::PopStyleColor();
                    drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + itemSz * 0.5f, p.y + itemSz * 0.5f), 7.5f * uiScale, dockIcons[i], IM_COL32(235, 242, 255, 240));
                    ImGui::PopID();
                    ImGui::Spacing();
                }
                ImGui::EndDisabled();
                ImGui::End();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();

                const float placePanelW = leftPanelW - toolDockW;
                ImGui::SetNextWindowPos(ImVec2(toolDockW, topTotalH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(placePanelW, contentH), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.051f, 0.067f, 0.090f, 1.0f));
                ImGui::Begin("PlaceActorsPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
                ImGui::BeginDisabled(simulationSessionActive);

                ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), editorText("ui.place_actors", "Place Actors"));
                ImGui::SameLine(placePanelW - 22.0f * uiScale);
                if (ImGui::SmallButton("<")) showPlaceActors = false;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("ui.collapse_place_actors_panel", "Collapse Place Actors panel"));
                static char classSearch[64] = "";
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##SearchClasses", editorText("ui.search_classes", "Search Classes..."), classSearch, sizeof(classSearch));
                ImGui::Separator();

                int matchedCards = 0;
                auto spawnCard = [&](const char* title, const char* desc, int spawnType, VectorIconType iconType, ImU32 iconCol) {
                    if (classSearch[0] != '\0') {
                        std::string tL = title, sL = classSearch;
                        const auto toLower = [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); };
                        std::transform(tL.begin(), tL.end(), tL.begin(), toLower);
                        std::transform(sL.begin(), sL.end(), sL.begin(), toLower);
                        if (tL.find(sL) == std::string::npos) return;
                    }
                    ++matchedCards;
                    ImGui::PushID(title);
                    const float cardH = 38.0f * uiScale;
                    ImGui::BeginChild(title, ImVec2(0, cardH), true, ImGuiWindowFlags_NoScrollbar);
                    ImDrawList* cdl = ImGui::GetWindowDrawList();
                    ImVec2 cPos = ImGui::GetWindowPos();
                    const float tileSz = 26.0f * uiScale;
                    ImVec2 tileMin(cPos.x + 5.0f * uiScale, cPos.y + (cardH - tileSz) * 0.5f);
                    cdl->AddRectFilled(tileMin, ImVec2(tileMin.x + tileSz, tileMin.y + tileSz), IM_COL32(18, 24, 34, 255), 4.0f);
                    drawEditorVectorIcon(cdl, ImVec2(tileMin.x + tileSz * 0.5f, tileMin.y + tileSz * 0.5f), 6.5f * uiScale, iconType, iconCol);

                    ImGui::SetCursorPos(ImVec2(36.0f * uiScale, 3.5f * uiScale));
                    ImGui::TextColored(ImVec4(0.96f, 0.97f, 1.0f, 1.0f), "%s", title);
                    ImGui::SetCursorPos(ImVec2(36.0f * uiScale, 19.0f * uiScale));
                    ImGui::TextColored(ImVec4(0.58f, 0.64f, 0.74f, 1.0f), "%s", desc);
                    ImGui::EndChild();
                    const bool clicked = ImGui::IsItemClicked();
                    ImGui::PopID();

                    if (clicked) {
                        auto entity = registry.createEntity();
                        glm::vec3 spawnPos = cameraPosition + cameraForward * 7.0f;
                        if (spawnPos.y < 0.5f) spawnPos.y = 0.5f;

                        if (spawnType == 0) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"PlatformBlock"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos, glm::vec3(0.0f), glm::vec3(2.0f, 0.8f, 2.0f));
                            registry.get().emplace<ecs::MeshComponent>(entity, ecs::MeshComponent{2});
                            ecs::RigidBodyComponent rb; rb.mass = 0.0f; rb.shapeType = 0;
                            registry.get().emplace<ecs::RigidBodyComponent>(entity, rb);
                            ecs::MaterialComponent mat; mat.baseColor = glm::vec4(0.2f, 0.45f, 0.75f, 1.0f); mat.roughness = 0.45f;
                            registry.get().emplace<ecs::MaterialComponent>(entity, mat);
                        } else if (spawnType == 1) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"Player"});
                            registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(spawnPos.x, std::max(2.0f, spawnPos.y), spawnPos.z));
                            registry.get().emplace<ecs::MeshComponent>(entity, ecs::MeshComponent{5});
                            ecs::RigidBodyComponent rb; rb.mass = 75.0f; rb.shapeType = 2; rb.capsuleRadius = 0.5f; rb.capsuleHalfHeight = 0.9f;
                            rb.lockRotationX = true; rb.lockRotationZ = true;
                            registry.get().emplace<ecs::RigidBodyComponent>(entity, rb);
                            ecs::MaterialComponent mat; mat.baseColor = glm::vec4(0.1f, 0.75f, 1.0f, 1.0f); mat.metallic = 0.25f; mat.roughness = 0.25f;
                            registry.get().emplace<ecs::MaterialComponent>(entity, mat);
                            ecs::CharacterMovementComponent cm; cm.maxWalkSpeed = 8.0f; cm.jumpZVelocity = 10.0f;
                            registry.get().emplace<ecs::CharacterMovementComponent>(entity, cm);
                            ecs::CameraBoomComponent boom; boom.targetArmLength = 8.0f; boom.socketOffsetY = 2.5f; boom.cameraLag = true; boom.isFirstPerson = false;
                            registry.get().emplace<ecs::CameraBoomComponent>(entity, boom);
                            if (engine) {
                                engine->syncEntityTransformPhysics(entity);
                                if (!engine->isPaused()) {
                                    engine->setCameraMode("thirdPerson");
                                }
                            }
                        } else if (spawnType == 2) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"Player"});
                            registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(spawnPos.x, std::max(2.0f, spawnPos.y), spawnPos.z));
                            registry.get().emplace<ecs::MeshComponent>(entity, ecs::MeshComponent{5});
                            ecs::RigidBodyComponent rb; rb.mass = 75.0f; rb.shapeType = 2; rb.capsuleRadius = 0.5f; rb.capsuleHalfHeight = 0.9f;
                            rb.lockRotationX = true; rb.lockRotationZ = true;
                            registry.get().emplace<ecs::RigidBodyComponent>(entity, rb);
                            ecs::MaterialComponent mat; mat.baseColor = glm::vec4(0.1f, 0.75f, 1.0f, 1.0f); mat.metallic = 0.25f; mat.roughness = 0.25f;
                            registry.get().emplace<ecs::MaterialComponent>(entity, mat);
                            ecs::CharacterMovementComponent cm; cm.maxWalkSpeed = 8.0f; cm.jumpZVelocity = 10.0f;
                            registry.get().emplace<ecs::CharacterMovementComponent>(entity, cm);
                            ecs::CameraBoomComponent boom; boom.targetArmLength = 0.0f; boom.socketOffsetY = 1.65f; boom.cameraLag = false; boom.isFirstPerson = true;
                            registry.get().emplace<ecs::CameraBoomComponent>(entity, boom);
                            if (engine) {
                                engine->syncEntityTransformPhysics(entity);
                                if (!engine->isPaused()) {
                                    engine->setCameraMode("firstPerson");
                                }
                            }
                        } else if (spawnType == 3) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"PointLight"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos + glm::vec3(0.0f, 2.5f, 0.0f));
                            registry.get().emplace<ecs::LightComponent>(entity, ecs::LightComponent{"point", glm::vec3(1.0f, 0.95f, 0.8f), 4.0f, 20.0f});
                        } else if (spawnType == 7) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"ChromeSphere"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos + glm::vec3(0.0f, 2.0f, 0.0f));
                            registry.get().emplace<ecs::MeshComponent>(entity, ecs::MeshComponent{3});
                            ecs::RigidBodyComponent rb; rb.mass = 1.0f; rb.shapeType = 1; rb.restitution = 0.75f;
                            registry.get().emplace<ecs::RigidBodyComponent>(entity, rb);
                            ecs::MaterialComponent mat; mat.baseColor = glm::vec4(0.95f, 0.82f, 0.55f, 1.0f); mat.metallic = 1.0f; mat.roughness = 0.08f;
                            registry.get().emplace<ecs::MaterialComponent>(entity, mat);
                        } else if (spawnType == 8) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"FireVFX"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos);
                            registry.get().emplace<ecs::ParticleEmitterComponent>(entity, ecs::ParticleEmitterComponent{"fire", glm::vec3(1.0f, 0.4f, 0.0f), 200, 3.0f, 0.4f});
                        } else if (spawnType == 9) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"DirectionalLight (Sun)"});
                            registry.get().emplace<ecs::TransformComponent>(entity, cameraPosition + glm::vec3(0.0f, 12.0f, 0.0f), glm::vec3(-45.0f, -40.0f, 0.0f), glm::vec3(1.0f));
                            registry.get().emplace<ecs::LightComponent>(entity, ecs::LightComponent{"directional", glm::vec3(1.0f, 0.98f, 0.92f), 3.5f, 0.0f});
                        } else if (spawnType == 10) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"SpotLight"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos + glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(-60.0f, 0.0f, 0.0f), glm::vec3(1.0f));
                            registry.get().emplace<ecs::LightComponent>(entity, ecs::LightComponent{"spot", glm::vec3(1.0f, 0.95f, 0.85f), 5.0f, 25.0f, 25.0f, 40.0f, 2.0f, 2.0f});
                        } else if (spawnType == 11) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"AreaLight"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos + glm::vec3(0.0f, 3.5f, 0.0f), glm::vec3(-45.0f, 0.0f, 0.0f), glm::vec3(1.0f));
                            registry.get().emplace<ecs::LightComponent>(entity, ecs::LightComponent{"area", glm::vec3(1.0f, 0.98f, 0.95f), 6.0f, 20.0f, 25.0f, 40.0f, 3.0f, 2.0f});
                        }
                        selectedEntity = entity;
                        if (engine) engine->setSelectedEntity(selectedEntity);
                    }
                };

                ImGui::BeginChild("PlaceActorListScroll", ImVec2(0, 0), false);
                spawnCard("Platform Cube",     "Static Jolt Box Actor",            0,  VectorIconType::Cube,      IM_COL32(148, 163, 184, 255));
                spawnCard("PBR Sphere",        "Metallic GGX / IBL Test Sphere",   7,  VectorIconType::TriggerSphere, IM_COL32(250, 204, 21, 255));
                spawnCard("Player (3rd Person)", "Sterowalna postac z ramieniem kamery (TPP)", 1, VectorIconType::Character, IM_COL32(52, 211, 153, 255));
                spawnCard("Player (1st Person FPP)", "Sterowalna postac ze spojrzeniem z oczu", 2, VectorIconType::Character, IM_COL32(16, 185, 129, 255));
                spawnCard("Directional Light", "4-Cascade Vogel PCF Sun",          9,  VectorIconType::Light,     IM_COL32(250, 204, 21, 255));
                spawnCard("Point Light",       "Omnidirectional Shadow Light",     3,  VectorIconType::Light,     IM_COL32(250, 204, 21, 255));
                spawnCard("Spot Light",        "Conical Spotlight Emitter",        10, VectorIconType::Light,     IM_COL32(245, 158, 11, 255));
                spawnCard("Area Light",        "Karis Rectangular Emitter",        11, VectorIconType::Light,     IM_COL32(56, 189, 248, 255));
                spawnCard("Fire VFX",          "Soft Gaussian HDR Particles",      8,  VectorIconType::Sparkles,  IM_COL32(251, 146, 60, 255));
                if (matchedCards == 0) {
                    ImGui::Spacing();
                    ImGui::TextDisabled(editorText("ui.no_actors_match_your_search", "No actors match your search."));
                    ImGui::TextDisabled(editorText("ui.try_another_name_e_g_player_light_or_sphere", "Try another name, e.g. Player, Light or Sphere."));
                }
                ImGui::EndChild();
                ImGui::EndDisabled();

                ImGui::End();
                ImGui::PopStyleColor();
            }
}

} // namespace engine::renderer
#endif
