#include "renderer/VulkanRenderer.hpp"
#include "renderer/EditorUiControls.hpp"
#include "core/Engine.hpp"
#include "ecs/Components.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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

                // Equal-width inspector tabs with a persistent active state.
                const float inspectorTabGap = 4.0f * uiScale;
                const float inspectorTabWidth = (ImGui::GetContentRegionAvail().x - inspectorTabGap) * 0.5f;
                const ImVec4 tabActive = ImVec4(0.10f, 0.34f, 0.58f, 1.0f);
                const ImVec4 tabInactive = ImVec4(0.085f, 0.095f, 0.125f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, m_detailsTab == 0 ? tabActive : tabInactive);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.13f, 0.40f, 0.68f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.30f, 0.52f, 1.0f));
                if (ImGui::Button("Details", ImVec2(inspectorTabWidth, 26.0f * uiScale))) m_detailsTab = 0;
                ImGui::PopStyleColor(3);
                ImGui::SameLine(0.0f, inspectorTabGap);
                ImGui::PushStyleColor(ImGuiCol_Button, m_detailsTab == 1 ? tabActive : tabInactive);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.13f, 0.40f, 0.68f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.30f, 0.52f, 1.0f));
                if (ImGui::Button("World & Graphics", ImVec2(inspectorTabWidth, 26.0f * uiScale))) m_detailsTab = 1;
                ImGui::PopStyleColor(3);
                ImGui::Separator();

                if (m_detailsTab == 0) {
                    if (selectedEntity == entt::null || !registry.get().valid(selectedEntity)) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Wybierz obiekt ze sceny");
                        ImGui::TextDisabled("Kliknij dowolny obiekt w oknie 3D lub w Outlinerze powyzej,");
                        ImGui::TextDisabled("aby wyswietlic pelne wlasciwosci (Transform, Siatka, PBR, Fizyka, Chemia, Swiatlo).");
                        ImGui::Spacing();
                        ImGui::SeparatorText("Szybkie spawnowanie obiektow");
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
                        ImGui::TextDisabled("Mozesz takze przelaczyc sie na zakladke 'World & Graphics' u gory,");
                        ImGui::TextDisabled("aby sterowac Niebem HDRI, Sloncem, Mgla i Ray Tracingiem.");
                    } else {
                        std::string tagStr = registry.get().all_of<ecs::TagComponent>(selectedEntity)
                            ? registry.get().get<ecs::TagComponent>(selectedEntity).name
                            : "Actor";
                        char nameBuf[64];
                        strncpy(nameBuf, tagStr.c_str(), sizeof(nameBuf) - 1);
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
                        if (ImGui::Button("+ Komponent", ImVec2(0, 0))) {
                            ImGui::OpenPopup("AddComponentPopup");
                        }
                        if (ImGui::BeginPopup("AddComponentPopup")) {
                            ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "Dodaj komponent:");
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
                            ImGui::EndPopup();
                        }
                        ImGui::Separator();

                        if (registry.get().all_of<ecs::TransformComponent>(selectedEntity)) {
                            auto& t = registry.get().get<ecs::TransformComponent>(selectedEntity);
                            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                                const bool pCh = drawEditorVec3Control("Location", t.position, 0.0f);
                                const bool rCh = drawEditorVec3Control("Rotation", t.rotation, 0.0f);
                                const bool sCh = drawEditorVec3Control("Scale", t.scale, 1.0f);
                                if (rCh) t.orientation = glm::quat(glm::radians(t.rotation));
                                if ((pCh || rCh || sCh) && engine) engine->syncEntityTransformPhysics(selectedEntity);

                                ImGui::Spacing();
                                if (ImGui::Button(" Zresetuj pozycje (0, 0, 0) ")) {
                                    t.position = glm::vec3(0.0f);
                                    if (engine) engine->syncEntityTransformPhysics(selectedEntity);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(" Skup kamere [F] ")) {
                                    if (engine) engine->setCamera(t.position - cameraForward * 8.0f, cameraYaw, cameraPitch, cameraFov);
                                }
                            }
                        }

                        if (registry.get().all_of<ecs::LightComponent>(selectedEntity)) {
                            auto& lc = registry.get().get<ecs::LightComponent>(selectedEntity);
                            if (ImGui::CollapsingHeader("Light Component (PBR)", ImGuiTreeNodeFlags_DefaultOpen)) {
                                const char* lightTypes[] = { "point", "sun", "spot", "area" };
                                int curType = (lc.type == "directional" || lc.type == "sun") ? 1 : (lc.type == "spot" ? 2 : (lc.type == "area" ? 3 : 0));
                                if (ImGui::Combo("Light Type", &curType, "Point Light\0Directional (Sun)\0Spot Light\0Area Light (Rect)\0\0")) {
                                    lc.type = lightTypes[curType];
                                }
                                ImGui::ColorEdit3("Color", (float*)&lc.color);
                                ImGui::DragFloat("Intensity", &lc.intensity, 0.1f, 0.0f, 100.0f, "%.2f");

                                if (curType == 0) { // Point
                                    ImGui::DragFloat("Range (m)", &lc.range, 0.5f, 1.0f, 200.0f, "%.1f m");
                                } else if (curType == 1) { // Sun / Directional
                                    if (registry.get().all_of<ecs::TransformComponent>(selectedEntity)) {
                                        auto& lt = registry.get().get<ecs::TransformComponent>(selectedEntity);
                                        bool changed = false;

                                        ImGui::Spacing();
                                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Kierunek padania slonca (Sun Direction):");
                                        if (ImGui::SliderFloat("Elevation (Pitch)", &lt.rotation.x, -89.0f, 89.0f, "%.1f deg")) changed = true;
                                        if (ImGui::SliderFloat("Azimuth (Yaw)", &lt.rotation.y, 0.0f, 360.0f, "%.1f deg")) changed = true;

                                        const float pitchRad = glm::radians(lt.rotation.x);
                                        const float yawRad   = glm::radians(lt.rotation.y);
                                        const glm::vec3 sunForward = glm::normalize(glm::vec3(std::cos(yawRad) * std::cos(pitchRad), std::sin(pitchRad), std::sin(yawRad) * std::cos(pitchRad)));
                                        const glm::vec3 currentSunDir = -sunForward;
                                        ImGui::TextDisabled("Wektor swiatla: (%.2f, %.2f, %.2f)", currentSunDir.x, currentSunDir.y, currentSunDir.z);

                                        ImGui::Text("Pory dnia:");
                                        if (ImGui::SmallButton("Poludnie (Noon)")) {
                                            lt.rotation.x = -75.0f; lt.rotation.y = 0.0f; changed = true;
                                        }
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton("Poranek")) {
                                            lt.rotation.x = -20.0f; lt.rotation.y = 80.0f; changed = true;
                                        }
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton("Zlota godzina")) {
                                            lt.rotation.x = -12.0f; lt.rotation.y = 240.0f; changed = true;
                                        }
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton("Zachod")) {
                                            lt.rotation.x = -3.0f; lt.rotation.y = 265.0f; changed = true;
                                        }

                                        ImGui::Spacing();
                                        if (engine) {
                                            auto env = engine->getEnvironmentSettings();
                                            if (ImGui::Checkbox("Slonce aktywne (Sun Enabled)", &env.sunEnabled)) {
                                                engine->setEnvironmentSettings(env);
                                            }
                                            if (ImGui::Checkbox("Kierunek z pozycji 3D (Direction From Position)", &env.sunDirectionFromPosition)) {
                                                engine->setEnvironmentSettings(env);
                                            }
                                            ImGui::SameLine();
                                            ImGui::TextDisabled("(?)");
                                            if (ImGui::IsItemHovered()) {
                                                ImGui::SetTooltip("Gdy wlaczone: tarcza slonca na niebie znajduje sie dokladnie w miejscu obiektu Slonca w przestrzeni 3D.\nGdy wylaczone: slonce jest w nieskonczonosci, a kierunek wynika z rotacji (Pitch/Yaw).");
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
                                    ImGui::DragFloat("Range (m)", &lc.range, 0.5f, 1.0f, 200.0f, "%.1f m");
                                    ImGui::SliderFloat("Inner Cone", &lc.innerConeAngle, 1.0f, 80.0f, "%.1f deg");
                                    if (lc.outerConeAngle <= lc.innerConeAngle) lc.outerConeAngle = lc.innerConeAngle + 0.5f;
                                    ImGui::SliderFloat("Outer Cone", &lc.outerConeAngle, lc.innerConeAngle + 0.5f, 89.0f, "%.1f deg");
                                    if (registry.get().all_of<ecs::TransformComponent>(selectedEntity)) {
                                        auto& lt = registry.get().get<ecs::TransformComponent>(selectedEntity);
                                        bool changed = false;
                                        if (ImGui::SliderFloat("Spot Pitch", &lt.rotation.x, -89.0f, 89.0f, "%.1f deg")) changed = true;
                                        if (ImGui::SliderFloat("Spot Yaw", &lt.rotation.y, 0.0f, 360.0f, "%.1f deg")) changed = true;
                                        if (changed) {
                                            lt.orientation = glm::quat(glm::radians(lt.rotation));
                                            if (engine) engine->syncEntityTransformPhysics(selectedEntity);
                                        }
                                    }
                                } else if (curType == 3) { // Area
                                    ImGui::DragFloat("Range (m)", &lc.range, 0.5f, 1.0f, 200.0f, "%.1f m");
                                    ImGui::SliderFloat("Area Width X", &lc.areaSizeX, 0.2f, 20.0f, "%.2f m");
                                    ImGui::SliderFloat("Area Height Y", &lc.areaSizeY, 0.2f, 20.0f, "%.2f m");
                                    if (registry.get().all_of<ecs::TransformComponent>(selectedEntity)) {
                                        auto& lt = registry.get().get<ecs::TransformComponent>(selectedEntity);
                                        bool changed = false;
                                        if (ImGui::SliderFloat("Area Pitch", &lt.rotation.x, -89.0f, 89.0f, "%.1f deg")) changed = true;
                                        if (ImGui::SliderFloat("Area Yaw", &lt.rotation.y, 0.0f, 360.0f, "%.1f deg")) changed = true;
                                        if (changed) {
                                            lt.orientation = glm::quat(glm::radians(lt.rotation));
                                            if (engine) engine->syncEntityTransformPhysics(selectedEntity);
                                        }
                                    }
                                }
                            }
                        }

                        if (registry.get().all_of<ecs::MeshComponent>(selectedEntity)) {
                            auto& mc = registry.get().get<ecs::MeshComponent>(selectedEntity);
                            if (ImGui::CollapsingHeader("Static Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
                                const char* meshes[] = { "None", "Plane (Ground)", "Cube (1x1x1)", "Sphere (R=0.5)", "Cylinder", "Capsule" };
                                int curMesh = std::clamp(static_cast<int>(mc.meshId), 0, 5);
                                if (ImGui::Combo("Mesh Asset", &curMesh, meshes, IM_ARRAYSIZE(meshes))) {
                                    mc.meshId = static_cast<uint32_t>(curMesh);
                                }
                            }
                        }

                        // Żywa edycja komponentu materiału PBR (Kulla-Conty Multiscatter + Cubemap IBL)
                        if (registry.get().all_of<ecs::MeshComponent>(selectedEntity)) {
                            if (ImGui::CollapsingHeader("PBR Material (Multiscatter GGX)", ImGuiTreeNodeFlags_DefaultOpen)) {
                                if (!registry.get().all_of<ecs::MaterialComponent>(selectedEntity)) {
                                    if (ImGui::Button("+ Dodaj wlasny Material PBR")) {
                                        ecs::MaterialComponent m{};
                                        m.baseColor = glm::vec4(0.85f, 0.85f, 0.90f, 1.0f);
                                        m.metallic = 0.0f;
                                        m.roughness = 0.35f;
                                        m.emissiveIntensity = 0.0f;
                                        registry.get().emplace<ecs::MaterialComponent>(selectedEntity, m);
                                    }
                                } else {
                                    auto& mat = registry.get().get<ecs::MaterialComponent>(selectedEntity);
                                    ImGui::ColorEdit3("Base Color (Albedo)", &mat.baseColor.x);
                                    ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f, "%.2f");
                                    ImGui::SliderFloat("Roughness", &mat.roughness, 0.045f, 1.0f, "%.3f");
                                    ImGui::DragFloat("Emissive HDR", &mat.emissiveIntensity, 0.05f, 0.0f, 25.0f, "%.2f");
                                }
                            }
                        }

                        if (registry.get().all_of<ecs::RigidBodyComponent>(selectedEntity)) {
                            auto& rb = registry.get().get<ecs::RigidBodyComponent>(selectedEntity);
                            if (ImGui::CollapsingHeader("Jolt Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
                                ImGui::DragFloat("Mass (kg)", &rb.mass, 0.1f, 0.0f, 500.0f);
                                ImGui::SliderFloat("Restitution", &rb.restitution, 0.0f, 1.0f);
                                ImGui::SliderFloat("Friction", &rb.friction, 0.0f, 1.0f);
                                ImGui::Checkbox("Is Sensor (Trigger)", &rb.isSensor);
                            }
                        }

                        if (registry.get().all_of<ecs::ParticleEmitterComponent>(selectedEntity)) {
                            auto& pe = registry.get().get<ecs::ParticleEmitterComponent>(selectedEntity);
                            if (ImGui::CollapsingHeader("Particle Emitter (HDR VFX)", ImGuiTreeNodeFlags_DefaultOpen)) {
                                const char* pTypes[] = { "fire", "sparks", "smoke", "portal", "explosion" };
                                int curP = 0;
                                for (int i = 0; i < 5; ++i) { if (pe.type == pTypes[i]) { curP = i; break; } }
                                if (ImGui::Combo("Particle Type", &curP, "Fire\0Sparks\0Smoke\0Portal\0Explosion\0\0")) {
                                    pe.type = pTypes[curP];
                                }
                                ImGui::ColorEdit3("Particle Color", &pe.color.x);
                                int pCount = static_cast<int>(pe.count);
                                if (ImGui::SliderInt("Particle Count", &pCount, 10, 1000)) pe.count = static_cast<uint32_t>(pCount);
                                ImGui::SliderFloat("Speed", &pe.speed, 0.1f, 20.0f, "%.1f m/s");
                                ImGui::SliderFloat("Size", &pe.size, 0.05f, 2.0f, "%.2f m");
                            }
                        }
                    }
                } else {
                    // Hardware ray-query effects are optional; raster shadows remain the fallback.
                    if (ImGui::CollapsingHeader("Hardware Ray Tracing (Vulkan Ray Query)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        const bool rtSupported = m_context && m_context->isRayTracingSupported();
                        if (!rtSupported) {
                            ImGui::TextDisabled("Ray tracing unavailable: GPU/driver lacks required Vulkan KHR features.");
                        }
                        ImGui::BeginDisabled(!rtSupported);
                        ImGui::Checkbox("Enable Hardware Ray Tracing", &m_rtSettings.enabled);
                        ImGui::BeginDisabled(!m_rtSettings.enabled);
                        ImGui::Checkbox("Ray-Traced Shadows (Sun + Point + Spot)", &m_rtSettings.shadowsEnabled);
                        ImGui::Checkbox("Ray-Traced Ambient Occlusion", &m_rtSettings.aoEnabled);
                        ImGui::Checkbox("Ray-Traced Reflections (Scene Geometry)", &m_rtSettings.reflectionsEnabled);
                        ImGui::BeginDisabled(!m_rtSettings.reflectionsEnabled);
                        ImGui::SliderInt("RT Reflection Samples", &m_rtSettings.reflectionSamples, 1, 16, "%d");
                        ImGui::Checkbox("Infinite Bounce (adaptive, max 16)", &m_rtSettings.reflectionInfiniteBounces);
                        ImGui::BeginDisabled(m_rtSettings.reflectionInfiniteBounces);
                        ImGui::SliderInt("RT Reflection Bounces", &m_rtSettings.reflectionBounces, 2, 4, "%d");
                        ImGui::EndDisabled();
                        ImGui::SliderFloat("Reflection Continuation Roughness", &m_rtSettings.reflectionMaxRoughness, 0.05f, 1.0f, "%.2f");
                        ImGui::SliderFloat(m_rtSettings.reflectionInfiniteBounces ? "Russian Roulette Energy Threshold" : "Reflection Min Throughput", &m_rtSettings.reflectionMinThroughput, 0.01f, 0.25f, "%.3f");
                        if (m_rtSettings.reflectionInfiniteBounces)
                            ImGui::TextDisabled("Adaptive path termination; hard safety limit: 16 surface hits.");
                        ImGui::Checkbox("Roughness Checkerboard (Variant 1)", &m_rtSettings.reflectionCheckerboard);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Smooth surfaces (roughness <= 0.25) traced at full 1:1; rough surfaces traced on alternating checkerboard and bilaterally reconstructed.");
                        ImGui::Checkbox("Stochastic Lights on Bounces (Variant 2)", &m_rtSettings.reflectionStochasticLights);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("On secondary bounces (bounce >= 1), importance-samples 1 light instead of testing all 8 lights, cutting shadow rays from 9 to 2.");
                        ImGui::Checkbox("Bounce Depth Mip LOD Bias (Variant 3)", &m_rtSettings.reflectionLodBias);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Increases texture mip LOD with bounce depth (textureLod float(bounce)) to reduce GPU texture cache thrashing.");
                        ImGui::Checkbox("Global Half-Res Reflections (Variant 4)", &m_rtSettings.reflectionHalfRes);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Forces all reflections into half-resolution with edge-aware bilateral normal/depth reconstruction.");
                        ImGui::EndDisabled();
                        ImGui::Checkbox("Ray-Traced Diffuse GI", &m_rtSettings.giEnabled);
                        ImGui::BeginDisabled(!m_rtSettings.giEnabled);
                        ImGui::SliderFloat("RT GI Intensity", &m_rtSettings.giIntensity, 0.0f, 3.0f, "%.2f");
                        ImGui::SliderInt("RT Diffuse GI Bounces", &m_rtSettings.giBounces, 1, 15, "%d");
                        ImGui::Checkbox("GI Checkerboard (Half-Rate Trace)", &m_rtSettings.giCheckerboard);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Traces GI on alternating checkerboard pixels, cutting ray tracing work by 50% and reconstructing via temporal history & bilateral filter.");
                        ImGui::TextDisabled("Resolution: Half-Res Diffuse GI (4x compute speedup)");
                        ImGui::EndDisabled();

                        // GI + AO are traced in compute, then temporally accumulated and denoised.
                        ImGui::SeparatorText("RT Sampling & Denoising");
                        ImGui::BeginDisabled(!m_rtSettings.giEnabled && !m_rtSettings.aoEnabled);
                        ImGui::SliderInt("RT GI/AO Samples per Pixel", &m_rtSettings.giSamples, 1, 16, "%d");
                        ImGui::Checkbox("Temporal + Spatial Denoiser", &m_rtSettings.denoiseEnabled);
                        ImGui::BeginDisabled(!m_rtSettings.denoiseEnabled);
                        ImGui::SliderInt("Temporal History (samples)", &m_rtSettings.maxHistory, 4, 256, "%d");
                        ImGui::SliderFloat("Spatial Filter Strength", &m_rtSettings.spatialStrength, 0.0f, 1.0f, "%.2f");
                        ImGui::Checkbox("SVGF (Variance-Guided Filter)", &m_rtSettings.svgfEnabled);
                        ImGui::EndDisabled();

                        // Debug Visualization Modes
                        ImGui::SeparatorText("RT Debug & Diagnostics");
                        const char* debugModes[] = {
                            "0: Off (Composite Render)",
                            "1: Raw Indirect Trace (imgS)",
                            "2: History Length (0..maxHistory)",
                            "3: Octahedral Geometric Normals",
                            "4: Linear Depth",
                            "5: Ambient Occlusion Only",
                            "6: Specular Reflections Only",
                            "7: Luminance Variance (SVGF)"
                        };
                        int currentDebugMode = static_cast<int>(m_rtSettings.debugMode);
                        if (ImGui::Combo("RT Debug View", &currentDebugMode, debugModes, IM_ARRAYSIZE(debugModes))) {
                            m_rtSettings.debugMode = static_cast<RtDebugMode>(currentDebugMode);
                        }

                        // RT GPU Timers
                        const auto& timings = m_rtDenoiser.getGpuTimings();
                        if (ImGui::TreeNode("RT GPU Profiler (Compute Timers)")) {
                            ImGui::Text("GI Trace Pass:    %.3f ms", timings.traceMs);
                            ImGui::Text("Refl Trace Pass:  %.3f ms", timings.reflTraceMs);
                            ImGui::Text("GI Temporal:      %.3f ms", timings.temporalMs);
                            ImGui::Text("Refl Temporal:    %.3f ms", timings.reflTemporalMs);
                            ImGui::Text("Atrous Step 1:    %.3f ms", timings.atrous0Ms);
                            ImGui::Text("Atrous Step 2:    %.3f ms", timings.atrous1Ms);
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
                        if (ImGui::Combo("HDRI Environment", &currentHdri, hdriItems, IM_ARRAYSIZE(hdriItems))) {
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

                        ImGui::InputTextWithHint("##CustomHdri", "Sciezka do pliku .exr / .hdr (np. C:/maps/sky.exr)", m_customHdriPath, sizeof(m_customHdriPath));
                        ImGui::SameLine();
                        if (ImGui::Button("Wczytaj HDRI/EXR")) {
                            if (m_customHdriPath[0] != '\0') {
                                loadEnvironmentHdriFile(m_customHdriPath);
                            }
                        }

                        bool hdriEnvironmentEnabled = m_skyboxEnabled || m_iblEnabled;
                        if (ImGui::Checkbox("HDRI Environment Enabled (Skybox + IBL)", &hdriEnvironmentEnabled)) {
                            m_skyboxEnabled = hdriEnvironmentEnabled;
                            m_iblEnabled = hdriEnvironmentEnabled;
                            if (engine) {
                                auto env = engine->getEnvironmentSettings();
                                env.skyboxEnabled = hdriEnvironmentEnabled;
                                env.iblEnabled = hdriEnvironmentEnabled;
                                engine->setEnvironmentSettings(env);
                            }
                        }
                        if (ImGui::Checkbox("Render HDRI Skybox & Solar Disk", &m_skyboxEnabled)) {
                            if (engine) {
                                auto env = engine->getEnvironmentSettings();
                                env.skyboxEnabled = m_skyboxEnabled;
                                engine->setEnvironmentSettings(env);
                            }
                        }
                        if (ImGui::Checkbox("Use HDRI for IBL (Diffuse Lighting + Reflections)", &m_iblEnabled)) {
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

                            if (ImGui::Checkbox("Slonce aktywne (Sun Enabled)##World", &env.sunEnabled)) changed = true;
                            if (ImGui::Checkbox("Kierunek z pozycji 3D##World", &env.sunDirectionFromPosition)) changed = true;
                            ImGui::SameLine();
                            ImGui::TextDisabled("(?)");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Gdy wlaczone: tarcza slonca na niebie znajduje sie dokladnie w miejscu obiektu Slonca w przestrzeni 3D.\nGdy wylaczone: slonce jest w nieskonczonosci, a kierunek wynika z rotacji (Pitch/Yaw).");
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
                                ImGui::Text("Kierunek padania:");
                                if (ImGui::SliderFloat("Elevation (Pitch)##World", &sunLt.rotation.x, -89.0f, 89.0f, "%.1f deg")) changed = true;
                                if (ImGui::SliderFloat("Azimuth (Yaw)##World", &sunLt.rotation.y, 0.0f, 360.0f, "%.1f deg")) changed = true;

                                const float pitchRad = glm::radians(sunLt.rotation.x);
                                const float yawRad   = glm::radians(sunLt.rotation.y);
                                const glm::vec3 sunForward = glm::normalize(glm::vec3(std::cos(yawRad) * std::cos(pitchRad), std::sin(pitchRad), std::sin(yawRad) * std::cos(pitchRad)));
                                const glm::vec3 sunDir = -sunForward;
                                env.sunDirection = sunDir;
                                ImGui::TextDisabled("Wektor slonca: (%.2f, %.2f, %.2f)", sunDir.x, sunDir.y, sunDir.z);

                                ImGui::Text("Pory dnia:");
                                if (ImGui::SmallButton("Poludnie##World")) { sunLt.rotation.x = -75.0f; sunLt.rotation.y = 0.0f; changed = true; }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Poranek##World")) { sunLt.rotation.x = -20.0f; sunLt.rotation.y = 80.0f; changed = true; }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Zlota godzina##World")) { sunLt.rotation.x = -12.0f; sunLt.rotation.y = 240.0f; changed = true; }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Zachod##World")) { sunLt.rotation.x = -3.0f; sunLt.rotation.y = 265.0f; changed = true; }

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
                        ImGui::SliderFloat("Exposure (EV)", &m_exposure, 0.15f, 4.0f, "%.2f");
                        ImGui::Checkbox("5-Mip Jimenez Bloom (Karis Average)", &m_bloomEnabled);
                        if (m_bloomEnabled) {
                            ImGui::SliderFloat("Bloom Intensity", &m_bloomIntensity, 0.0f, 3.0f, "%.2f");
                            ImGui::SliderFloat("Bloom Threshold", &m_bloomThreshold, 0.2f, 5.0f, "%.2f");
                        }
                    }

                    if (ImGui::CollapsingHeader("Indirect Diffuse GI (SSGI & Ground Bounce)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("GI & Bounce Active (Globalne oswietlenie)", &m_lightBounceEnabled);
                        if (m_lightBounceEnabled) {
                            ImGui::SliderFloat("Bounce Intensity", &m_lightBounceIntensity, 0.0f, 2.0f, "%.2f");
                            ImGui::SliderFloat("Ground Albedo", &m_lightBounceGroundReflectance, 0.0f, 1.0f, "%.2f");
                            ImGui::ColorEdit3("Ground Color (sRGB)", &m_lightBounceGroundColor.x);
                            ImGui::TextDisabled("SSGI: Color bleed od kazdego elementu i tekstury");
                            ImGui::TextDisabled("Ground Bounce: Odbicie podloza z cieniem slonca i 1/PI");
                        }
                    }

                    if (ImGui::CollapsingHeader("Multi-Bounce GTAO (Depth Prepass)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("GTAO Active", &m_gtaoEnabled);
                        if (m_gtaoEnabled) {
                            ImGui::SliderFloat("AO World Radius", &m_gtaoRadius, 0.1f, 3.0f, "%.2f m");
                            ImGui::SliderFloat("AO Power", &m_gtaoIntensity, 0.2f, 4.0f, "%.2f");
                        }
                    }

                    if (ImGui::CollapsingHeader("Exponential Height Fog (Mie Phase)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Height Fog Active", &m_fogEnabled);
                        if (m_fogEnabled) {
                            ImGui::SliderFloat("Fog Density", &m_fogDensity, 0.001f, 0.10f, "%.4f");
                            ImGui::SliderFloat("Height Falloff", &m_fogHeightFalloff, 0.01f, 0.50f, "%.3f");
                            ImGui::ColorEdit3("Fog Inscatter Color", &m_fogColor.x);
                        }
                    }

                    if (ImGui::CollapsingHeader("Shadows (4096px 4-Cascade Vogel PCF)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Soft Shadows", &m_softShadows);
                        ImGui::SliderFloat("CSM Distance", &m_shadowDistance, 25.0f, 200.0f, "%.1f m");
                    }

                    if (ImGui::CollapsingHeader("FXAA 3.11 & Resolution Scaling", ImGuiTreeNodeFlags_DefaultOpen)) {
                        const char* aaModes[] = {
                            "0: Off (Brak AA)",
                            "1: FXAA 3.11 Fast",
                            "2: FXAA 3.11 Quality (Subpixel)",
                            "3: FXAA 3.11 Ultra"
                        };
                        ImGui::Combo("AA Preset", &m_antiAliasingMode, aaModes, IM_ARRAYSIZE(aaModes));

                        const char* resPresets[] = {
                            "0: Auto 1:1 Viewport",
                            "1: 4K UHD (3840x2160)",
                            "2: 1440p QHD (2560x1440)",
                            "3: 1080p FHD (1920x1080)",
                            "4: 720p HD (1280x720)"
                        };
                        if (ImGui::Combo("Resolution Mode", &m_resolutionPresetIndex, resPresets, IM_ARRAYSIZE(resPresets))) {
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
                        if (ImGui::SliderFloat("Resolution Scale (Skala renderu)", &resSc, 0.25f, 2.00f, "%.2fx")) {
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
                        ImGui::SliderFloat("UI Scale", &m_uiScale, 0.75f, 1.65f, "%.2fx");
                    }
                }

                ImGui::End();
                ImGui::PopStyleColor();
            }

}
} // namespace engine::renderer
#endif
