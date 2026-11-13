#include "renderer/VulkanRenderer.hpp"
#include "renderer/EditorUiControls.hpp"
#include "renderer/EditorLocalization.hpp"
#include "editor/EditorActionHistory.hpp"
#include "core/Engine.hpp"
#include "ecs/Components.hpp"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>

#ifdef USE_GLFW
namespace engine::renderer {

extern int g_pendingHdriIndex;
void VulkanRenderer::drawEditorDetailsPanel(ecs::Registry& registry, core::Engine* engine,
                                            entt::entity& selectedEntity, bool immersivePlay,
                                            bool showDetails, bool showOutliner, float uiScale,
                                            float leftPanelW, float centerViewportW, float topTotalH,
                                            float outlinerH, float rightPanelW, float detailsH,
                                            const glm::vec3& cameraPosition, const glm::vec3& cameraForward,
                                            float cameraYaw, float cameraPitch, float cameraFov,
                                            const glm::vec2& viewportPanelSize) {
            if (!immersivePlay && showDetails) {
                const float detailsY = topTotalH + (showOutliner ? outlinerH : 0.0f);

                ImGui::SetNextWindowPos(ImVec2(leftPanelW + centerViewportW, detailsY), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(rightPanelW, detailsH), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.055f, 0.086f, 1.0f));
                ImGui::Begin("DetailsPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
                const bool simulationSessionActive = engine && engine->isSimulationSessionActive();

                // Equal-width inspector tabs with a persistent active state.
                const float inspectorTabGap = 4.0f * uiScale;
                const float inspectorTabWidth = std::max(1.0f, (ImGui::GetContentRegionAvail().x - inspectorTabGap) * 0.5f);
                const ImVec4 tabActive = ImVec4(0.10f, 0.34f, 0.58f, 1.0f);
                const ImVec4 tabInactive = ImVec4(0.085f, 0.095f, 0.125f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, m_detailsTab == 0 ? tabActive : tabInactive);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.13f, 0.40f, 0.68f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.30f, 0.52f, 1.0f));
                if (ImGui::Button(editorText("ui.details", "Details"), ImVec2(inspectorTabWidth, 26.0f * uiScale))) m_detailsTab = 0;
                ImGui::PopStyleColor(3);
                ImGui::SameLine(0.0f, inspectorTabGap);
                ImGui::PushStyleColor(ImGuiCol_Button, m_detailsTab == 1 ? tabActive : tabInactive);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.13f, 0.40f, 0.68f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.30f, 0.52f, 1.0f));
                if (ImGui::Button(editorText("ui.world_graphics", "World & Graphics"), ImVec2(inspectorTabWidth, 26.0f * uiScale))) m_detailsTab = 1;
                ImGui::PopStyleColor(3);
                ImGui::Separator();
                if (simulationSessionActive) {
                    ImGui::TextDisabled("%s", editorText("details.simulation_readonly", "Inspector is read-only while simulation is running."));
                    ImGui::Separator();
                }
                ImGui::BeginDisabled(simulationSessionActive);

                if (m_detailsTab == 0) {
                    // Clear stale handles before any component access (e.g. after deleting an entity elsewhere).
                    if (selectedEntity != entt::null && !registry.get().valid(selectedEntity)) {
                        selectedEntity = entt::null;
                        if (engine) engine->setSelectedEntity(entt::null);
                    }
                    if (selectedEntity == entt::null) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), editorText("ui.select_an_object_in_the_scene", "Select an object in the scene"));
                        ImGui::TextDisabled(editorText("ui.select_any_object_in_the_3d_viewport_or_the_outliner_above", "Select any object in the 3D viewport or the Outliner above,"));
                        ImGui::TextDisabled(editorText("ui.to_inspect_its_components_transform_mesh_pbr_physics_chemistry_lightin", "to inspect its components (Transform, Mesh, PBR, Physics, Chemistry, Lighting)."));
                        ImGui::Spacing();
                        ImGui::SeparatorText(editorText("ui.quickly_add_objects", "Quickly add objects"));
                        auto quickSpawnBtn = [&](const char* name, int mId, glm::vec3 col) {
                            if (ImGui::Button(name)) {
                                auto ent = registry.createEntity();
                                glm::vec3 sp = cameraPosition + cameraForward * 6.0f;
                                if (sp.y < 0.5f) sp.y = 0.5f;
                                registry.get().emplace<ecs::TagComponent>(ent, ecs::TagComponent{name});
                                registry.get().emplace<ecs::TransformComponent>(ent, sp, glm::vec3(0.0f), glm::vec3(1.0f));
                                registry.get().emplace<ecs::MeshComponent>(ent, ecs::MeshComponent{static_cast<uint32_t>(mId)});
                                ecs::RigidBodyComponent rb{}; rb.mass = 1.0f;
                                registry.get().emplace<ecs::RigidBodyComponent>(ent, rb);
                                ecs::MaterialComponent mat{}; mat.baseColor = glm::vec4(col, 1.0f); mat.roughness = 0.35f;
                                registry.get().emplace<ecs::MaterialComponent>(ent, mat);
                                selectedEntity = ent;
                                if (engine) engine->setSelectedEntity(selectedEntity);
                            }
                        };
                        quickSpawnBtn("+ Szescian Fizyczny", 2, glm::vec3(0.3f, 0.6f, 0.9f));
                        quickSpawnBtn("+ Kula Metaliczna", 3, glm::vec3(0.95f, 0.8f, 0.3f));
                        quickSpawnBtn("+ Kapsula / Postac", 5, glm::vec3(0.2f, 0.9f, 0.4f));
                        ImGui::Spacing();
                        ImGui::TextDisabled(editorText("details.world_hint_1", "You can also switch to the 'World & Graphics' tab above,"));
                        ImGui::TextDisabled(editorText("details.world_hint_2", "to control the HDRI sky, Sun, Fog and Ray Tracing."));
                    } else {
                        std::string tagStr = registry.get().all_of<ecs::TagComponent>(selectedEntity)
                            ? registry.get().get<ecs::TagComponent>(selectedEntity).name
                            : "Actor";
                        char nameBuf[64];
                        std::snprintf(nameBuf, sizeof(nameBuf), "%s", tagStr.c_str());
                        nameBuf[sizeof(nameBuf) - 1] = '\0';
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 170.0f * uiScale);
                        if (ImGui::InputText("##EntityName", nameBuf, sizeof(nameBuf))) {
                            if (registry.get().all_of<ecs::TagComponent>(selectedEntity)) {
                                registry.get().get<ecs::TagComponent>(selectedEntity).name = nameBuf;
                            } else {
                                registry.get().emplace<ecs::TagComponent>(selectedEntity, ecs::TagComponent{nameBuf});
                            }
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("#%u", static_cast<uint32_t>(selectedEntity));
                        ImGui::SameLine();
                        if (ImGui::Button(editorText("ui.component", "+ Component"), ImVec2(0, 0))) {
                            ImGui::OpenPopup("AddComponentPopup");
                        }
                        if (ImGui::BeginPopup("AddComponentPopup")) {
                            ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), editorText("ui.add_component", "Add component:"));
                            ImGui::Separator();
                            if (!registry.get().all_of<ecs::LightComponent>(selectedEntity)) {
                                if (ImGui::MenuItem("+ Swiatlo (Light)")) {
                                    registry.get().emplace<ecs::LightComponent>(selectedEntity, ecs::LightComponent{"point", glm::vec3(1.0f, 0.95f, 0.8f), 3.0f, 15.0f});
                                }
                            }
                            if (!registry.get().all_of<ecs::MeshComponent>(selectedEntity)) {
                                if (ImGui::MenuItem("+ Siatka 3D (Mesh)")) {
                                    registry.get().emplace<ecs::MeshComponent>(selectedEntity, ecs::MeshComponent{2});
                                }
                            }
                            if (!registry.get().all_of<ecs::MaterialComponent>(selectedEntity)) {
                                if (ImGui::MenuItem("+ Material PBR")) {
                                    ecs::MaterialComponent m{};
                                    m.baseColor = glm::vec4(0.85f, 0.85f, 0.90f, 1.0f);
                                    m.metallic = 0.0f;
                                    m.roughness = 0.35f;
                                    registry.get().emplace<ecs::MaterialComponent>(selectedEntity, m);
                                }
                            }
                            if (!registry.get().all_of<ecs::RigidBodyComponent>(selectedEntity)) {
                                if (ImGui::MenuItem("+ Fizyka Jolt (RigidBody)")) {
                                    ecs::RigidBodyComponent rb{};
                                    rb.mass = 1.0f;
                                    registry.get().emplace<ecs::RigidBodyComponent>(selectedEntity, rb);
                                    if (engine) engine->syncEntityTransformPhysics(selectedEntity);
                                }
                            }
                            if (!registry.get().all_of<ecs::ParticleEmitterComponent>(selectedEntity)) {
                                if (ImGui::MenuItem("+ Czasteczki (Particle VFX)")) {
                                    registry.get().emplace<ecs::ParticleEmitterComponent>(selectedEntity, ecs::ParticleEmitterComponent{"fire", glm::vec3(1.0f, 0.4f, 0.0f), 200, 3.0f, 0.4f});
                                }
                            }
                            if (!registry.get().all_of<ecs::CharacterMovementComponent>(selectedEntity)) {
                                if (ImGui::MenuItem("+ Ruch Postaci (Character Movement)")) {
                                    registry.get().emplace<ecs::CharacterMovementComponent>(selectedEntity);
                                }
                            }
                            if (!registry.get().all_of<ecs::CameraBoomComponent>(selectedEntity)) {
                                if (ImGui::MenuItem("+ Ramie Kamery (Camera Boom)")) {
                                    registry.get().emplace<ecs::CameraBoomComponent>(selectedEntity);
                                }
                            }
                            ImGui::EndPopup();
                        }
                        ImGui::Separator();

                        static char inspectorFilterBuffer[96] = "";
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::InputTextWithHint("##InspectorComponentFilter",
                            editorText("details.search_components", "Search components..."),
                            inspectorFilterBuffer, sizeof(inspectorFilterBuffer));
                        std::string inspectorFilter = inspectorFilterBuffer;
                        std::transform(inspectorFilter.begin(), inspectorFilter.end(), inspectorFilter.begin(),
                            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                        auto componentMatches = [&](const char* english, const char* polish) {
                            if (inspectorFilter.empty()) return true;
                            std::string a = english ? english : "";
                            std::string b = polish ? polish : "";
                            std::transform(a.begin(), a.end(), a.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                            std::transform(b.begin(), b.end(), b.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                            return a.find(inspectorFilter) != std::string::npos || b.find(inspectorFilter) != std::string::npos;
                        };
                        bool inspectorHasVisibleComponent = false;

                        if (registry.get().all_of<ecs::TransformComponent>(selectedEntity) &&
                            componentMatches("Transform", "Transformacja")) {
                            inspectorHasVisibleComponent = true;
                            auto& worldTransform = registry.get().get<ecs::TransformComponent>(selectedEntity);
                            auto* localTransform = registry.get().try_get<ecs::LocalTransformComponent>(selectedEntity);
                            glm::vec3& editPosition = localTransform ? localTransform->position : worldTransform.position;
                            glm::vec3& editRotation = localTransform ? localTransform->rotation : worldTransform.rotation;
                            glm::vec3& editScale = localTransform ? localTransform->scale : worldTransform.scale;
                            glm::quat& editOrientation = localTransform ? localTransform->orientation : worldTransform.orientation;
                            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                                const bool pCh = drawEditorVec3Control("Location", editPosition, 0.0f);
                                const bool rCh = drawEditorVec3Control("Rotation", editRotation, 0.0f);
                                const bool sCh = drawEditorVec3Control("Scale", editScale, 1.0f);
                                if (rCh) editOrientation = glm::quat(glm::radians(editRotation));
                                if (pCh || rCh || sCh) {
                                    registry.updateWorldTransforms();
                                    if (engine) engine->syncEntityTransformPhysics(selectedEntity);
                                }

                                ImGui::Spacing();
                                if (ImGui::Button(editorText("ui.reset_position_0_0_0", " Reset Position (0, 0, 0) "))) {
                                    editPosition = glm::vec3(0.0f);
                                    registry.updateWorldTransforms();
                                    if (engine) engine->syncEntityTransformPhysics(selectedEntity);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(editorText("ui.focus_camera_f", " Focus Camera [F] "))) {
                                    if (engine) engine->setCamera(worldTransform.position - cameraForward * 8.0f, cameraYaw, cameraPitch, cameraFov);
                                }
                            }
                        }

                        if (registry.get().all_of<ecs::LightComponent>(selectedEntity) && componentMatches("Light", "Swiatlo")) {
                            inspectorHasVisibleComponent = true;
                            auto& lc = registry.get().get<ecs::LightComponent>(selectedEntity);
                            if (ImGui::CollapsingHeader("Light Component (PBR)", ImGuiTreeNodeFlags_DefaultOpen)) {
                                const char* lightTypes[] = { "point", "sun", "spot", "area" };
                                int curType = (lc.type == "directional" || lc.type == "sun") ? 1 : (lc.type == "spot" ? 2 : (lc.type == "area" ? 3 : 0));
                                if (ImGui::Combo(editorText("details.light_type", "Light Type"), &curType, "Point Light\0Directional (Sun)\0Spot Light\0Area Light (Rect)\0\0")) {
                                    lc.type = lightTypes[curType];
                                }
                                if (ImGui::ColorEdit3("Color", (float*)&lc.color)) {}
                                if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);
                                if (drawEditorDragFloat("Intensity", &lc.intensity, 0.1f, 0.0f, 100.0f, 5.0f, "%.2f")) {}
                                if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);

                                if (curType == 0) { // Point
                                    if (drawEditorDragFloat("Range (m)", &lc.range, 0.5f, 1.0f, 200.0f, 25.0f, "%.1f m")) {}
                                    if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);
                                } else if (curType == 1) { // Sun / Directional
                                    if (registry.get().all_of<ecs::TransformComponent>(selectedEntity)) {
                                        auto& lt = registry.get().get<ecs::TransformComponent>(selectedEntity);
                                        bool changed = false;

                                        ImGui::Spacing();
                                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Kierunek padania slonca (Sun Direction):");
                                        if (ImGui::SliderFloat(editorText("details.elevation_pitch", "Elevation (Pitch)"), &lt.rotation.x, -89.0f, 89.0f, "%.1f deg")) changed = true;
                                        if (ImGui::SliderFloat(editorText("details.azimuth_yaw", "Azimuth (Yaw)"), &lt.rotation.y, 0.0f, 360.0f, "%.1f deg")) changed = true;

                                        const float pitchRad = glm::radians(lt.rotation.x);
                                        const float yawRad   = glm::radians(lt.rotation.y);
                                        const glm::vec3 sunForward = glm::normalize(glm::vec3(std::cos(yawRad) * std::cos(pitchRad), std::sin(pitchRad), std::sin(yawRad) * std::cos(pitchRad)));
                                        const glm::vec3 currentSunDir = -sunForward;
                                        ImGui::TextDisabled(editorText("details.light_vector", "Light vector: (%.2f, %.2f, %.2f)"), currentSunDir.x, currentSunDir.y, currentSunDir.z);

                                        ImGui::Text(editorText("ui.time_of_day", "Time of day:"));
                                        if (ImGui::SmallButton(editorText("ui.noon", "Noon"))) {
                                            lt.rotation.x = -75.0f; lt.rotation.y = 0.0f; changed = true;
                                        }
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton(editorText("ui.morning", "Morning"))) {
                                            lt.rotation.x = -20.0f; lt.rotation.y = 80.0f; changed = true;
                                        }
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton(editorText("ui.golden_hour", "Golden hour"))) {
                                            lt.rotation.x = -12.0f; lt.rotation.y = 240.0f; changed = true;
                                        }
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton(editorText("ui.sunset", "Sunset"))) {
                                            lt.rotation.x = -3.0f; lt.rotation.y = 265.0f; changed = true;
                                        }

                                        ImGui::Spacing();
                                        if (engine) {
                                            auto env = engine->getEnvironmentSettings();
                                            if (ImGui::Checkbox(editorText("details.sun_enabled", "Sun Enabled"), &env.sunEnabled)) {
                                                engine->setEnvironmentSettings(env);
                                            }
                                            if (ImGui::Checkbox(editorText("details.sun_direction_from_position", "Direction From Position"), &env.sunDirectionFromPosition)) {
                                                engine->setEnvironmentSettings(env);
                                            }
                                            ImGui::SameLine();
                                            ImGui::TextDisabled("(?)");
                                            if (ImGui::IsItemHovered()) {
                                                ImGui::SetTooltip(editorText("details.sun_direction_tooltip", "When enabled: the sun disk in the sky is located exactly at the Sun object in 3D space.\nWhen disabled: the sun is infinitely distant and its direction comes from rotation (Pitch/Yaw)."));
                                            }
                                        }

                                        ImGui::Spacing();
                                        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Pozycja slonca 3D (Sun Position):");
                                        if (drawEditorVec3Control("Pozycja (XYZ)", lt.position, 0.0f)) changed = true;
                                        if (ImGui::DragFloat("Wysokosc / Altitude (Y)", &lt.position.y, 0.5f, -200.0f, 2000.0f, "%.1f m")) changed = true;

                                        if (changed) {
                                            lt.orientation = glm::quat(glm::radians(lt.rotation));
                                            if (engine) {
                                                engine->syncEntityTransformPhysics(selectedEntity);
                                                auto env = engine->getEnvironmentSettings();
                                                env.sunDirection = currentSunDir;
                                                env.sunColor = lc.color;
                                                env.sunIntensity = lc.intensity;
                                                engine->setEnvironmentSettings(env);
                                            }
                                        }
                                    }
                                } else if (curType == 2) { // Spot
                                    if (drawEditorDragFloat("Range (m)", &lc.range, 0.5f, 1.0f, 200.0f, 25.0f, "%.1f m")) {}
                                    if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);
                                    if (drawEditorSliderFloat(editorText("details.inner_cone", "Inner Cone"), &lc.innerConeAngle, 1.0f, 80.0f, 25.0f, "%.1f deg")) {}
                                    if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);
                                    if (lc.outerConeAngle <= lc.innerConeAngle) lc.outerConeAngle = lc.innerConeAngle + 0.5f;
                                    if (drawEditorSliderFloat(editorText("details.outer_cone", "Outer Cone"), &lc.outerConeAngle, lc.innerConeAngle + 0.5f, 89.0f, 35.0f, "%.1f deg")) {}
                                    if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);
                                    if (registry.get().all_of<ecs::TransformComponent>(selectedEntity)) {
                                        auto& lt = registry.get().get<ecs::TransformComponent>(selectedEntity);
                                        bool changed = false;
                                        if (drawEditorSliderFloat(editorText("details.spot_pitch", "Spot Pitch"), &lt.rotation.x, -89.0f, 89.0f, 0.0f, "%.1f deg")) changed = true;
                                        if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);
                                        if (drawEditorSliderFloat(editorText("details.spot_yaw", "Spot Yaw"), &lt.rotation.y, 0.0f, 360.0f, 0.0f, "%.1f deg")) changed = true;
                                        if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);
                                        if (changed) {
                                            lt.orientation = glm::quat(glm::radians(lt.rotation));
                                            if (engine) engine->syncEntityTransformPhysics(selectedEntity);
                                        }
                                    }
                                } else if (curType == 3) { // Area
                                    if (drawEditorDragFloat("Range (m)", &lc.range, 0.5f, 1.0f, 200.0f, 25.0f, "%.1f m")) {}
                                    if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);
                                    if (drawEditorSliderFloat(editorText("details.area_width", "Area Width X"), &lc.areaSizeX, 0.2f, 20.0f, 2.0f, "%.2f m")) {}
                                    if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);
                                    if (drawEditorSliderFloat(editorText("details.area_height", "Area Height Y"), &lc.areaSizeY, 0.2f, 20.0f, 2.0f, "%.2f m")) {}
                                    if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);
                                    if (registry.get().all_of<ecs::TransformComponent>(selectedEntity)) {
                                        auto& lt = registry.get().get<ecs::TransformComponent>(selectedEntity);
                                        bool changed = false;
                                        if (drawEditorSliderFloat(editorText("details.area_pitch", "Area Pitch"), &lt.rotation.x, -89.0f, 89.0f, 0.0f, "%.1f deg")) changed = true;
                                        if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);
                                        if (drawEditorSliderFloat(editorText("details.area_yaw", "Area Yaw"), &lt.rotation.y, 0.0f, 360.0f, 0.0f, "%.1f deg")) changed = true;
                                        if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);
                                        if (changed) {
                                            lt.orientation = glm::quat(glm::radians(lt.rotation));
                                            if (engine) engine->syncEntityTransformPhysics(selectedEntity);
                                        }
                                    }
                                }
                            }
                        }

                        if (registry.get().all_of<ecs::MeshComponent>(selectedEntity) && componentMatches("Mesh", "Siatka")) {
                            inspectorHasVisibleComponent = true;
                            auto& mc = registry.get().get<ecs::MeshComponent>(selectedEntity);
                            if (ImGui::CollapsingHeader("Static Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
                                const char* meshes[] = { "None", "Plane (Ground)", "Cube (1x1x1)", "Sphere (R=0.5)", "Cylinder", "Capsule" };
                                int curMesh = std::clamp(static_cast<int>(mc.meshId), 0, 5);
                                if (ImGui::Combo(editorText("details.mesh_asset", "Mesh Asset"), &curMesh, meshes, IM_ARRAYSIZE(meshes))) {
                                    mc.meshId = static_cast<uint32_t>(curMesh);
                                }
                            }
                        }

                        // Żywa edycja komponentu materiału PBR (Kulla-Conty Multiscatter + Cubemap IBL)
                        if (registry.get().all_of<ecs::MeshComponent>(selectedEntity) &&
                            componentMatches("Material", "Material PBR")) {
                            inspectorHasVisibleComponent = true;
                            if (ImGui::CollapsingHeader("PBR Material (Multiscatter GGX)", ImGuiTreeNodeFlags_DefaultOpen)) {
                                if (!registry.get().all_of<ecs::MaterialComponent>(selectedEntity)) {
                                    if (ImGui::Button(editorText("details.add_custom_pbr", "+ Add Custom PBR Material"))) {
                                        ecs::MaterialComponent m{};
                                        m.baseColor = glm::vec4(0.85f, 0.85f, 0.90f, 1.0f);
                                        m.metallic = 0.0f;
                                        m.roughness = 0.35f;
                                        m.emissiveIntensity = 0.0f;
                                        registry.get().emplace<ecs::MaterialComponent>(selectedEntity, m);
                                    }
                                } else {
                                    auto& mat = registry.get().get<ecs::MaterialComponent>(selectedEntity);
                                    if (ImGui::ColorEdit3("Base Color (Albedo)", &mat.baseColor.x)) {
                                    }
                                    if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);

                                    if (drawEditorSliderFloat(editorText("details.metallic", "Metallic"), &mat.metallic, 0.0f, 1.0f, 0.0f, "%.2f")) {
                                    }
                                    if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);

                                    if (drawEditorSliderFloat(editorText("details.roughness", "Roughness"), &mat.roughness, 0.0f, 1.0f, 0.5f, "%.3f")) {
                                    }
                                    if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);

                                    if (drawEditorDragFloat("Emissive HDR", &mat.emissiveIntensity, 0.05f, 0.0f, 25.0f, 0.0f, "%.2f")) {
                                    }
                                    if (ImGui::IsItemActivated()) editor::EditorActionHistory::get().recordSnapshot(registry);

                                    if (ImGui::Checkbox(editorText("details.reflects_light", "Reflects Light (Odbija światło)"), &mat.reflectsLight)) {
                                        editor::EditorActionHistory::get().recordSnapshot(registry);
                                    }

                                    ImGui::Spacing();
                                    ImGui::Separator();
                                    ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Zestawy Tekstur PBR (Proceduralne):");

                                    auto applyPreset = [&](const char* name) {
                                        if (engine && engine->getAssetManager()) {
                                            auto* am = engine->getAssetManager();
                                            uint32_t mId = am->applyPbrPreset(name);
                                            mat.materialId = mId;
                                            if (auto* mAsset = am->getMaterial(mId)) {
                                                mat.albedoTextureId = mAsset->albedoTextureId;
                                                mat.normalTextureId = mAsset->normalTextureId;
                                                mat.ormTextureId = mAsset->ormTextureId;
                                            }
                                            invalidateMaterialDescriptorSet(mat.materialId);
                                        }
                                    };

                                    if (ImGui::Button("Cegly (Bricks)")) applyPreset("Bricks");
                                    ImGui::SameLine();
                                    if (ImGui::Button("Kafelki (Tiles)")) applyPreset("Tiles");
                                    ImGui::SameLine();
                                    if (ImGui::Button("Drewno (Wood)")) applyPreset("Wood");

                                    if (ImGui::Button("Krata (MetalGrid)")) applyPreset("MetalGrid");
                                    ImGui::SameLine();
                                    if (ImGui::Button("Szachownica")) applyPreset("Checkerboard");
                                    ImGui::SameLine();
                                    if (ImGui::Button("Reset")) {
                                        mat.materialId = 0;
                                        mat.albedoTextureId = 0;
                                        mat.normalTextureId = 0;
                                        mat.ormTextureId = 0;
                                        invalidateMaterialDescriptorSet(0);
                                    }

                                    ImGui::Spacing();
                                    ImGui::Separator();
                                    ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Sloty Tekstur PBR (Pliki .png/.jpg):");

                                    auto syncCustomMaterial = [&]() {
                                        if (!engine || !engine->getAssetManager()) return;
                                        auto* am = engine->getAssetManager();
                                        core::MaterialAsset mAsset{};
                                        mAsset.baseColorFactor = mat.baseColor;
                                        mAsset.metallicFactor = mat.metallic;
                                        mAsset.roughnessFactor = mat.roughness;
                                        mAsset.albedoTextureId = mat.albedoTextureId;
                                        mAsset.normalTextureId = mat.normalTextureId;
                                        mAsset.ormTextureId = mat.ormTextureId;
                                        if (mat.materialId == 0) {
                                            mat.materialId = am->createMaterial(mAsset);
                                        } else {
                                            am->updateMaterial(mat.materialId, mAsset);
                                        }
                                        invalidateMaterialDescriptorSet(mat.materialId);
                                    };

                                    auto renderSlot = [&](const char* label, std::string& path, uint32_t& texId) {
                                        ImGui::PushID(label);
                                        char buf[256];
                                        std::snprintf(buf, sizeof(buf), "%s", path.c_str());
                                        buf[sizeof(buf) - 1] = '\0';
                                        if (ImGui::InputText(label, buf, sizeof(buf))) {
                                            path = buf;
                                        }
                                        ImGui::SameLine();
                                        if (ImGui::Button("Wczytaj")) {
                                            if (engine && engine->getAssetManager() && !path.empty()) {
                                                const bool isAlbedo = std::string(label) == "Albedo";
                                                uint32_t newId = engine->getAssetManager()->loadTextureFromFile(path, true, isAlbedo);
                                                if (newId != 0) {
                                                    texId = newId;
                                                    syncCustomMaterial();
                                                }
                                            }
                                        }
                                        if (texId != 0) {
                                            ImGui::SameLine();
                                            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[ID: %u]", texId);
                                            ImGui::SameLine();
                                            if (ImGui::SmallButton("X")) {
                                                texId = 0;
                                                path.clear();
                                                syncCustomMaterial();
                                            }
                                        } else {
                                            ImGui::SameLine();
                                            ImGui::TextDisabled("(domyslna)");
                                        }
                                        ImGui::PopID();
                                    };

                                    renderSlot("Albedo Map", mat.albedoPath, mat.albedoTextureId);
                                    renderSlot("Normal Map", mat.normalPath, mat.normalTextureId);
                                    renderSlot("ORM Map (AO/Rough/Met)", mat.ormPath, mat.ormTextureId);
                                }
                            }
                        }

                        if (registry.get().all_of<ecs::RigidBodyComponent>(selectedEntity) && componentMatches("Physics", "Fizyka")) {
                            inspectorHasVisibleComponent = true;
                            auto& rb = registry.get().get<ecs::RigidBodyComponent>(selectedEntity);
                            if (ImGui::CollapsingHeader("Jolt Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
                                ImGui::DragFloat("Mass (kg)", &rb.mass, 0.1f, 0.0f, 500.0f);
                                ImGui::SliderFloat(editorText("details.restitution", "Restitution"), &rb.restitution, 0.0f, 1.0f);
                                ImGui::SliderFloat(editorText("details.friction", "Friction"), &rb.friction, 0.0f, 1.0f);
                                ImGui::Checkbox(editorText("details.sensor_trigger", "Is Sensor (Trigger)"), &rb.isSensor);
                            }
                        }

                        if (registry.get().all_of<ecs::ParticleEmitterComponent>(selectedEntity) && componentMatches("Particle", "Czasteczki")) {
                            inspectorHasVisibleComponent = true;
                            auto& pe = registry.get().get<ecs::ParticleEmitterComponent>(selectedEntity);
                            if (ImGui::CollapsingHeader("Particle Emitter (HDR VFX)", ImGuiTreeNodeFlags_DefaultOpen)) {
                                const char* pTypes[] = { "fire", "sparks", "smoke", "portal", "explosion" };
                                int curP = 0;
                                for (int i = 0; i < 5; ++i) { if (pe.type == pTypes[i]) { curP = i; break; } }
                                if (ImGui::Combo(editorText("details.particle_type", "Particle Type"), &curP, "Fire\0Sparks\0Smoke\0Portal\0Explosion\0\0")) {
                                    pe.type = pTypes[curP];
                                }
                                ImGui::ColorEdit3("Particle Color", &pe.color.x);
                                int pCount = static_cast<int>(pe.count);
                                if (ImGui::SliderInt("Particle Count", &pCount, 10, 1000)) pe.count = static_cast<uint32_t>(pCount);
                                ImGui::SliderFloat(editorText("details.speed", "Speed"), &pe.speed, 0.1f, 20.0f, "%.1f m/s");
                                ImGui::SliderFloat(editorText("details.size", "Size"), &pe.size, 0.05f, 2.0f, "%.2f m");
                            }
                        }

                        if (registry.get().all_of<ecs::CharacterMovementComponent>(selectedEntity) && componentMatches("Character", "Postac")) {
                            inspectorHasVisibleComponent = true;
                            auto& cm = registry.get().get<ecs::CharacterMovementComponent>(selectedEntity);
                            if (ImGui::CollapsingHeader("Character Movement (Sterowanie)", ImGuiTreeNodeFlags_DefaultOpen)) {
                                ImGui::DragFloat("Max Walk Speed (m/s)", &cm.maxWalkSpeed, 0.2f, 1.0f, 50.0f, "%.1f m/s");
                                ImGui::DragFloat("Jump Z Velocity (m/s)", &cm.jumpZVelocity, 0.2f, 0.0f, 30.0f, "%.1f m/s");
                                ImGui::DragFloat("Gravity Scale", &cm.gravityScale, 0.05f, 0.0f, 10.0f, "%.2f");
                                ImGui::DragFloat("Air Control (0..1)", &cm.airControl, 0.02f, 0.0f, 1.0f, "%.2f");
                                ImGui::TextDisabled(editorText("details.controls_movement", "Controls: WASD = Move | Space = Jump | Shift = Sprint"));
                            }
                        }

                        if (registry.get().all_of<ecs::CameraBoomComponent>(selectedEntity) && componentMatches("Camera", "Kamera")) {
                            inspectorHasVisibleComponent = true;
                            auto& cb = registry.get().get<ecs::CameraBoomComponent>(selectedEntity);
                            if (ImGui::CollapsingHeader("Camera Boom (Ramie Kamery)", ImGuiTreeNodeFlags_DefaultOpen)) {
                                if (ImGui::Checkbox(editorText("details.first_person_view", "First Person View (FPP)"), &cb.isFirstPerson)) {
                                    if (cb.isFirstPerson) {
                                        cb.targetArmLength = 0.0f;
                                        cb.socketOffsetY = 1.65f;
                                        cb.cameraLag = false;
                                        if (engine && !engine->isPaused()) engine->setCameraMode("firstPerson");
                                    } else {
                                        cb.targetArmLength = 8.0f;
                                        cb.socketOffsetY = 2.5f;
                                        cb.cameraLag = true;
                                        if (engine && !engine->isPaused()) engine->setCameraMode("thirdPerson");
                                    }
                                }
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("details.first_person_tooltip", "Switch between first-person (FPP) and third-person (TPP) view [V key in game]"));

                                if (cb.isFirstPerson) {
                                    ImGui::DragFloat("Wysokosc oczu Y", &cb.socketOffsetY, 0.05f, 0.5f, 3.0f, "%.2f m");
                                    ImGui::TextDisabled(editorText("details.first_person_hint", "FPP mode: camera is attached directly to the player's eyes."));
                                } else {
                                    ImGui::DragFloat("Target Arm Length (m)", &cb.targetArmLength, 0.2f, 0.5f, 50.0f, "%.1f m");
                                    ImGui::DragFloat("Socket Offset Y", &cb.socketOffsetY, 0.1f, -5.0f, 10.0f, "%.1f m");
                                    ImGui::Checkbox(editorText("details.camera_lag", "Camera Lag (Smooth Follow)"), &cb.cameraLag);
                                    if (cb.cameraLag) {
                                        ImGui::SliderFloat(editorText("details.lag_speed", "Lag Speed"), &cb.lagSpeed, 1.0f, 30.0f, "%.1f");
                                    }
                                }
                            }
                        }
                        if (!inspectorFilter.empty() && !inspectorHasVisibleComponent) {
                            ImGui::TextDisabled("%s", editorText("details.no_component_matches", "No components match this filter."));
                        }
                    }
                } else {
                    // Hardware ray-query effects are optional; raster shadows remain the fallback.
                    if (ImGui::CollapsingHeader("Hardware Ray Tracing (Vulkan Ray Query)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        const bool rtSupported = m_context && m_context->isRayTracingSupported();
                        if (!rtSupported) {
                            ImGui::TextDisabled(editorText("details.rt_unavailable", "Ray tracing unavailable: GPU/driver lacks required Vulkan KHR features."));
                        }
                        ImGui::BeginDisabled(!rtSupported);
                        ImGui::Checkbox(editorText("details.rt_enable", "Enable Hardware Ray Tracing"), &m_rtSettings.enabled);
                        ImGui::BeginDisabled(!m_rtSettings.enabled);
                        ImGui::Checkbox(editorText("details.rt_shadows", "Ray-Traced Shadows (Sun + Point + Spot)"), &m_rtSettings.shadowsEnabled);
                        ImGui::Checkbox(editorText("details.rt_ao", "Ray-Traced Ambient Occlusion"), &m_rtSettings.aoEnabled);
                        ImGui::Checkbox(editorText("details.rt_reflections", "Ray-Traced Reflections (Scene Geometry)"), &m_rtSettings.reflectionsEnabled);
                        ImGui::BeginDisabled(!m_rtSettings.reflectionsEnabled);
                        ImGui::SliderInt("RT Reflection Samples", &m_rtSettings.reflectionSamples, 1, 16, "%d");
                        ImGui::Checkbox(editorText("details.rt_infinite_bounce", "Infinite Bounce (adaptive, max 16)"), &m_rtSettings.reflectionInfiniteBounces);
                        ImGui::BeginDisabled(m_rtSettings.reflectionInfiniteBounces);
                        ImGui::SliderInt("RT Reflection Bounces", &m_rtSettings.reflectionBounces, 2, 4, "%d");
                        ImGui::EndDisabled();
                        ImGui::SliderFloat(editorText("details.rt_reflection_roughness", "Reflection Continuation Roughness"), &m_rtSettings.reflectionMaxRoughness, 0.05f, 1.0f, "%.2f");
                        ImGui::SliderFloat(m_rtSettings.reflectionInfiniteBounces ? editorText("details.rt_russian_roulette", "Russian Roulette Energy Threshold") : editorText("details.rt_min_throughput", "Reflection Min Throughput"), &m_rtSettings.reflectionMinThroughput, 0.01f, 0.25f, "%.3f");
                        if (m_rtSettings.reflectionInfiniteBounces)
                            ImGui::TextDisabled(editorText("details.rt_adaptive_termination", "Adaptive path termination; hard safety limit: 16 surface hits."));
                        ImGui::Checkbox(editorText("details.rt_roughness_checkerboard", "Roughness Checkerboard (Variant 1)"), &m_rtSettings.reflectionCheckerboard);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(editorText("details.rt_roughness_tooltip", "Smooth surfaces (roughness <= 0.25) traced at full 1:1; rough surfaces traced on alternating checkerboard and bilaterally reconstructed."));
                        ImGui::Checkbox(editorText("details.rt_stochastic_lights", "Stochastic Lights on Bounces (Variant 2)"), &m_rtSettings.reflectionStochasticLights);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(editorText("details.rt_stochastic_tooltip", "On secondary bounces (bounce >= 1), importance-samples 1 light instead of testing all active local lights."));
                        ImGui::Checkbox("Primary-hit stochastic local-light sampling (PR #49)", &m_rtSettings.reflectionPrimaryStochasticLights);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Optional: applies the same unbiased one-light estimator to the first reflection hit. Reduces local-light shadow queries, but can increase visible per-sample variance. Disabled by default to preserve pre-PR #49 behavior.");
                        ImGui::Checkbox(editorText("details.rt_lod_bias", "Bounce Depth Mip LOD Bias (Variant 3)"), &m_rtSettings.reflectionLodBias);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(editorText("details.rt_lod_tooltip", "Increases texture mip LOD with bounce depth (textureLod float(bounce)) to reduce GPU texture cache thrashing."));
                        ImGui::Checkbox(editorText("details.rt_half_res", "Global Half-Res Reflections (Variant 4)"), &m_rtSettings.reflectionHalfRes);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(editorText("details.rt_half_res_tooltip", "Forces all reflections into half-resolution with edge-aware bilateral normal/depth reconstruction."));
                        ImGui::Checkbox(editorText("details.rt_show_light_bulbs", "Show Light Bulbs in Reflections (Świecące kule świateł w odbiciach)"), &m_rtSettings.reflectionShowLightBulbs);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("When disabled, analytical lights do not draw visible emissive proxy orbs inside reflections. Spotlights are only ever visible within their forward cone.");
                        ImGui::Checkbox(editorText("details.rt_denoise_mirrors", "Denoise Mirror Reflections (Filtr przestrzenny luster)"), &m_rtSettings.reflectionDenoiseMirrors);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Opcjonalny filtr usuwający fireflies dla idealnie gładkich luster (roughness < 0.035). Gdy wyłączony (domyślnie), lustro zachowuje czyste, niezmodyfikowane odbicie bez rozmycia.");
                        ImGui::EndDisabled();
                        ImGui::Checkbox(editorText("details.rt_diffuse_gi", "Ray-Traced Diffuse GI"), &m_rtSettings.giEnabled);
                        ImGui::BeginDisabled(!m_rtSettings.giEnabled);
                        ImGui::SliderFloat(editorText("details.rt_gi_intensity", "RT GI Intensity"), &m_rtSettings.giIntensity, 0.0f, 3.0f, "%.2f");
                        ImGui::SliderInt("RT Diffuse GI Bounces", &m_rtSettings.giBounces, 1, 15, "%d");
                        ImGui::Checkbox(editorText("details.rt_gi_checkerboard", "GI Checkerboard (Half-Rate Trace)"), &m_rtSettings.giCheckerboard);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(editorText("details.rt_gi_checkerboard_tooltip", "Traces GI on alternating checkerboard pixels, cutting ray tracing work by 50% and reconstructing via temporal history & bilateral filter."));
                        ImGui::TextDisabled(editorText("details.rt_gi_resolution", "Resolution: Half-Res Diffuse GI (4x compute speedup)"));
                        ImGui::EndDisabled();

                        // GI + AO are traced in compute, then temporally accumulated and denoised.
                        ImGui::SeparatorText("RT Sampling & Denoising");
                        ImGui::BeginDisabled(!m_rtSettings.giEnabled && !m_rtSettings.aoEnabled);
                        ImGui::SliderInt("RT GI/AO Samples per Pixel", &m_rtSettings.giSamples, 1, 16, "%d");
                        ImGui::Checkbox("GI Path Firefly Clamp", &m_rtSettings.giTraceFireflyClampEnabled);
                        ImGui::SeparatorText("GI Radiance Caches");
                        ImGui::Checkbox("World Radiance Probe Cache", &m_rtSettings.worldRadianceCacheEnabled);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Controls probe-cache updates and temporal GI fallback sampling. Toggling clears cached probes to prevent stale re-use.");
                        ImGui::Checkbox("Persistent GI Surface History Cache", &m_rtSettings.giSurfaceHistoryCacheEnabled);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Controls world-space diffuse history restore and final à-trous writeback. Toggling invalidates the cache epoch.");
                        ImGui::Checkbox(editorText("details.rt_denoiser", "Enable GI Denoising"), &m_rtSettings.denoiseEnabled);
                        ImGui::BeginDisabled(!m_rtSettings.denoiseEnabled);
                        ImGui::Checkbox("GI Temporal Accumulation", &m_rtSettings.giTemporalFilterEnabled);
                        ImGui::Checkbox("GI Temporal Neighborhood Clamp", &m_rtSettings.giHistoryClampEnabled);
                        ImGui::Checkbox("GI Temporal Firefly Rejection", &m_rtSettings.giTemporalFireflyFilterEnabled);
                        ImGui::SliderInt("Temporal History (samples)", &m_rtSettings.maxHistory, 4, 256, "%d");
                        ImGui::Checkbox(editorText("details.rt_svgf", "SVGF (Variance-Guided Filter)"), &m_rtSettings.svgfEnabled);
                        ImGui::SeparatorText("À-Trous Wavelet Passes");
                        ImGui::Checkbox("À-Trous Pass 1 (step 1)", &m_rtSettings.giAtrousPass1Enabled);
                        ImGui::Checkbox("À-Trous Pass 2 (step 2)", &m_rtSettings.giAtrousPass2Enabled);
                        ImGui::Checkbox("À-Trous Pass 3 (step 4)", &m_rtSettings.giAtrousPass3Enabled);
                        ImGui::Checkbox("À-Trous Pass 4 (step 8)", &m_rtSettings.giAtrousPass4Enabled);
                        ImGui::SliderFloat(editorText("details.rt_spatial_strength", "GI Spatial Filter Strength"), &m_rtSettings.spatialStrength, 0.0f, 1.0f, "%.2f");
                        ImGui::EndDisabled();
                        // Debug Visualization Modes
                        ImGui::SeparatorText("RT Debug & Diagnostics");
                        const char* debugModes[] = {
                            "0: Off (Composite Render)",
                            "1: Raw GI Trace (before temporal)",
                            "2: History Length (0..maxHistory)",
                            "3: Octahedral Geometric Normals",
                            "4: Linear Depth",
                            "5: Ambient Occlusion Only",
                            "6: Specular Reflections Only",
                            "7: Luminance Variance (SVGF)",
                            "8: Raw Specular Caustics (before filtering)",
                            "9: Caustic Contribution Density",
                            "10: Temporal GI (before A-Trous)",
                            "11: World Radiance Cache Hits (green=hit, red=miss, blue=disabled)",
                            "12: Persistent Surface History Hits (green=hit, red=miss, blue=disabled)"
                        };
                        int currentDebugMode = static_cast<int>(m_rtSettings.debugMode);
                        if (ImGui::Combo(editorText("details.rt_debug_view", "RT Debug View"), &currentDebugMode, debugModes, IM_ARRAYSIZE(debugModes))) {
                            m_rtSettings.debugMode = static_cast<RtDebugMode>(currentDebugMode);
                        }

                        // RT GPU Timers
                        const auto& timings = m_rtDenoiser.getGpuTimings();
                        if (ImGui::TreeNode("RT GPU Profiler (Compute Timers)")) {
                            ImGui::Text("GI Trace Pass:    %.3f ms", timings.traceMs);
                            ImGui::Text("Refl Trace Pass:  %.3f ms", timings.reflTraceMs);
                            ImGui::Text("GI Temporal:      %.3f ms", timings.temporalMs);
                            ImGui::Text("Refl Temporal:    %.3f ms", timings.reflTemporalMs);
                            ImGui::Text("Atrous Step 1 (s1): %.3f ms", timings.atrous0Ms);
                            ImGui::Text("Atrous Step 2 (s2): %.3f ms", timings.atrous1Ms);
                            ImGui::Text("Atrous Step 3 (s4): %.3f ms", timings.atrous2Ms);
                            ImGui::Text("Atrous Step 4 (s8): %.3f ms", timings.atrous3Ms);
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Total RT Compute: %.3f ms", timings.totalMs);
                            ImGui::TreePop();
                        }

                        ImGui::EndDisabled();
                        m_rtSettings.giSamples = std::clamp(m_rtSettings.giSamples, 1, 16);
                        m_rtSettings.reflectionSamples = std::clamp(m_rtSettings.reflectionSamples, 1, 16);
                        m_rtSettings.reflectionBounces = std::clamp(m_rtSettings.reflectionBounces, 2, 4);
                        m_rtSettings.reflectionMaxRoughness = std::clamp(m_rtSettings.reflectionMaxRoughness, 0.05f, 1.0f);
                        m_rtSettings.reflectionMinThroughput = std::clamp(m_rtSettings.reflectionMinThroughput, 0.01f, 0.25f);
                        m_rtSettings.maxHistory = std::clamp(m_rtSettings.maxHistory, 4, 256);
                        ImGui::EndDisabled();
                        ImGui::EndDisabled();

                        ImGui::SeparatorText("RT Reflection Filtering");
                        ImGui::BeginDisabled(!m_rtSettings.reflectionsEnabled);
                        ImGui::Checkbox("Reflection Temporal Filter", &m_rtSettings.reflectionTemporalFilterEnabled);
                        ImGui::Checkbox("Reflection Temporal Outlier Rejection", &m_rtSettings.reflectionOutlierFilterEnabled);
                        ImGui::Checkbox("Reflection Spatial Filter", &m_rtSettings.reflectionSpatialFilterEnabled);
                        ImGui::EndDisabled();

                        const auto& rtDiag = m_rtDenoiser.getDiagnosticCounters();
                        if (ImGui::TreeNode("RT Mirror / Caustics Diagnostics (previous frame)")) {
                            ImGui::Text("GI path surface hits: %u", rtDiag[48]);
                            ImGui::Text("Reflective surface hits: %u", rtDiag[49]);
                            ImGui::Text("Tested light candidates: %u", rtDiag[50]);
                            ImGui::Text("Reflection-cone matches: %u", rtDiag[51]);
                            ImGui::Text("Unoccluded matches: %u", rtDiag[52]);
                            ImGui::Text("Accepted caustic contributions: %u", rtDiag[53]);
                            ImGui::Text("Traced GI/AO samples: %u", rtDiag[54]);
                            ImGui::Text("Path surface bounces: %u", rtDiag[55]);
                            ImGui::TreePop();
                        }
                    }

                    // Zakładka World & Graphics Settings (wszystkie nowe systemy Vulkan 1.3)
                    if (ImGui::CollapsingHeader("GPU Cubemap IBL & Skybox", ImGuiTreeNodeFlags_DefaultOpen)) {
                        const char* hdriItems[] = {
                            "0: Sunset (Zachod slonca)",
                            "1: Studio (Neutralne studio)",
                            "2: City (Panorama miejska)",
                            "3: Courtyard (Dziedziniec)",
                            "4: Forest (Otoczenie lesne)",
                            "5: Interior (Wnetrze)",
                            "6: Night (Nocne niebo)",
                            "7: Sunrise (Wschod slonca)"
                        };
                        int currentHdri = m_selectedHdriIndex;
                        if (ImGui::Combo(editorText("details.hdri_environment", "HDRI Environment"), &currentHdri, hdriItems, IM_ARRAYSIZE(hdriItems))) {
                            g_pendingHdriIndex = currentHdri;
                            if (engine) {
                                auto env = engine->getEnvironmentSettings();
                                switch (currentHdri) {
                                    case 0: // Sunset
                                        env.sunColor = glm::vec3(1.0f, 0.65f, 0.30f);
                                        env.sunDirection = glm::normalize(glm::vec3(0.75f, 0.12f, 0.45f));
                                        env.ambientColor = glm::vec3(0.22f, 0.12f, 0.18f);
                                        env.sunIntensity = 4.2f;
                                        break;
                                    case 1: // Studio
                                        env.sunColor = glm::vec3(1.0f, 1.0f, 1.0f);
                                        env.sunDirection = glm::normalize(glm::vec3(0.0f, 0.95f, 0.20f));
                                        env.ambientColor = glm::vec3(0.35f, 0.36f, 0.38f);
                                        env.sunIntensity = 2.5f;
                                        break;
                                    case 2: // City
                                        env.sunColor = glm::vec3(1.0f, 0.96f, 0.88f);
                                        env.sunDirection = glm::normalize(glm::vec3(0.40f, 0.75f, -0.30f));
                                        env.ambientColor = glm::vec3(0.18f, 0.22f, 0.28f);
                                        env.sunIntensity = 3.2f;
                                        break;
                                    case 3: // Courtyard
                                        env.sunColor = glm::vec3(1.0f, 0.98f, 0.92f);
                                        env.sunDirection = glm::normalize(glm::vec3(0.35f, 0.85f, 0.20f));
                                        env.ambientColor = glm::vec3(0.20f, 0.24f, 0.30f);
                                        env.sunIntensity = 3.6f;
                                        break;
                                    case 4: // Forest
                                        env.sunColor = glm::vec3(1.0f, 0.94f, 0.82f);
                                        env.sunDirection = glm::normalize(glm::vec3(-0.45f, 0.65f, 0.45f));
                                        env.ambientColor = glm::vec3(0.12f, 0.25f, 0.14f);
                                        env.sunIntensity = 3.0f;
                                        break;
                                    case 5: // Interior
                                        env.sunColor = glm::vec3(1.0f, 0.92f, 0.78f);
                                        env.sunDirection = glm::normalize(glm::vec3(0.20f, 0.92f, 0.20f));
                                        env.ambientColor = glm::vec3(0.30f, 0.26f, 0.22f);
                                        env.sunIntensity = 2.2f;
                                        break;
                                    case 6: // Night
                                        env.sunColor = glm::vec3(0.65f, 0.80f, 1.0f);
                                        env.sunDirection = glm::normalize(glm::vec3(-0.60f, 0.55f, -0.40f));
                                        env.ambientColor = glm::vec3(0.02f, 0.03f, 0.06f);
                                        env.sunIntensity = 0.8f;
                                        break;
                                    case 7: // Sunrise
                                    default:
                                        env.sunColor = glm::vec3(1.0f, 0.82f, 0.60f);
                                        env.sunDirection = glm::normalize(glm::vec3(-0.80f, 0.10f, 0.35f));
                                        env.ambientColor = glm::vec3(0.24f, 0.16f, 0.20f);
                                        env.sunIntensity = 3.8f;
                                        break;
                                }
                                engine->setEnvironmentSettings(env);
                            }
                        }

                        ImGui::InputTextWithHint("##CustomHdri", editorText("ui.path_to_exr_hdr_file_e_g_c_maps_sky_exr", "Path to .exr / .hdr file (e.g. C:/maps/sky.exr)"), m_customHdriPath, sizeof(m_customHdriPath));
                        ImGui::SameLine();
                        if (ImGui::Button(editorText("ui.load_hdri_exr", "Load HDRI/EXR"))) {
                            if (m_customHdriPath[0] != '\0') {
                                loadEnvironmentHdriFile(m_customHdriPath);
                            }
                        }

                        bool hdriEnvironmentEnabled = m_skyboxEnabled || m_iblEnabled;
                        if (ImGui::Checkbox(editorText("details.hdri_enabled", "HDRI Environment Enabled (Skybox + IBL)"), &hdriEnvironmentEnabled)) {
                            m_skyboxEnabled = hdriEnvironmentEnabled;
                            m_iblEnabled = hdriEnvironmentEnabled;
                            if (engine) {
                                auto env = engine->getEnvironmentSettings();
                                env.skyboxEnabled = hdriEnvironmentEnabled;
                                env.iblEnabled = hdriEnvironmentEnabled;
                                engine->setEnvironmentSettings(env);
                            }
                        }
                        if (ImGui::Checkbox(editorText("details.hdri_skybox", "Render HDRI Skybox & Solar Disk"), &m_skyboxEnabled)) {
                            if (engine) {
                                auto env = engine->getEnvironmentSettings();
                                env.skyboxEnabled = m_skyboxEnabled;
                                engine->setEnvironmentSettings(env);
                            }
                        }
                        if (ImGui::Checkbox(editorText("details.hdri_ibl", "Use HDRI for IBL (Diffuse Lighting + Reflections)"), &m_iblEnabled)) {
                            if (engine) {
                                auto env = engine->getEnvironmentSettings();
                                env.iblEnabled = m_iblEnabled;
                                engine->setEnvironmentSettings(env);
                            }
                        }

                        if (engine) {
                            auto env = engine->getEnvironmentSettings();
                            env.skyboxEnabled = m_skyboxEnabled;
                            env.iblEnabled = m_iblEnabled;
                            bool changed = false;

                            // Find Directional Light (Sun) entity in the scene
                            entt::entity sunEntity = entt::null;
                            auto dirLightView = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
                            for (auto dlEntity : dirLightView) {
                                const auto& lc = dirLightView.get<ecs::LightComponent>(dlEntity);
                                if (lc.type == "directional" || lc.type == "sun") {
                                    sunEntity = dlEntity;
                                    break;
                                }
                            }

                            ImGui::Spacing();
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Slonce / Swiatlo kierunkowe (Directional Sun):");

                            if (ImGui::Checkbox(editorText("details.sun_enabled_world", "Sun Enabled##World"), &env.sunEnabled)) changed = true;
                            if (ImGui::Checkbox(editorText("details.sun_direction_world", "Direction From Position##World"), &env.sunDirectionFromPosition)) changed = true;
                            ImGui::SameLine();
                            ImGui::TextDisabled("(?)");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip(editorText("details.sun_direction_tooltip", "When enabled: the sun disk in the sky is located exactly at the Sun object in 3D space.\nWhen disabled: the sun is infinitely distant and its direction comes from rotation (Pitch/Yaw)."));
                            }

                            if (sunEntity != entt::null) {
                                auto& sunLt = registry.get().get<ecs::TransformComponent>(sunEntity);
                                auto& sunLc = registry.get().get<ecs::LightComponent>(sunEntity);

                                if (ImGui::ColorEdit3("Sun Color", (float*)&sunLc.color)) {
                                    env.sunColor = sunLc.color;
                                    changed = true;
                                }
                                if (ImGui::DragFloat("Sun Intensity", &sunLc.intensity, 0.1f, 0.0f, 50.0f, "%.2f")) {
                                    env.sunIntensity = sunLc.intensity;
                                    changed = true;
                                }

                                ImGui::Spacing();
                                ImGui::Text(editorText("details.sun_direction_label", "Sun direction:"));
                                if (ImGui::SliderFloat(editorText("details.elevation_world", "Elevation (Pitch)##World"), &sunLt.rotation.x, -89.0f, 89.0f, "%.1f deg")) changed = true;
                                if (ImGui::SliderFloat(editorText("details.azimuth_world", "Azimuth (Yaw)##World"), &sunLt.rotation.y, 0.0f, 360.0f, "%.1f deg")) changed = true;

                                const float pitchRad = glm::radians(sunLt.rotation.x);
                                const float yawRad   = glm::radians(sunLt.rotation.y);
                                const glm::vec3 sunForward = glm::normalize(glm::vec3(std::cos(yawRad) * std::cos(pitchRad), std::sin(pitchRad), std::sin(yawRad) * std::cos(pitchRad)));
                                const glm::vec3 sunDir = -sunForward;
                                env.sunDirection = sunDir;
                                ImGui::TextDisabled(editorText("details.sun_vector", "Sun vector: (%.2f, %.2f, %.2f)"), sunDir.x, sunDir.y, sunDir.z);

                                ImGui::Text(editorText("ui.time_of_day", "Time of day:"));
                                if (ImGui::SmallButton(editorText("details.noon", "Noon##World"))) { sunLt.rotation.x = -75.0f; sunLt.rotation.y = 0.0f; changed = true; }
                                ImGui::SameLine();
                                if (ImGui::SmallButton(editorText("details.morning", "Morning##World"))) { sunLt.rotation.x = -20.0f; sunLt.rotation.y = 80.0f; changed = true; }
                                ImGui::SameLine();
                                if (ImGui::SmallButton(editorText("details.golden_hour", "Golden hour##World"))) { sunLt.rotation.x = -12.0f; sunLt.rotation.y = 240.0f; changed = true; }
                                ImGui::SameLine();
                                if (ImGui::SmallButton(editorText("details.sunset", "Sunset##World"))) { sunLt.rotation.x = -3.0f; sunLt.rotation.y = 265.0f; changed = true; }

                                ImGui::Spacing();
                                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Pozycja slonca 3D:");
                                if (drawEditorVec3Control("Sun Pos (XYZ)##World", sunLt.position, 0.0f)) changed = true;
                                if (ImGui::DragFloat("Sun Height (Y)##World", &sunLt.position.y, 0.5f, -200.0f, 2000.0f, "%.1f m")) changed = true;

                                if (changed) {
                                    sunLt.orientation = glm::quat(glm::radians(sunLt.rotation));
                                    engine->syncEntityTransformPhysics(sunEntity);
                                }
                            } else {
                                if (ImGui::ColorEdit3("Sun Color", (float*)&env.sunColor)) changed = true;
                                if (ImGui::DragFloat("Sun Intensity", &env.sunIntensity, 0.1f, 0.0f, 25.0f, "%.2f")) changed = true;
                                if (drawEditorVec3Control("Sun Direction##World", env.sunDirection, 0.0f)) {
                                    env.sunDirection = glm::normalize(env.sunDirection);
                                    changed = true;
                                }
                            }

                            ImGui::Spacing();
                            if (ImGui::ColorEdit3("Ambient Tint", (float*)&env.ambientColor)) changed = true;
                            if (ImGui::DragFloat("IBL Intensity", &env.ambientIntensity, 0.05f, 0.0f, 5.0f, "%.2f")) changed = true;
                            if (changed) engine->setEnvironmentSettings(env);
                        }
                    }

                    if (ImGui::CollapsingHeader("ACES Tonemapping & 5-Mip Jimenez Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::SliderFloat(editorText("details.exposure_ev", "Exposure (EV)"), &m_exposure, 0.15f, 4.0f, "%.2f");
                        ImGui::Checkbox(editorText("details.bloom", "5-Mip Jimenez Bloom (Karis Average)"), &m_bloomEnabled);
                        if (m_bloomEnabled) {
                            ImGui::SliderFloat(editorText("details.bloom_intensity", "Bloom Intensity"), &m_bloomIntensity, 0.0f, 3.0f, "%.2f");
                            ImGui::SliderFloat(editorText("details.bloom_threshold", "Bloom Threshold"), &m_bloomThreshold, 0.2f, 5.0f, "%.2f");
                        }
                    }

                    if (ImGui::CollapsingHeader("Indirect Diffuse GI (SSGI & Ground Bounce)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox(editorText("details.gi_bounce_active", "GI & Bounce Active (Global Illumination)"), &m_lightBounceEnabled);
                        if (m_lightBounceEnabled) {
                            ImGui::SliderFloat(editorText("details.bounce_intensity", "Bounce Intensity"), &m_lightBounceIntensity, 0.0f, 2.0f, "%.2f");
                            ImGui::SliderFloat(editorText("details.ground_albedo", "Ground Albedo"), &m_lightBounceGroundReflectance, 0.0f, 1.0f, "%.2f");
                            ImGui::ColorEdit3("Ground Color (sRGB)", &m_lightBounceGroundColor.x);
                            ImGui::TextDisabled(editorText("details.ssgi_description", "SSGI: Color bleed from every object and texture"));
                            ImGui::TextDisabled(editorText("details.ground_bounce_description", "Ground Bounce: Ground reflection with sun shadow and 1/PI"));
                        }
                    }

                    if (ImGui::CollapsingHeader("Multi-Bounce GTAO (Depth Prepass)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox(editorText("details.gtao_active", "GTAO Active"), &m_gtaoEnabled);
                        if (m_gtaoEnabled) {
                            ImGui::SliderFloat(editorText("details.ao_world_radius", "AO World Radius"), &m_gtaoRadius, 0.1f, 3.0f, "%.2f m");
                            ImGui::SliderFloat(editorText("details.ao_power", "AO Power"), &m_gtaoIntensity, 0.2f, 4.0f, "%.2f");
                        }
                    }

                    if (ImGui::CollapsingHeader("Exponential Height Fog (Mie Phase)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox(editorText("details.height_fog_active", "Height Fog Active"), &m_fogEnabled);
                        if (m_fogEnabled) {
                            ImGui::SliderFloat(editorText("details.fog_density", "Fog Density"), &m_fogDensity, 0.001f, 0.10f, "%.4f");
                            ImGui::SliderFloat(editorText("details.height_falloff", "Height Falloff"), &m_fogHeightFalloff, 0.01f, 0.50f, "%.3f");
                            ImGui::ColorEdit3("Fog Inscatter Color", &m_fogColor.x);
                        }
                    }

                    if (ImGui::CollapsingHeader("Shadows (4096px 4-Cascade Vogel PCF)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox(editorText("details.soft_shadows", "Soft Shadows"), &m_softShadows);
                        ImGui::SliderFloat(editorText("details.csm_distance", "CSM Distance"), &m_shadowDistance, 25.0f, 200.0f, "%.1f m");
                    }

                    if (ImGui::CollapsingHeader("FXAA 3.11 & Resolution Scaling", ImGuiTreeNodeFlags_DefaultOpen)) {
                        const char* aaModes[] = {
                            "0: Off (Brak AA)",
                            "1: FXAA 3.11 Fast",
                            "2: FXAA 3.11 Quality (Subpixel)",
                            "3: FXAA 3.11 Ultra"
                        };
                        ImGui::Combo(editorText("details.aa_preset", "AA Preset"), &m_antiAliasingMode, aaModes, IM_ARRAYSIZE(aaModes));

                        const char* resPresets[] = {
                            "0: Auto 1:1 Viewport",
                            "1: 4K UHD (3840x2160)",
                            "2: 1440p QHD (2560x1440)",
                            "3: 1080p FHD (1920x1080)",
                            "4: 720p HD (1280x720)"
                        };
                        if (ImGui::Combo(editorText("details.resolution_mode", "Resolution Mode"), &m_resolutionPresetIndex, resPresets, IM_ARRAYSIZE(resPresets))) {
                            if (m_resolutionPresetIndex == 0) m_autoViewportResolution = true;
                            else {
                                m_autoViewportResolution = false;
                                if (m_resolutionPresetIndex == 1) requestResolution(3840, 2160);
                                else if (m_resolutionPresetIndex == 2) requestResolution(2560, 1440);
                                else if (m_resolutionPresetIndex == 3) requestResolution(1920, 1080);
                                else if (m_resolutionPresetIndex == 4) requestResolution(1280, 720);
                            }
                        }

                        float resSc = m_resolutionScale;
                        if (ImGui::SliderFloat(editorText("details.resolution_scale", "Resolution Scale"), &resSc, 0.25f, 2.00f, "%.2fx")) {
                            setResolutionScale(resSc);
                            if (m_autoViewportResolution && viewportPanelSize.x > 10.0f) {
                                requestResolution(
                                    (static_cast<uint32_t>(viewportPanelSize.x * m_resolutionScale) + 1u) & ~1u,
                                    (static_cast<uint32_t>(viewportPanelSize.y * m_resolutionScale) + 1u) & ~1u);
                            }
                            if (engine) engine->getPreferences().resolutionScale = m_resolutionScale;
                        }
                        if (ImGui::Button("50%##ResSc")) { setResolutionScale(0.50f); if (m_autoViewportResolution && viewportPanelSize.x > 10.0f) requestResolution((static_cast<uint32_t>(viewportPanelSize.x * 0.50f) + 1u) & ~1u, (static_cast<uint32_t>(viewportPanelSize.y * 0.50f) + 1u) & ~1u); }
                        ImGui::SameLine();
                        if (ImGui::Button("67%##ResSc")) { setResolutionScale(0.67f); if (m_autoViewportResolution && viewportPanelSize.x > 10.0f) requestResolution((static_cast<uint32_t>(viewportPanelSize.x * 0.67f) + 1u) & ~1u, (static_cast<uint32_t>(viewportPanelSize.y * 0.67f) + 1u) & ~1u); }
                        ImGui::SameLine();
                        if (ImGui::Button("75%##ResSc")) { setResolutionScale(0.75f); if (m_autoViewportResolution && viewportPanelSize.x > 10.0f) requestResolution((static_cast<uint32_t>(viewportPanelSize.x * 0.75f) + 1u) & ~1u, (static_cast<uint32_t>(viewportPanelSize.y * 0.75f) + 1u) & ~1u); }
                        ImGui::SameLine();
                        if (ImGui::Button("100%##ResSc")) { setResolutionScale(1.00f); if (m_autoViewportResolution && viewportPanelSize.x > 10.0f) requestResolution((static_cast<uint32_t>(viewportPanelSize.x * 1.00f) + 1u) & ~1u, (static_cast<uint32_t>(viewportPanelSize.y * 1.00f) + 1u) & ~1u); }
                        ImGui::SliderFloat(editorText("details.ui_scale", "UI Scale"), &m_uiScale, 0.75f, 1.65f, "%.2fx");
                    }
                }

                ImGui::EndDisabled();
                ImGui::End();
                ImGui::PopStyleColor();
            }

}
} // namespace engine::renderer
#endif
