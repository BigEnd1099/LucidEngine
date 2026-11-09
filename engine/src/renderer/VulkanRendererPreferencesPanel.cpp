#include "renderer/VulkanRenderer.hpp"
#include "renderer/EditorPreferencesPanel.hpp"
#include "core/Engine.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <cstdio>
#ifdef USE_GLFW
#include "imgui.h"
namespace engine::renderer {
void VulkanRenderer::drawEditorPreferencesPanel(core::Engine* engine,
                                                EditorPreferencesUiState& state,
                                                float screenW, float screenH) {
            if (state.showScriptStudio) {
                ImGui::SetNextWindowSize(ImVec2(750 * state.uiScale, 520 * state.uiScale), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSizeConstraints(ImVec2(360 * state.uiScale, 240 * state.uiScale), ImVec2(screenW, screenH));
                if (ImGui::Begin("Blueprints / QuickJS Script Studio", &state.showScriptStudio)) {
                    static char scriptBuf[16384] = "";
                    static bool s_scriptInit = false;
                    if (engine && (!s_scriptInit || scriptBuf[0] == '\0')) {
                        s_scriptInit = true;
                        const std::string code = engine->getCurrentScript();
                        std::strncpy(scriptBuf, code.c_str(), sizeof(scriptBuf) - 1);
                    }
                    if (ImGui::Button(" Zastosuj i Uruchom Skrypt ") && engine) {
                        engine->loadScript(scriptBuf);
                        engine->setCurrentScript(scriptBuf);
                    }
                    ImGui::InputTextMultiline("##ScriptCode", scriptBuf, sizeof(scriptBuf), ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput);
                }
                ImGui::End();
            }

            if (state.showPreferencesWindow && engine) {
                ImGui::SetNextWindowSize(ImVec2(820 * state.uiScale, 560 * state.uiScale), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSizeConstraints(ImVec2(500 * state.uiScale, 380 * state.uiScale), ImVec2(screenW, screenH));
                if (ImGui::Begin("Editor Preferences (Preferencje Edytora)###EditorPrefsDialog", &state.showPreferencesWindow, ImGuiWindowFlags_NoCollapse)) {
                    auto& pref = engine->getPreferences();

                    // Header
                    ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "  LUCID ENGINE - PREFERENCJE I KONFIGURACJA UZYTKOWNIKA");
                    ImGui::TextDisabled("  Zarzadzaj ukladem UI, sterowaniem kamera, grafika Vulkana i zachowaniem edytora.");
                    ImGui::Separator();

                    static int s_prefCategory = 0; // 0=UI, 1=Camera, 2=Graphics, 3=Audio, 4=Storage
                    const float leftColW = 190.0f * state.uiScale;

                    ImGui::Columns(2, "PrefsColumns", true);
                    ImGui::SetColumnWidth(0, leftColW);

                    // Category List (Left side)
                    const char* categories[] = {
                        "  Interfejs (UI)",
                        "  Kamera i Viewport",
                        "  Grafika i Vulkan",
                        "  Dzwiek (Audio)",
                        "  Zapis i Projekty"
                    };

                    for (int i = 0; i < 5; ++i) {
                        bool selected = (s_prefCategory == i);
                        if (ImGui::Selectable(categories[i], selected, 0, ImVec2(0, 28.0f * state.uiScale))) {
                            s_prefCategory = i;
                        }
                    }

                    ImGui::NextColumn();

                    // Details Panel (Right side)
                    ImGui::BeginChild("PrefDetailsChild", ImVec2(0, -42.0f * state.uiScale), true);

                    if (s_prefCategory == 0) {
                        // Category 0: UI & Layout
                        ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.08f, 1.0f), "UKLAD I SKALA INTERFEJSU");
                        ImGui::Separator();

                        float curUiScale = pref.uiScale;
                        if (ImGui::SliderFloat("Skala Interfejsu (UI Scale)", &curUiScale, 0.70f, 2.0f, "%.2fx")) {
                            pref.uiScale = curUiScale;
                            m_uiScale = curUiScale;
                            engine->setUiScale(curUiScale);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("100%")) { pref.uiScale = 1.0f; m_uiScale = 1.0f; engine->setUiScale(1.0f); }
                        ImGui::SameLine();
                        if (ImGui::Button("125%")) { pref.uiScale = 1.25f; m_uiScale = 1.25f; engine->setUiScale(1.25f); }
                        ImGui::SameLine();
                        if (ImGui::Button("150%")) { pref.uiScale = 1.50f; m_uiScale = 1.50f; engine->setUiScale(1.50f); }

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Widocznosc domyslna paneli:");
                        if (ImGui::Checkbox("Place Actors (Aktorzy)", &pref.showPlaceActors)) state.showPlaceActors = pref.showPlaceActors;
                        if (ImGui::Checkbox("Outliner (Drzewo sceny)", &pref.showOutliner)) state.showOutliner = pref.showOutliner;
                        if (ImGui::Checkbox("Details (Wlasciwosci / Inspektor)", &pref.showDetails)) state.showDetails = pref.showDetails;
                        if (ImGui::Checkbox("Content Drawer (Przegladarka assetow)", &pref.showContentDrawer)) state.showContentDrawer = pref.showContentDrawer;
                        if (ImGui::Checkbox("Output Log (Konsola logow)", &pref.showOutputLog)) state.showOutputLog = pref.showOutputLog;
                        if (ImGui::Checkbox("Blueprints / QuickJS Studio", &pref.showScriptStudio)) state.showScriptStudio = pref.showScriptStudio;

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Wymiary paneli:");
                        if (ImGui::SliderFloat("Szerokosc panelu lewego", &state.userLeftPanelW, 140.0f, 600.0f, "%.0f px")) pref.userLeftPanelW = state.userLeftPanelW;
                        if (ImGui::SliderFloat("Szerokosc panelu prawego", &state.userRightPanelW, 180.0f, 700.0f, "%.0f px")) pref.userRightPanelW = state.userRightPanelW;
                        if (ImGui::SliderFloat("Wysokosc Outlinera", &state.userOutlinerH, 100.0f, 600.0f, "%.0f px")) pref.userOutlinerH = state.userOutlinerH;
                        if (ImGui::SliderFloat("Wysokosc dolnej szuflady", &state.userDrawerH, 100.0f, 500.0f, "%.0f px")) pref.userDrawerH = state.userDrawerH;

                        ImGui::Spacing();
                        if (ImGui::Button(" Zresetuj uklad paneli do domyslnego ")) {
                            state.userLeftPanelW  = 260.0f * state.uiScale;
                            state.userRightPanelW = 310.0f * state.uiScale;
                            state.userOutlinerH   = 240.0f * state.uiScale;
                            state.userDrawerH     = 220.0f * state.uiScale;
                            state.showPlaceActors = true;
                            state.showOutliner    = true;
                            state.showDetails     = true;
                            pref.userLeftPanelW  = state.userLeftPanelW;
                            pref.userRightPanelW = state.userRightPanelW;
                            pref.userOutlinerH   = state.userOutlinerH;
                            pref.userDrawerH     = state.userDrawerH;
                            pref.showPlaceActors = true;
                            pref.showOutliner    = true;
                            pref.showDetails     = true;
                        }

                        ImGui::Spacing();
                        ImGui::Checkbox("Automatycznie zapisuj preferencje przy zmianach", &pref.autoSavePreferences);

                    } else if (s_prefCategory == 1) {
                        // Category 1: Camera & Viewport
                        ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.08f, 1.0f), "STEROWANIE I NAWIGACJA W VIEWPORCIE");
                        ImGui::Separator();

                        if (ImGui::SliderFloat("Bazowa predkosc kamery (Fly Speed)", &pref.camFlySpeed, 5.0f, 100.0f, "%.1f m/s")) state.camFlySpeed = pref.camFlySpeed;
                        if (ImGui::SliderInt("Mnoznik biegu kamery (1..8)", &pref.cameraSpeed, 1, 8)) state.cameraSpeed = pref.cameraSpeed;
                        ImGui::SliderFloat("Czulosz myszy (Mouse Sensitivity)", &pref.camMouseSensitivity, 0.02f, 1.0f, "%.2f");
                        ImGui::Checkbox("Odwroc os pionowa (Invert Mouse Y)", &pref.invertY);

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Przyciaganie (Snapping) & Transformacja:");
                        if (ImGui::Checkbox("Wlacz przyciaganie do siatki (Grid Snap)", &pref.snapEnabled)) state.snapEnabled = pref.snapEnabled;
                        if (ImGui::InputFloat("Krok przyciagania (Snap Step)", &pref.snapStep, 0.1f, 1.0f, "%.2f")) state.snapStep = pref.snapStep;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("0.25")) { pref.snapStep = 0.25f; state.snapStep = 0.25f; }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("0.50")) { pref.snapStep = 0.50f; state.snapStep = 0.50f; }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("1.00")) { pref.snapStep = 1.00f; state.snapStep = 1.00f; }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("2.00")) { pref.snapStep = 2.00f; state.snapStep = 2.00f; }

                        if (ImGui::Checkbox("Uklad lokalny gizmo (Local Space)", &pref.gizmoSpaceLocal)) state.gizmoSpaceLocal = pref.gizmoSpaceLocal;

                    } else if (s_prefCategory == 2) {
                        // Category 2: Graphics & Rendering
                        ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.08f, 1.0f), "USTAWIENIA GRAFIKI I VULKANA");
                        ImGui::Separator();

                        const char* fpsLabels[] = {"Bez limitu (Unlimited)", "30 FPS", "60 FPS", "120 FPS", "240 FPS"};
                        int fpsValues[] = {0, 30, 60, 120, 240};
                        int curFpsIdx = 0;
                        for (int f = 0; f < 5; ++f) {
                            if (pref.targetFps == fpsValues[f]) { curFpsIdx = f; break; }
                        }
                        if (ImGui::Combo("Limit FPS", &curFpsIdx, fpsLabels, 5)) {
                            pref.targetFps = fpsValues[curFpsIdx];
                            engine->setTargetFps(pref.targetFps);
                        }

                        if (ImGui::Checkbox("V-Sync (Synchronizacja pionowa)", &pref.vsync)) {
                            engine->setVsyncEnabled(pref.vsync);
                        }

                        const char* aaLabels[] = {"Wylaczony (Off)", "FXAA Fast (Szybki)", "FXAA Quality (Wysoka jakosc)", "FXAA Ultra (Maksymalny)"};
                        if (ImGui::Combo("Antyaliasing (AA)", &pref.antiAliasingMode, aaLabels, 4)) {
                            m_antiAliasingMode = pref.antiAliasingMode;
                            setAntiAliasingMode(pref.antiAliasingMode);
                        }

                        if (ImGui::SliderFloat("Skala rozdzielczosci (Resolution Scale)", &pref.resolutionScale, 0.25f, 2.50f, "%.2fx")) {
                            setResolutionScale(pref.resolutionScale);
                        }
                        ImGui::Checkbox("Automatyczne dopasowanie rozdzielczosci do Viewportu", &pref.autoViewportResolution);

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Efekty Post-Process & Oswietlenie:");

                        if (ImGui::SliderFloat("Zasieg cieni (Shadow Distance)", &pref.shadowDistance, 20.0f, 200.0f, "%.0f m")) {
                            setShadowDistance(pref.shadowDistance);
                        }

                        if (ImGui::Checkbox("Bloom", &pref.bloomEnabled)) setBloomEnabled(pref.bloomEnabled);
                        if (pref.bloomEnabled) {
                            if (ImGui::SliderFloat("  Intensywnosc Bloom", &pref.bloomIntensity, 0.0f, 3.0f, "%.2f")) setBloomIntensity(pref.bloomIntensity);
                            if (ImGui::SliderFloat("  Prog Bloom (Threshold)", &pref.bloomThreshold, 0.1f, 5.0f, "%.2f")) setBloomThreshold(pref.bloomThreshold);
                        }

                        if (ImGui::Checkbox("GTAO (Ground-Truth Ambient Occlusion)", &pref.gtaoEnabled)) setGtaoEnabled(pref.gtaoEnabled);
                        if (pref.gtaoEnabled) {
                            if (ImGui::SliderFloat("  Promien GTAO (Radius)", &pref.gtaoRadius, 0.1f, 3.0f, "%.2f m")) setGtaoRadius(pref.gtaoRadius);
                            if (ImGui::SliderFloat("  Intensywnosc GTAO", &pref.gtaoIntensity, 0.0f, 3.0f, "%.2f")) setGtaoIntensity(pref.gtaoIntensity);
                        }

                        bool rtSupported = m_context && m_context->isRayTracingSupported();
                        if (rtSupported) {
                            if (ImGui::Checkbox("Hardware Ray Tracing (RTX / Vulkan KHR)", &pref.rayTracingEnabled)) {
                                setRayTracingEnabled(pref.rayTracingEnabled);
                            }
                        } else {
                            ImGui::TextDisabled("Hardware Ray Tracing: Niedostepne na tym GPU");
                        }

                        if (ImGui::SliderFloat("Ekspozycja (Exposure)", &pref.exposure, 0.1f, 5.0f, "%.2f")) {
                            setExposure(pref.exposure);
                        }

                    } else if (s_prefCategory == 3) {
                        // Category 3: Audio
                        ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.08f, 1.0f), "GLOSNOSC I SYSTEM AUDIO");
                        ImGui::Separator();

                        ImGui::SliderFloat("Glosnosc glowna (Master Volume)", &pref.masterVolume, 0.0f, 1.0f, "%.0f%%");
                        ImGui::SliderFloat("Efekty dzwiekowe (SFX Volume)", &pref.sfxVolume, 0.0f, 1.0f, "%.0f%%");

                        ImGui::Spacing();
                        ImGui::TextDisabled("System: Spatial 3D Audio Engine (Doppler, Distance Attenuation)");

                    } else if (s_prefCategory == 4) {
                        // Category 4: Storage & Projects
                        ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.08f, 1.0f), "PLIKI, PROJEKTY I ZAPIS");
                        ImGui::Separator();

                        static char projPathBuf[512] = "";
                        static bool s_projBufInit = false;
                        if (!s_projBufInit) {
                            s_projBufInit = true;
                            std::strncpy(projPathBuf, pref.lastProjectPath.c_str(), sizeof(projPathBuf) - 1);
                        }
                        if (ImGui::InputText("Ostatni projekt", projPathBuf, sizeof(projPathBuf))) {
                            pref.lastProjectPath = projPathBuf;
                        }
                        ImGui::Checkbox("Automatycznie wczytuj ostatni projekt przy starcie", &pref.loadLastProjectOnStartup);

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::TextDisabled("Sciezka pliku konfiguracyjnego:");
                        ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "%s", pref.getDefaultConfigPath().c_str());

                        const bool fileExists = std::filesystem::exists(pref.getDefaultConfigPath());
                        ImGui::TextDisabled("Status pliku na dysku: %s", fileExists ? "Istnieje (Zapisany)" : "Brak (Zostanie utworzony)");
                    }

                    ImGui::EndChild();

                    // Bottom Action Bar
                    ImGui::Separator();
                    if (ImGui::Button(" 💾 Zapisz preferencje (Save) ")) {
                        // Sync current UI state into pref
                        pref.uiScale = m_uiScale;
                        pref.showPlaceActors = state.showPlaceActors;
                        pref.showOutliner = state.showOutliner;
                        pref.showDetails = state.showDetails;
                        pref.showContentDrawer = state.showContentDrawer;
                        pref.showOutputLog = state.showOutputLog;
                        pref.showScriptStudio = state.showScriptStudio;
                        pref.userLeftPanelW = state.userLeftPanelW;
                        pref.userRightPanelW = state.userRightPanelW;
                        pref.userOutlinerH = state.userOutlinerH;
                        pref.userDrawerH = state.userDrawerH;
                        pref.camFlySpeed = state.camFlySpeed;
                        pref.cameraSpeed = state.cameraSpeed;
                        pref.snapEnabled = state.snapEnabled;
                        pref.snapStep = state.snapStep;
                        pref.gizmoSpaceLocal = state.gizmoSpaceLocal;
                        pref.lastProjectPath = state.currentProjectPath;

                        bool ok = engine->saveUserPreferences();
                        snprintf(state.engineNotification, sizeof(state.engineNotification),
                                 ok ? "Zapisano preferencje do: %s" : "Blad zapisu preferencji!",
                                 pref.getDefaultConfigPath().c_str());
                        state.engineNotificationTimer = 4.0f;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" ⚡ Zastosuj (Apply) ")) {
                        engine->applyPreferences();
                        snprintf(state.engineNotification, sizeof(state.engineNotification), "Zastosowano preferencje");
                        state.engineNotificationTimer = 3.0f;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" 🔄 Przywroc domyslne (Reset) ")) {
                        engine->getPreferences() = core::EditorPreferences{};
                        engine->applyPreferences();
                        state.userLeftPanelW  = 260.0f * state.uiScale;
                        state.userRightPanelW = 310.0f * state.uiScale;
                        state.userOutlinerH   = 240.0f * state.uiScale;
                        state.userDrawerH     = 220.0f * state.uiScale;
                        state.showPlaceActors = true;
                        state.showOutliner    = true;
                        state.showDetails     = true;
                        snprintf(state.engineNotification, sizeof(state.engineNotification), "Przywrocono ustawienia domyslne");
                        state.engineNotificationTimer = 3.0f;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(" Zamknij (Close) ")) {
                        state.showPreferencesWindow = false;
                    }
                    ImGui::Columns(1);
                }
                ImGui::End();
            }


}
} // namespace engine::renderer
#endif
