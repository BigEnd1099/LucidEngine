#include "renderer/VulkanRenderer.hpp"
#include "renderer/EditorTopBarPanel.hpp"
#include "renderer/EditorUiStyle.hpp"
#include "core/Engine.hpp"
#include <cmath>
#include <cstdio>
#ifdef USE_GLFW
#include "imgui.h"
namespace engine::renderer {
void VulkanRenderer::drawEditorTopBarPanel(core::Engine* engine, EditorTopBarUiState& state,
                                           float screenW, float topTitleBarH,
                                           float topLevelTabsH, float topToolbarH,
                                           entt::entity& selectedEntity) {
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

                    if (ImGui::BeginMenu("File")) {
                        if (ImGui::MenuItem("New Level (Nowy poziom)", "Ctrl+N")) {
                            if (engine) { selectedEntity = entt::null; engine->setCurrentPreset("empty"); engine->resetScene("empty"); }
                        }
                        if (ImGui::MenuItem("Open Project... (Otworz z dysku)", "Ctrl+O")) {
                            state.loadProject("");
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Save Project (Zapisz)", "Ctrl+S")) {
                            state.saveProject(false);
                        }
                        if (ImGui::MenuItem("Save Project As... (Zapisz jako...)", "Ctrl+Shift+S")) {
                            state.saveProject(true);
                        }
                        if (ImGui::BeginMenu("Szybki wybor")) {
                            if (ImGui::MenuItem("Wczytaj 'projects/MyGame'")) state.loadProject("projects/MyGame");
                            ImGui::EndMenu();
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Kompiluj pelna gre (.exe)", "Ctrl+Shift+P")) {
                            state.saveProject(false);
                            if (engine && engine->packageStandaloneGame("Build/Windows/MyLucidGame", "MyLucidGame")) {
                                std::snprintf(state.engineNotification, 128, "Gra skompilowana do Build/Windows/MyLucidGame/!");
                                state.engineNotificationTimer = 5.0f;
                            }
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Edit")) {
                        if (ImGui::MenuItem("Editor Preferences... (Preferencje)", "Ctrl+,")) {
                            state.showPreferencesWindow = true;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("World & Graphics Settings")) { m_detailsTab = 1; state.showDetails = true; }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Window")) {
                        ImGui::MenuItem("Place Actors", nullptr, &state.showPlaceActors);
                        ImGui::MenuItem("Outliner", nullptr, &state.showOutliner);
                        ImGui::MenuItem("Details", nullptr, &state.showDetails);
                        ImGui::MenuItem("Content Drawer", "Ctrl+Space", &state.showContentDrawer);
                        ImGui::MenuItem("Output Log", nullptr, &state.showOutputLog);
                        ImGui::MenuItem("Blueprints / Scripts", nullptr, &state.showScriptStudio);
                        ImGui::Separator();
                        ImGui::MenuItem("Editor Preferences...", "Ctrl+,", &state.showPreferencesWindow);
                        ImGui::Separator();
                        if (ImGui::BeginMenu("Motyw Edytora (Theme)")) {
                            static int s_topBarTheme = 0;
                            if (ImGui::MenuItem("Unreal Slate (Ciemny grafit)", nullptr, s_topBarTheme == 0)) {
                                s_topBarTheme = 0;
                                applyEditorUiTheme(EditorThemePreset::UnrealSlate);
                            }
                            if (ImGui::MenuItem("Blender Dark Pro (BUI Palette)", nullptr, s_topBarTheme == 1)) {
                                s_topBarTheme = 1;
                                applyEditorUiTheme(EditorThemePreset::BlenderDarkPro);
                            }
                            ImGui::EndMenu();
                        }
                        if (ImGui::BeginMenu("Skala Interfejsu (UI Scale)")) {
                            if (ImGui::MenuItem("85% (Kompaktowe)", nullptr, std::abs(m_uiScale - 0.85f) < 0.05f)) m_uiScale = 0.85f;
                            if (ImGui::MenuItem("100% (Standard)", nullptr, std::abs(m_uiScale - 1.0f) < 0.05f)) m_uiScale = 1.0f;
                            if (ImGui::MenuItem("125% (Duze)", nullptr, std::abs(m_uiScale - 1.25f) < 0.05f)) m_uiScale = 1.25f;
                            if (ImGui::MenuItem("150% (Bardzo duze / 4K)", nullptr, std::abs(m_uiScale - 1.50f) < 0.05f)) m_uiScale = 1.50f;
                            ImGui::EndMenu();
                        }
                        if (ImGui::BeginMenu("Skala Renderowania (Resolution Scale)")) {
                            if (ImGui::MenuItem("50% (Wydajnosc / Performance)", nullptr, std::abs(state.resolutionScale - 0.50f) < 0.04f)) setResolutionScale(0.50f);
                            if (ImGui::MenuItem("67% (Jakosc / Quality)", nullptr, std::abs(state.resolutionScale - 0.67f) < 0.04f)) setResolutionScale(0.67f);
                            if (ImGui::MenuItem("75% (Zrownowazona / Balanced)", nullptr, std::abs(state.resolutionScale - 0.75f) < 0.04f)) setResolutionScale(0.75f);
                            if (ImGui::MenuItem("100% (Natywna / 1:1)", nullptr, std::abs(state.resolutionScale - 1.00f) < 0.04f)) setResolutionScale(1.00f);
                            if (ImGui::MenuItem("125% (Super-Sampling)", nullptr, std::abs(state.resolutionScale - 1.25f) < 0.04f)) setResolutionScale(1.25f);
                            if (ImGui::MenuItem("150% (Ultra DSR)", nullptr, std::abs(state.resolutionScale - 1.50f) < 0.04f)) setResolutionScale(1.50f);
                            ImGui::EndMenu();
                        }
                        if (ImGui::MenuItem("Zresetuj uklad okien do domyslnego")) {
                            state.userLeftPanelW  = 260.0f * state.uiScale;
                            state.userRightPanelW = 310.0f * state.uiScale;
                            state.userOutlinerH   = 240.0f * state.uiScale;
                            state.userDrawerH     = 220.0f * state.uiScale;
                            state.showPlaceActors = true;
                            state.showOutliner    = true;
                            state.showDetails     = true;
                        }
                        ImGui::Separator();
                        if (ImGui::BeginMenu("FPS")) {
                            int curFps = engine ? engine->getTargetFps() : 0;
                            if (ImGui::MenuItem("30 FPS", nullptr, curFps == 30)) { if (engine) engine->setTargetFps(30); }
                            if (ImGui::MenuItem("60 FPS", nullptr, curFps == 60)) { if (engine) engine->setTargetFps(60); }
                            if (ImGui::MenuItem("120 FPS", nullptr, curFps == 120)) { if (engine) engine->setTargetFps(120); }
                            if (ImGui::MenuItem("240 FPS", nullptr, curFps == 240)) { if (engine) engine->setTargetFps(240); }
                            if (ImGui::MenuItem("Unlimited (Brak limitu)", nullptr, curFps == 0)) { if (engine) engine->setTargetFps(0); }
                            ImGui::Separator();
                            bool vsync = engine ? engine->isVsyncEnabled() : true;
                            if (ImGui::MenuItem("V-Sync", nullptr, vsync)) {
                                if (engine) engine->setVsyncEnabled(!vsync);
                            }
                            ImGui::EndMenu();
                        }
                        if (ImGui::BeginMenu("Anti-Aliasing (FXAA 3.11)")) {
                            if (ImGui::MenuItem("Off (Brak AA)", nullptr, state.antiAliasingMode == 0)) state.antiAliasingMode = 0;
                            if (ImGui::MenuItem("FXAA Fast", nullptr, state.antiAliasingMode == 1)) state.antiAliasingMode = 1;
                            if (ImGui::MenuItem("FXAA Quality (Subpixel)", nullptr, state.antiAliasingMode == 2)) state.antiAliasingMode = 2;
                            if (ImGui::MenuItem("FXAA Ultra", nullptr, state.antiAliasingMode == 3)) state.antiAliasingMode = 3;
                            ImGui::EndMenu();
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Tools")) {
                        if (ImGui::MenuItem("Execute QuickJS Script")) state.showScriptStudio = true;
                        if (ImGui::MenuItem("Export TypeScript (.d.ts)") && engine) engine->exportTypeDefinitions();
                        ImGui::EndMenu();
                    }

                    const float projOffset = screenW * 0.52f;
                    if (ImGui::GetCursorPosX() < projOffset) ImGui::SameLine(projOffset);
                    ImGui::TextDisabled("Renderer:");
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
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.65f, 0.75f, 1.0f));
                if (ImGui::SmallButton("+ Nowy Poziom")) {
                    if (engine) {
                        selectedEntity = entt::null;
                        engine->setCurrentPreset("empty");
                        engine->resetScene("empty");
                    }
                }
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zresetuj do nowego, czystego poziomu");

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
            if (ImGui::Button("     Zapisz ", ImVec2(0, btnH))) state.saveProject(false);
            drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(sPos.x + 11.0f * state.uiScale, sPos.y + btnH * 0.5f), 5.5f * state.uiScale, VectorIconType::SaveDisk, IM_COL32(245, 158, 11, 255));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zapisz biezacy stan projektu [Ctrl+S]");
            ImGui::SameLine();

            // Studio Skryptow
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.72f, 1.0f, 1.0f));
            if (ImGui::Button(" </> Skrypty ", ImVec2(0, btnH))) state.showScriptStudio = !state.showScriptStudio;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Otworz QuickJS ESM Live Coding & Skrypty (F4)");
            ImGui::SameLine();

            // Szybki dostep do oswietlenia i renderera
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.32f, 1.0f));
            ImVec2 gPos = ImGui::GetCursorScreenPos();
            if (ImGui::Button("     Swiatlo & Grafika ", ImVec2(0, btnH))) { m_detailsTab = 1; state.showDetails = true; }
            drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(gPos.x + 11.0f * state.uiScale, gPos.y + btnH * 0.5f), 5.5f * state.uiScale, VectorIconType::Gear, IM_COL32(251, 191, 36, 255));
            ImGui::PopStyleColor();
            ImGui::SameLine();

            // Centrum sterowania symulacja (Simulation Transport Dock)
            const float transportPos = screenW * 0.44f;
            if (ImGui::GetCursorPosX() < transportPos) ImGui::SameLine(transportPos);
            const bool isPaused = engine ? engine->isPaused() : true;

            ImVec2 plPos = ImGui::GetCursorScreenPos();
            if (!isPaused) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.55f, 0.25f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                if (ImGui::Button("     PLAYING ", ImVec2(0, btnH)) && engine) engine->playSimulation();
                ImGui::PopStyleColor(2);
            } else {
                if (ImGui::Button("     Play ", ImVec2(0, btnH)) && engine) engine->playSimulation();
            }
            drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(plPos.x + 11.0f * state.uiScale, plPos.y + btnH * 0.5f), 5.0f * state.uiScale, VectorIconType::Play, isPaused ? IM_COL32(34, 197, 94, 255) : IM_COL32(255, 255, 255, 255));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Uruchom symulacje fizyki Jolt i skrypty");
            ImGui::SameLine();

            ImVec2 paPos = ImGui::GetCursorScreenPos();
            if (ImGui::Button("     Pauza ", ImVec2(0, btnH)) && engine) engine->setPaused(true);
            drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(paPos.x + 11.0f * state.uiScale, paPos.y + btnH * 0.5f), 4.8f * state.uiScale, VectorIconType::Pause, IM_COL32(251, 191, 36, 255));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wstrzymaj symulacje fizyki");
            ImGui::SameLine();

            ImVec2 stPos = ImGui::GetCursorScreenPos();
            if (ImGui::Button("     Stop ", ImVec2(0, btnH)) && engine) engine->stopSimulation();
            drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(stPos.x + 11.0f * state.uiScale, stPos.y + btnH * 0.5f), 4.8f * state.uiScale, VectorIconType::Stop, IM_COL32(239, 68, 68, 255));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zatrzymaj i przywroc pozycje edycyjne obiektow");
            ImGui::SameLine();

            if (ImGui::Button(" Krok ", ImVec2(0, btnH)) && engine) engine->stepSimulation(1, 1.0f / 60.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wykonaj krok symulacji o 1 klatke fizyki (1/60s)");

            // Kontrola FOV kamery po prawej stronie paska
            ImGui::SameLine(screenW - 180.0f * state.uiScale);
            if (engine) {
                float curFov = engine->getCameraFov();
                ImGui::SetNextItemWidth(165.0f * state.uiScale);
                if (ImGui::SliderFloat("##CamFov", &curFov, 25.0f, 95.0f, "Kamera FOV: %.0f deg")) {
                    engine->setCameraFov(curFov);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Kat widzenia kamery edytora (FOV)");
            }

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

}
}
#endif
