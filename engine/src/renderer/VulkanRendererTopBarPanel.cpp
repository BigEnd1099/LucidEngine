#include "renderer/VulkanRenderer.hpp"
#include "renderer/EditorTopBarPanel.hpp"
#include "renderer/EditorUiStyle.hpp"
#include "renderer/EditorLocalization.hpp"
#include "core/Engine.hpp"
#include "ecs/Components.hpp"
#include <cmath>
#include <cstdio>
#ifdef USE_GLFW
#include "imgui.h"
namespace engine::renderer {
void VulkanRenderer::drawEditorTopBarPanel(core::Engine* engine, EditorTopBarUiState& state,
                                           float screenW, float topTitleBarH,
                                           float topLevelTabsH, float topToolbarH,
                                           entt::entity& selectedEntity) {
            static bool s_showShortcutsWindow = false;
            if (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_F1)) {
                s_showShortcutsWindow = !s_showShortcutsWindow;
            }

            // 1. TOP WINDOW BAR
            if (!state.immersivePlay) {
                ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(screenW, topTitleBarH), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 3));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.035f, 0.055f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.035f, 0.035f, 0.055f, 1.0f));
                ImGuiWindowFlags titleFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
                ImGui::Begin("TopTitleBar", nullptr, titleFlags);

                if (ImGui::BeginMenuBar()) {
                    ImDrawList* tdl = ImGui::GetWindowDrawList();
                    ImVec2 badgePos = ImGui::GetCursorScreenPos();
                    const float badgeR = 8.0f * state.uiScale;
                    ImVec2 badgeCenter(badgePos.x + badgeR + 2.0f, badgePos.y + topTitleBarH * 0.42f);
                    tdl->AddCircleFilled(badgeCenter, badgeR, IM_COL32(0, 140, 235, 255), 16);
                    tdl->AddText(ImVec2(badgeCenter.x - 3.5f * state.uiScale, badgeCenter.y - 6.5f * state.uiScale), IM_COL32(255, 255, 255, 255), "L");
                    ImGui::Dummy(ImVec2(badgeR * 2.0f + 4.0f, badgeR * 2.0f));
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "LUCID");
                    ImGui::SameLine(0.0f, 14.0f * state.uiScale);

                    if (ImGui::BeginMenu(editorText("ui.file", "File"))) {
                        ImGui::BeginDisabled(state.simulationSessionActive);
                        if (ImGui::MenuItem(editorText("ui.new_level", "New Level"), "Ctrl+N")) {
                            if (engine) { selectedEntity = entt::null; engine->setCurrentPreset("empty"); engine->resetScene("empty"); }
                        }
                        if (ImGui::MenuItem(editorText("ui.open_project", "Open Project..."), "Ctrl+O")) {
                            state.loadProject("");
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem(editorText("ui.save_project", "Save Project"), "Ctrl+S")) {
                            state.saveProject(false);
                        }
                        if (ImGui::MenuItem(editorText("ui.save_project_as", "Save Project As..."), "Ctrl+Shift+S")) {
                            state.saveProject(true);
                        }
                        if (ImGui::BeginMenu(editorText("ui.quick_select", "Quick Select"))) {
                            if (ImGui::MenuItem(editorText("ui.load_projects_mygame", "Load 'projects/MyGame'"))) state.loadProject("projects/MyGame");
                            ImGui::EndMenu();
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem(editorText("ui.package_game_exe", "Package Game (.exe)"), "Ctrl+Shift+P")) {
                            state.saveProject(false);
                            if (engine && engine->packageStandaloneGame("Build/Windows/MyLucidGame", "MyLucidGame")) {
                                std::snprintf(state.engineNotification, 128, "Gra skompilowana do Build/Windows/MyLucidGame/!");
                                state.engineNotificationTimer = 5.0f;
                            }
                        }
                        ImGui::EndDisabled();
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu(editorText("ui.edit", "Edit"))) {
                        if (ImGui::MenuItem(editorText("ui.editor_preferences", "Editor Preferences..."), "Ctrl+,")) {
                            state.showPreferencesWindow = true;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem(editorText("ui.world_graphics_settings", "World & Graphics Settings"))) { m_detailsTab = 1; state.showDetails = true; }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu(editorText("ui.window", "Window"))) {
                        ImGui::MenuItem(editorText("ui.place_actors", "Place Actors"), nullptr, &state.showPlaceActors);
                        ImGui::MenuItem(editorText("ui.outliner", "Outliner"), nullptr, &state.showOutliner);
                        ImGui::MenuItem(editorText("ui.details", "Details"), nullptr, &state.showDetails);
                        ImGui::MenuItem(editorText("ui.content_drawer", "Content Drawer"), "Ctrl+Space", &state.showContentDrawer);
                        ImGui::MenuItem(editorText("ui.output_log", "Output Log"), nullptr, &state.showOutputLog);
                        ImGui::MenuItem(editorText("ui.performance_profiler", "Performance Profiler"), nullptr, &m_showPerformanceProfiler);
                        ImGui::MenuItem(editorText("ui.blueprints_scripts", "Blueprints / Scripts"), nullptr, &state.showScriptStudio);
                        ImGui::Separator();
                        ImGui::MenuItem(editorText("ui.editor_preferences", "Editor Preferences..."), "Ctrl+,", &state.showPreferencesWindow);
                        ImGui::Separator();
                        if (ImGui::BeginMenu(editorText("ui.editor_theme", "Editor Theme"))) {
                            static int s_topBarTheme = 0;
                            if (ImGui::MenuItem(editorText("ui.unreal_slate_dark_graphite", "Unreal Slate (Dark Graphite)"), nullptr, s_topBarTheme == 0)) {
                                s_topBarTheme = 0;
                                applyEditorUiTheme(EditorThemePreset::UnrealSlate);
                            }
                            if (ImGui::MenuItem(editorText("ui.blender_dark_pro", "Blender Dark Pro (BUI Palette)"), nullptr, s_topBarTheme == 1)) {
                                s_topBarTheme = 1;
                                applyEditorUiTheme(EditorThemePreset::BlenderDarkPro);
                            }
                            ImGui::EndMenu();
                        }
                        if (ImGui::BeginMenu(editorText("ui.interface_scale", "Interface Scale"))) {
                            auto applyScale = [&](float scale) {
                                m_uiScale = scale;
                                state.uiScale = scale;
                                if (engine) {
                                    engine->setUiScale(scale);
                                    auto& preferences = engine->getPreferences();
                                    preferences.uiScale = scale;
                                    if (preferences.autoSavePreferences) engine->saveUserPreferences();
                                }
                            };
                            if (ImGui::MenuItem(editorText("ui.scale_85", "85% (Compact)"), nullptr, std::abs(m_uiScale - 0.85f) < 0.05f)) applyScale(0.85f);
                            if (ImGui::MenuItem(editorText("ui.scale_100", "100% (Standard)"), nullptr, std::abs(m_uiScale - 1.0f) < 0.05f)) applyScale(1.0f);
                            if (ImGui::MenuItem(editorText("ui.scale_125", "125% (Large)"), nullptr, std::abs(m_uiScale - 1.25f) < 0.05f)) applyScale(1.25f);
                            if (ImGui::MenuItem(editorText("ui.scale_150", "150% (Extra large / 4K)"), nullptr, std::abs(m_uiScale - 1.50f) < 0.05f)) applyScale(1.50f);
                            ImGui::EndMenu();
                        }
                        if (ImGui::BeginMenu(editorText("ui.render_scale", "Render Scale"))) {
                            if (ImGui::MenuItem(editorText("ui.render_scale_50", "50% (Performance)"), nullptr, std::abs(state.resolutionScale - 0.50f) < 0.04f)) setResolutionScale(0.50f);
                            if (ImGui::MenuItem(editorText("ui.render_scale_67", "67% (Quality)"), nullptr, std::abs(state.resolutionScale - 0.67f) < 0.04f)) setResolutionScale(0.67f);
                            if (ImGui::MenuItem(editorText("ui.render_scale_75", "75% (Balanced)"), nullptr, std::abs(state.resolutionScale - 0.75f) < 0.04f)) setResolutionScale(0.75f);
                            if (ImGui::MenuItem(editorText("ui.render_scale_100", "100% (Native / 1:1)"), nullptr, std::abs(state.resolutionScale - 1.00f) < 0.04f)) setResolutionScale(1.00f);
                            if (ImGui::MenuItem(editorText("ui.render_scale_125", "125% (Super-sampling)"), nullptr, std::abs(state.resolutionScale - 1.25f) < 0.04f)) setResolutionScale(1.25f);
                            if (ImGui::MenuItem(editorText("ui.render_scale_150", "150% (Ultra DSR)"), nullptr, std::abs(state.resolutionScale - 1.50f) < 0.04f)) setResolutionScale(1.50f);
                            ImGui::EndMenu();
                        }
                        if (ImGui::MenuItem(editorText("ui.reset_window_layout", "Reset Window Layout"))) {
                            state.userLeftPanelW  = 260.0f * state.uiScale;
                            state.userRightPanelW = 310.0f * state.uiScale;
                            state.userOutlinerH   = 240.0f * state.uiScale;
                            state.userDrawerH     = 220.0f * state.uiScale;
                            state.showPlaceActors = true;
                            state.showOutliner    = true;
                            state.showDetails     = true;
                        }
                        ImGui::Separator();
                        if (ImGui::BeginMenu(editorText("ui.fps", "FPS"))) {
                            int curFps = engine ? engine->getTargetFps() : 0;
                            if (ImGui::MenuItem("30 FPS", nullptr, curFps == 30)) { if (engine) engine->setTargetFps(30); }
                            if (ImGui::MenuItem("60 FPS", nullptr, curFps == 60)) { if (engine) engine->setTargetFps(60); }
                            if (ImGui::MenuItem("120 FPS", nullptr, curFps == 120)) { if (engine) engine->setTargetFps(120); }
                            if (ImGui::MenuItem("240 FPS", nullptr, curFps == 240)) { if (engine) engine->setTargetFps(240); }
                            if (ImGui::MenuItem(editorText("ui.unlimited", "Unlimited"), nullptr, curFps == 0)) { if (engine) engine->setTargetFps(0); }
                            ImGui::Separator();
                            bool vsync = engine ? engine->isVsyncEnabled() : true;
                            if (ImGui::MenuItem(editorText("ui.v_sync", "V-Sync"), nullptr, vsync)) {
                                if (engine) engine->setVsyncEnabled(!vsync);
                            }
                            ImGui::EndMenu();
                        }
                        if (ImGui::BeginMenu(editorText("ui.anti_aliasing_fxaa", "Anti-Aliasing (FXAA 3.11)"))) {
                            if (ImGui::MenuItem(editorText("ui.off", "Off"), nullptr, state.antiAliasingMode == 0)) state.antiAliasingMode = 0;
                            if (ImGui::MenuItem(editorText("ui.fxaa_fast", "FXAA Fast"), nullptr, state.antiAliasingMode == 1)) state.antiAliasingMode = 1;
                            if (ImGui::MenuItem(editorText("ui.fxaa_quality", "FXAA Quality (Subpixel)"), nullptr, state.antiAliasingMode == 2)) state.antiAliasingMode = 2;
                            if (ImGui::MenuItem(editorText("ui.fxaa_ultra", "FXAA Ultra"), nullptr, state.antiAliasingMode == 3)) state.antiAliasingMode = 3;
                            ImGui::EndMenu();
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu(editorText("ui.tools", "Tools"))) {
                        if (ImGui::MenuItem(editorText("ui.execute_quickjs_script", "Execute QuickJS Script"))) state.showScriptStudio = true;
                        if (ImGui::MenuItem(editorText("ui.export_typescript", "Export TypeScript (.d.ts)")) && engine) engine->exportTypeDefinitions();
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu(editorText("ui.help", "Help"))) {
                        if (ImGui::MenuItem(editorText("ui.keyboard_shortcuts", "Keyboard Shortcuts"), "F1")) s_showShortcutsWindow = true;
                        ImGui::EndMenu();
                    }

                    const float projOffset = screenW * 0.52f;
                    if (ImGui::GetCursorPosX() < projOffset) ImGui::SameLine(projOffset);
                    ImGui::TextDisabled("%s", editorText("ui.renderer", "Renderer:"));
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "Vulkan 1.3 Cubemap IBL + Kulla-Conty PBR");
                    ImGui::EndMenuBar();
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar();

                // 2. LEVEL TABS STRIP
                ImGui::SetNextWindowPos(ImVec2(0, topTitleBarH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(screenW, topLevelTabsH), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 2));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.043f, 0.067f, 1.0f));
                ImGui::Begin("LevelTabsStrip", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);

                const std::string curPreset = engine ? engine->getCurrentPreset() : "empty";
                std::string sceneDisplayName = curPreset;
                if (sceneDisplayName == "empty") sceneDisplayName = "CzystyPoziom";
                else if (!sceneDisplayName.empty()) sceneDisplayName[0] = std::toupper(sceneDisplayName[0]);
                std::string tabBtnLabel = "    " + sceneDisplayName + ".scene  (Poziom glowny) ";

                ImVec2 tabStart = ImGui::GetCursorScreenPos();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.10f, 0.14f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.94f, 0.98f, 1.0f));
                ImGui::Button(tabBtnLabel.c_str(), ImVec2(0, topLevelTabsH - 4.0f));
                ImGui::PopStyleColor(2);

                ImVec2 tabEnd = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddLine(tabStart, ImVec2(tabEnd.x, tabStart.y), IM_COL32(0, 130, 240, 255), 2.0f);
                drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(tabStart.x + 11.0f * state.uiScale, (tabStart.y + tabEnd.y) * 0.5f), 5.0f * state.uiScale, VectorIconType::Folder, IM_COL32(245, 158, 11, 255));

                ImGui::SameLine(0.0f, 6.0f * state.uiScale);
                ImGui::BeginDisabled(state.simulationSessionActive);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.65f, 0.75f, 1.0f));
                if (ImGui::SmallButton(editorText("ui.new_level_2", "+ New Level"))) {
                    if (engine) {
                        selectedEntity = entt::null;
                        engine->setCurrentPreset("empty");
                        engine->resetScene("empty");
                    }
                }
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("ui.create_a_new_empty_level", "Create a new empty level"));
                ImGui::EndDisabled();

                ImGui::End();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }

            // 3. MAIN TOOLBAR (Stacja kontroli symulacji, zapis i perspektywa)
            ImGui::SetNextWindowPos(ImVec2(0, topTitleBarH + topLevelTabsH), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(screenW, topToolbarH), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 5));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.055f, 0.082f, 1.0f));
            ImGui::Begin("MainToolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);

            const float btnH = topToolbarH - 10.0f;

            // Zapisz projekt z ikona dysku
            ImVec2 sPos = ImGui::GetCursorScreenPos();
            ImGui::BeginDisabled(state.simulationSessionActive);
            if (ImGui::Button(editorText("ui.save", "     Save "), ImVec2(0, btnH))) state.saveProject(false);
            ImGui::EndDisabled();
            drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(sPos.x + 11.0f * state.uiScale, sPos.y + btnH * 0.5f), 5.5f * state.uiScale, VectorIconType::SaveDisk, IM_COL32(245, 158, 11, 255));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("ui.save_current_project_ctrl_s", "Save current project [Ctrl+S]"));
            ImGui::SameLine();

            // Studio Skryptow
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.72f, 1.0f, 1.0f));
            if (ImGui::Button(editorText("ui.scripts", " </> Scripts "), ImVec2(0, btnH))) state.showScriptStudio = !state.showScriptStudio;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("ui.open_quickjs_esm_live_coding_scripts_f4", "Open QuickJS ESM Live Coding & Scripts (F4)"));
            ImGui::SameLine();

            // Szybki dostep do oswietlenia i renderera
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.32f, 1.0f));
            ImVec2 gPos = ImGui::GetCursorScreenPos();
            if (ImGui::Button(editorText("ui.lighting_graphics", "     Lighting & Graphics "), ImVec2(0, btnH))) { m_detailsTab = 1; state.showDetails = true; }
            drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(gPos.x + 11.0f * state.uiScale, gPos.y + btnH * 0.5f), 5.5f * state.uiScale, VectorIconType::Gear, IM_COL32(251, 191, 36, 255));
            ImGui::PopStyleColor();
            ImGui::SameLine();

            // Centrum sterowania symulacja (Simulation Transport Dock)
            const float transportPos = screenW * 0.44f;
            if (ImGui::GetCursorPosX() < transportPos) ImGui::SameLine(transportPos);
            const bool isPaused = engine ? engine->isPaused() : true;
            const bool simulationActive = engine && engine->isSimulationSessionActive();

            ImVec2 plPos = ImGui::GetCursorScreenPos();
            if (!isPaused) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.55f, 0.25f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                ImGui::BeginDisabled();
                ImGui::Button(editorText("ui.playing", "     PLAYING "), ImVec2(0, btnH));
                ImGui::EndDisabled();
                ImGui::PopStyleColor(2);
            } else {
                if (ImGui::Button(editorText("ui.play", "     Play "), ImVec2(0, btnH)) && engine) {
                    engine->playSimulation();
                    if (engine->getCameraMode() == "editor" && engine->getRegistry()) {
                        auto view = engine->getRegistry()->get().view<ecs::CharacterMovementComponent>();
                        if (!view.empty()) {
                            auto firstEnt = *view.begin();
                            bool isFpp = false;
                            if (engine->getRegistry()->get().all_of<ecs::CameraBoomComponent>(firstEnt)) {
                                isFpp = engine->getRegistry()->get().get<ecs::CameraBoomComponent>(firstEnt).isFirstPerson;
                            }
                            engine->setCameraMode(isFpp ? "firstPerson" : "thirdPerson");
                        }
                    }
                }
            }
            drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(plPos.x + 11.0f * state.uiScale, plPos.y + btnH * 0.5f), 5.0f * state.uiScale, VectorIconType::Play, isPaused ? IM_COL32(34, 197, 94, 255) : IM_COL32(255, 255, 255, 255));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("ui.start_jolt_physics_simulation_and_scripts", "Start Jolt physics simulation and scripts"));
            ImGui::SameLine();

            ImVec2 paPos = ImGui::GetCursorScreenPos();
            ImGui::BeginDisabled(!simulationActive || isPaused);
            if (ImGui::Button(editorText("ui.pause", "     Pause "), ImVec2(0, btnH)) && engine) engine->setPaused(true);
            ImGui::EndDisabled();
            drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(paPos.x + 11.0f * state.uiScale, paPos.y + btnH * 0.5f), 4.8f * state.uiScale, VectorIconType::Pause, IM_COL32(251, 191, 36, 255));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("ui.pause_physics_simulation", "Pause physics simulation"));
            ImGui::SameLine();

            ImVec2 stPos = ImGui::GetCursorScreenPos();
            ImGui::BeginDisabled(!simulationActive);
            if (ImGui::Button(editorText("ui.stop", "     Stop "), ImVec2(0, btnH)) && engine) {
                engine->stopSimulation();
                engine->setCameraMode("editor");
            }
            ImGui::EndDisabled();
            drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(stPos.x + 11.0f * state.uiScale, stPos.y + btnH * 0.5f), 4.8f * state.uiScale, VectorIconType::Stop, IM_COL32(239, 68, 68, 255));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("ui.stop_and_restore_editor_transforms", "Stop and restore editor transforms"));
            ImGui::SameLine();

            const bool canStepSimulation = engine && engine->isSimulationSessionActive() && engine->isPaused();
            ImGui::BeginDisabled(!canStepSimulation);
            if (ImGui::Button(editorText("ui.step", " Step "), ImVec2(0, btnH)) && engine) engine->stepSimulation(1, 1.0f / 60.0f);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(canStepSimulation ? editorText("ui.advance_simulation_by_one_step_1_60s", "Advance simulation by one step (1/60s)") : editorText("ui.step_is_available_only_while_the_simulation_is_paused", "Step is available only while the simulation is paused"));

            // Wybor trybu kamery (Editor Freecam, 3rd Person TPP, 1st Person FPP, Top-Down, Side-Scroller)
            ImGui::SameLine(screenW - 365.0f * state.uiScale);
            if (engine) {
                const char* camModes[] = { editorText("ui.camera_freecam", "Freecam"), editorText("ui.camera_third_person", "3rd Person (TPP)"), editorText("ui.camera_first_person", "1st Person (FPP)"), editorText("ui.camera_top_down", "Top-Down"), editorText("ui.camera_side_scroller", "Side-Scroller") };
                const char* camModeKeys[] = { "editor", "thirdPerson", "firstPerson", "topDown", "sideScroller" };
                std::string curMode = engine->getCameraMode();
                int curModeIdx = 0;
                for (int i = 0; i < 5; ++i) {
                    if (curMode == camModeKeys[i]) { curModeIdx = i; break; }
                }
                ImGui::SetNextItemWidth(165.0f * state.uiScale);
                if (ImGui::Combo("##CamModeCombo", &curModeIdx, camModes, IM_ARRAYSIZE(camModes))) {
                    engine->setCameraMode(camModeKeys[curModeIdx]);
                    if (engine->getRegistry()) {
                        auto boomView = engine->getRegistry()->get().view<ecs::CameraBoomComponent>();
                        for (auto bEnt : boomView) {
                            auto& b = boomView.get<ecs::CameraBoomComponent>(bEnt);
                            if (curModeIdx == 2) {
                                b.isFirstPerson = true;
                                b.targetArmLength = 0.0f;
                                b.cameraLag = false;
                            } else if (curModeIdx == 1) {
                                b.isFirstPerson = false;
                                b.targetArmLength = 8.0f;
                                b.cameraLag = true;
                            }
                        }
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("ui.camera_mode_tooltip", "Camera mode: Editor free camera or player tracking (TPP/FPP/Top-Down)"));
            }

            // Kontrola FOV kamery po prawej stronie paska
            ImGui::SameLine(screenW - 180.0f * state.uiScale);
            if (engine) {
                float curFov = engine->getCameraFov();
                ImGui::SetNextItemWidth(165.0f * state.uiScale);
                if (ImGui::SliderFloat("##CamFov", &curFov, 25.0f, 95.0f, editorText("ui.camera_fov_format", "Camera FOV: %.0f deg"))) {
                    engine->setCameraFov(curFov);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("ui.camera_fov_tooltip", "Editor camera field of view (FOV)"));
            }

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            if (s_showShortcutsWindow) {
                ImGui::SetNextWindowSize(ImVec2(520.0f * state.uiScale, 440.0f * state.uiScale), ImGuiCond_FirstUseEver);
                const std::string shortcutsTitle = std::string(editorText("ui.keyboard_shortcuts", "Keyboard Shortcuts")) + "###KeyboardShortcuts";
                if (ImGui::Begin(shortcutsTitle.c_str(), &s_showShortcutsWindow, ImGuiWindowFlags_NoCollapse)) {
                    auto shortcutRow = [&](const char* keys, const char* description) {
                        ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "%s", keys);
                        ImGui::SameLine(150.0f * state.uiScale);
                        ImGui::TextUnformatted(description);
                    };
                    ImGui::SeparatorText(editorText("ui.shortcuts_project_windows", "Project & Windows"));
                    shortcutRow("Ctrl + S", editorText("ui.shortcut_save", "Save project"));
                    shortcutRow("Ctrl + Shift + S", editorText("ui.shortcut_save_as", "Save project as"));
                    shortcutRow("Ctrl + O", editorText("ui.shortcut_open", "Open project"));
                    shortcutRow("Ctrl + ,", editorText("ui.shortcut_preferences", "Open editor preferences"));
                    shortcutRow("Ctrl + Space", editorText("ui.shortcut_content_drawer", "Toggle Content Drawer"));
                    shortcutRow("F4", editorText("ui.shortcut_script_studio", "Toggle QuickJS Script Studio"));
                    shortcutRow("F11", editorText("ui.shortcut_immersive", "Toggle immersive viewport"));
                    ImGui::SeparatorText(editorText("ui.shortcuts_viewport", "Viewport & Selection"));
                    shortcutRow("W / G", editorText("ui.shortcut_translate", "Move gizmo"));
                    shortcutRow("E / R", editorText("ui.shortcut_rotate", "Rotate gizmo"));
                    shortcutRow("S", editorText("ui.shortcut_scale", "Scale gizmo"));
                    shortcutRow("F", editorText("ui.shortcut_focus", "Focus selected object"));
                    shortcutRow("Delete", editorText("ui.shortcut_delete", "Delete selected object"));
                    shortcutRow("Ctrl + D", editorText("ui.shortcut_duplicate", "Duplicate selected object"));
                    ImGui::Spacing();
                    ImGui::TextDisabled("%s", editorText("ui.shortcuts_note", "Viewport shortcuts are disabled while editing text or during simulation where applicable."));
                }
                ImGui::End();
            }

}

} // namespace engine::renderer
#endif
