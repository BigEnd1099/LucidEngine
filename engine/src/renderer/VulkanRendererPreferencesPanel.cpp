#include "renderer/VulkanRenderer.hpp"
#include "renderer/EditorPreferencesPanel.hpp"
#include "renderer/EditorUiStyle.hpp"
#include "renderer/EditorLocalization.hpp"
#include "core/Engine.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <cstdio>
#include <string>
#ifdef USE_GLFW
#include "imgui.h"
namespace engine::renderer {
void VulkanRenderer::drawEditorPreferencesPanel(core::Engine* engine,
                                                EditorPreferencesUiState& state,
                                                float screenW, float screenH) {
            if (state.showScriptStudio) {
                ImGui::SetNextWindowSize(ImVec2(750 * state.uiScale, 520 * state.uiScale), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSizeConstraints(ImVec2(360 * state.uiScale, 240 * state.uiScale), ImVec2(screenW, screenH));
                if (ImGui::Begin(editorText("script.studio_title", "Blueprints / QuickJS Script Studio"), &state.showScriptStudio)) {
                    static char scriptBuf[16384] = "";
                    static bool s_scriptInit = false;
                    if (engine && (!s_scriptInit || scriptBuf[0] == '\0')) {
                        s_scriptInit = true;
                        const std::string code = engine->getCurrentScript();
                        std::strncpy(scriptBuf, code.c_str(), sizeof(scriptBuf) - 1);
                    }
                    if (ImGui::Button(editorText("script.apply_run", " Apply & Run Script ")) && engine) {
                        engine->loadScript(scriptBuf);
                        engine->setCurrentScript(scriptBuf);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(editorText("script.reload_from_scene", " Reload from Scene ")) && engine) {
                        const std::string code = engine->getCurrentScript();
                        std::strncpy(scriptBuf, code.c_str(), sizeof(scriptBuf) - 1);
                        scriptBuf[sizeof(scriptBuf) - 1] = '\0';
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(editorText("script.clear", " Clear "))) {
                        scriptBuf[0] = '\0';
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(editorText("script.template_rotation", " Template: Rotation "))) {
                        std::string tpl = "// QuickJS Live Script: Rotacja i animacja obiektu\nlet angle = 0;\nexport function update(dt) {\n    angle += dt * 45.0;\n    console.log('[QuickJS] Kat rotacji: ' + angle.toFixed(1));\n}\n";
                        std::strncpy(scriptBuf, tpl.c_str(), sizeof(scriptBuf) - 1);
                        scriptBuf[sizeof(scriptBuf) - 1] = '\0';
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(editorText("script.template_impulse", " Template: Impulse "))) {
                        std::string tpl = "// QuickJS Live Script: Impuls fizyczny Jolt\nexport function update(dt) {\n    console.log('[QuickJS] Impuls fizyczny zaaplikowany');\n}\n";
                        std::strncpy(scriptBuf, tpl.c_str(), sizeof(scriptBuf) - 1);
                        scriptBuf[sizeof(scriptBuf) - 1] = '\0';
                    }
                    ImGui::InputTextMultiline("##ScriptCode", scriptBuf, sizeof(scriptBuf), ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput);
                }
                ImGui::End();
            }

            if (state.showPreferencesWindow && engine) {
                ImGui::SetNextWindowSize(ImVec2(820 * state.uiScale, 560 * state.uiScale), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSizeConstraints(ImVec2(500 * state.uiScale, 380 * state.uiScale), ImVec2(screenW, screenH));
                if (ImGui::Begin(editorText("ui.editor_preferences_editorprefsdialog", "Editor Preferences###EditorPrefsDialog"), &state.showPreferencesWindow, ImGuiWindowFlags_NoCollapse)) {
                    auto& pref = engine->getPreferences();

                    int languageIndex = pref.language == "pl" ? 1 : 0;
                    const char* languageOptions[] = { "English", "Polski" };
                    ImGui::SetNextItemWidth(220.0f * state.uiScale);
                    if (ImGui::Combo(editorText("settings.language", "Language"), &languageIndex, languageOptions, 2)) {
                        pref.language = languageIndex == 1 ? "pl" : "en";
                        setEditorLanguage(pref.language);
                        if (pref.autoSavePreferences) engine->saveUserPreferences();
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled(editorText("ui.changes_are_applied_immediately", "Changes are applied immediately."));
                    ImGui::Separator();

                    // Header
                    ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), editorText("ui.lucid_engine_user_preferences", "  LUCID ENGINE - USER PREFERENCES"));
                    ImGui::TextDisabled(editorText("ui.configure_the_interface_camera_controls_vulkan_graphics_and_editor_beh", "  Configure the interface, camera controls, Vulkan graphics and editor behavior."));
                    ImGui::Separator();

                    static int s_prefCategory = 0; // 0=UI, 1=Camera, 2=Graphics, 3=Audio, 4=Storage
                    const float leftColW = 190.0f * state.uiScale;

                    ImGui::Columns(2, "PrefsColumns", true);
                    ImGui::SetColumnWidth(0, leftColW);

                    // Category List (Left side)
                    const char* categories[] = {
                        editorText("ui.interface_ui", "  Interface (UI)"),
                        editorText("ui.camera_viewport", "  Camera & Viewport"),
                        editorText("ui.graphics_vulkan", "  Graphics & Vulkan"),
                        editorText("ui.audio", "  Audio"),
                        editorText("ui.storage_projects", "  Storage & Projects")
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
                        ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.08f, 1.0f), editorText("ui.interface_layout", "INTERFACE & LAYOUT"));
                        ImGui::Separator();

                        static int s_themeChoice = 0;
                        const std::string themeOptions = std::string(editorText("settings.theme_unreal_dark", "Unreal Slate (Dark Graphite)")) + '\0' + editorText("ui.blender_dark_pro", "Blender Dark Pro (BUI Palette)") + '\0' + '\0';
                        if (ImGui::Combo(editorText("ui.editor_theme", "Editor Theme"), &s_themeChoice, themeOptions.c_str())) {
                            applyEditorUiTheme(s_themeChoice == 1 ? EditorThemePreset::BlenderDarkPro : EditorThemePreset::UnrealSlate);
                        }

                        float curUiScale = pref.uiScale;
                        if (ImGui::SliderFloat(editorText("ui.interface_scale", "Interface Scale"), &curUiScale, 0.70f, 2.0f, "%.2fx")) {
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
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), editorText("ui.default_panel_visibility", "Default panel visibility:"));
                        if (ImGui::Checkbox(editorText("ui.place_actors", "Place Actors"), &pref.showPlaceActors)) state.showPlaceActors = pref.showPlaceActors;
                        if (ImGui::Checkbox(editorText("ui.scene_outliner", "Scene Outliner"), &pref.showOutliner)) state.showOutliner = pref.showOutliner;
                        if (ImGui::Checkbox(editorText("ui.details_inspector", "Details Inspector"), &pref.showDetails)) state.showDetails = pref.showDetails;
                        if (ImGui::Checkbox(editorText("ui.content_browser", "Content Browser"), &pref.showContentDrawer)) state.showContentDrawer = pref.showContentDrawer;
                        if (ImGui::Checkbox(editorText("ui.output_log", "Output Log"), &pref.showOutputLog)) state.showOutputLog = pref.showOutputLog;
                        if (ImGui::Checkbox(editorText("script.studio_visibility", "Blueprints / QuickJS Studio"), &pref.showScriptStudio)) state.showScriptStudio = pref.showScriptStudio;

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), editorText("ui.panel_dimensions", "Panel dimensions:"));
                        if (ImGui::SliderFloat(editorText("ui.left_panel_width", "Left Panel Width"), &state.userLeftPanelW, 140.0f, 600.0f, "%.0f px")) pref.userLeftPanelW = state.userLeftPanelW;
                        if (ImGui::SliderFloat(editorText("ui.right_panel_width", "Right Panel Width"), &state.userRightPanelW, 180.0f, 700.0f, "%.0f px")) pref.userRightPanelW = state.userRightPanelW;
                        if (ImGui::SliderFloat(editorText("ui.outliner_height", "Outliner Height"), &state.userOutlinerH, 100.0f, 600.0f, "%.0f px")) pref.userOutlinerH = state.userOutlinerH;
                        if (ImGui::SliderFloat(editorText("ui.content_drawer_height", "Content Drawer Height"), &state.userDrawerH, 100.0f, 500.0f, "%.0f px")) pref.userDrawerH = state.userDrawerH;

                        ImGui::Spacing();
                        if (ImGui::Button(editorText("ui.reset_panel_layout", " Reset Panel Layout "))) {
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
                        ImGui::Checkbox(editorText("ui.automatically_save_preferences_when_changed", "Automatically save preferences when changed"), &pref.autoSavePreferences);

                    } else if (s_prefCategory == 1) {
                        // Category 1: Camera & Viewport
                        ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.08f, 1.0f), editorText("ui.viewport_controls_navigation", "VIEWPORT CONTROLS & NAVIGATION"));
                        ImGui::Separator();

                        if (ImGui::SliderFloat(editorText("ui.base_camera_speed_fly_speed", "Base Camera Speed (Fly Speed)"), &pref.camFlySpeed, 5.0f, 100.0f, "%.1f m/s")) state.camFlySpeed = pref.camFlySpeed;
                        if (ImGui::SliderInt(editorText("ui.camera_speed_multiplier_1_8", "Camera Speed Multiplier (1..8)"), &pref.cameraSpeed, 1, 8)) state.cameraSpeed = pref.cameraSpeed;
                        ImGui::SliderFloat(editorText("ui.mouse_sensitivity", "Mouse Sensitivity"), &pref.camMouseSensitivity, 0.02f, 1.0f, "%.2f");
                        ImGui::Checkbox(editorText("ui.invert_mouse_y", "Invert Mouse Y"), &pref.invertY);

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Przyciaganie (Snapping) & Transformacja:");
                        if (ImGui::Checkbox(editorText("ui.enable_grid_snapping", "Enable Grid Snapping"), &pref.snapEnabled)) state.snapEnabled = pref.snapEnabled;
                        if (ImGui::InputFloat(editorText("ui.snap_step", "Snap Step"), &pref.snapStep, 0.1f, 1.0f, "%.2f")) state.snapStep = pref.snapStep;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("0.25")) { pref.snapStep = 0.25f; state.snapStep = 0.25f; }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("0.50")) { pref.snapStep = 0.50f; state.snapStep = 0.50f; }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("1.00")) { pref.snapStep = 1.00f; state.snapStep = 1.00f; }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("2.00")) { pref.snapStep = 2.00f; state.snapStep = 2.00f; }

                        if (ImGui::Checkbox(editorText("ui.local_gizmo_space", "Local Gizmo Space"), &pref.gizmoSpaceLocal)) state.gizmoSpaceLocal = pref.gizmoSpaceLocal;

                    } else if (s_prefCategory == 2) {
                        // Category 2: Graphics & Rendering
                        ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.08f, 1.0f), editorText("ui.graphics_vulkan_settings", "GRAPHICS & VULKAN SETTINGS"));
                        ImGui::Separator();

                        const char* fpsLabels[] = { editorText("ui.unlimited", "Unlimited"), "30 FPS", "60 FPS", "120 FPS", "240 FPS" };
                        int fpsValues[] = {0, 30, 60, 120, 240};
                        int curFpsIdx = 0;
                        for (int f = 0; f < 5; ++f) {
                            if (pref.targetFps == fpsValues[f]) { curFpsIdx = f; break; }
                        }
                        if (ImGui::Combo(editorText("ui.fps_limit", "FPS Limit"), &curFpsIdx, fpsLabels, 5)) {
                            pref.targetFps = fpsValues[curFpsIdx];
                            engine->setTargetFps(pref.targetFps);
                        }

                        if (ImGui::Checkbox(editorText("ui.v_sync", "V-Sync"), &pref.vsync)) {
                            engine->setVsyncEnabled(pref.vsync);
                        }

                        const char* aaLabels[] = { editorText("ui.off", "Off"), editorText("ui.fxaa_fast", "FXAA Fast"), editorText("ui.fxaa_quality", "FXAA Quality (Subpixel)"), editorText("ui.fxaa_ultra", "FXAA Ultra") };
                        if (ImGui::Combo(editorText("settings.anti_aliasing", "Anti-Aliasing (AA)"), &pref.antiAliasingMode, aaLabels, 4)) {
                            m_antiAliasingMode = pref.antiAliasingMode;
                            setAntiAliasingMode(pref.antiAliasingMode);
                        }

                        if (ImGui::SliderFloat(editorText("ui.render_resolution_scale", "Render Resolution Scale"), &pref.resolutionScale, 0.25f, 2.50f, "%.2fx")) {
                            setResolutionScale(pref.resolutionScale);
                        }
                        ImGui::Checkbox(editorText("settings.auto_viewport_resolution", "Automatically fit resolution to Viewport"), &pref.autoViewportResolution);

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Efekty Post-Process & Oswietlenie:");

                        if (ImGui::SliderFloat(editorText("settings.shadow_distance", "Shadow Distance"), &pref.shadowDistance, 20.0f, 200.0f, "%.0f m")) {
                            setShadowDistance(pref.shadowDistance);
                        }

                        if (ImGui::Checkbox("Bloom", &pref.bloomEnabled)) setBloomEnabled(pref.bloomEnabled);
                        if (pref.bloomEnabled) {
                            if (ImGui::SliderFloat(editorText("settings.bloom_intensity", "  Bloom Intensity"), &pref.bloomIntensity, 0.0f, 3.0f, "%.2f")) setBloomIntensity(pref.bloomIntensity);
                            if (ImGui::SliderFloat(editorText("settings.bloom_threshold", "  Bloom Threshold"), &pref.bloomThreshold, 0.1f, 5.0f, "%.2f")) setBloomThreshold(pref.bloomThreshold);
                        }

                        if (ImGui::Checkbox("GTAO (Ground-Truth Ambient Occlusion)", &pref.gtaoEnabled)) setGtaoEnabled(pref.gtaoEnabled);
                        if (pref.gtaoEnabled) {
                            if (ImGui::SliderFloat(editorText("settings.gtao_radius", "  GTAO Radius"), &pref.gtaoRadius, 0.1f, 3.0f, "%.2f m")) setGtaoRadius(pref.gtaoRadius);
                            if (ImGui::SliderFloat(editorText("settings.gtao_intensity", "  GTAO Intensity"), &pref.gtaoIntensity, 0.0f, 3.0f, "%.2f")) setGtaoIntensity(pref.gtaoIntensity);
                        }

                        bool rtSupported = m_context && m_context->isRayTracingSupported();
                        if (rtSupported) {
                            if (ImGui::Checkbox("Hardware Ray Tracing (RTX / Vulkan KHR)", &pref.rayTracingEnabled)) {
                                setRayTracingEnabled(pref.rayTracingEnabled);
                            }
                        } else {
                            ImGui::TextDisabled(editorText("settings.ray_tracing_unavailable", "Hardware Ray Tracing: Not available on this GPU"));
                        }

                        if (ImGui::SliderFloat(editorText("settings.exposure", "Exposure"), &pref.exposure, 0.1f, 5.0f, "%.2f")) {
                            setExposure(pref.exposure);
                        }

                    } else if (s_prefCategory == 3) {
                        // Category 3: Audio
                        ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.08f, 1.0f), editorText("ui.audio_system_volume", "AUDIO SYSTEM & VOLUME"));
                        ImGui::Separator();

                        ImGui::SliderFloat(editorText("settings.master_volume", "Master Volume"), &pref.masterVolume, 0.0f, 1.0f, "%.0f%%");
                        ImGui::SliderFloat(editorText("settings.sfx_volume", "Sound Effects (SFX Volume)"), &pref.sfxVolume, 0.0f, 1.0f, "%.0f%%");

                        ImGui::Spacing();
                        ImGui::TextDisabled(editorText("settings.spatial_audio_info", "System: Spatial 3D Audio Engine (Doppler, Distance Attenuation)"));

                    } else if (s_prefCategory == 4) {
                        // Category 4: Storage & Projects
                        ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.08f, 1.0f), editorText("ui.files_projects_storage", "FILES, PROJECTS & STORAGE"));
                        ImGui::Separator();

                        static char projPathBuf[512] = "";
                        static bool s_projBufInit = false;
                        if (!s_projBufInit) {
                            s_projBufInit = true;
                            std::strncpy(projPathBuf, pref.lastProjectPath.c_str(), sizeof(projPathBuf) - 1);
                        }
                        if (ImGui::InputText(editorText("settings.last_project", "Last Project"), projPathBuf, sizeof(projPathBuf))) {
                            pref.lastProjectPath = projPathBuf;
                        }
                        ImGui::Checkbox(editorText("settings.load_last_project", "Automatically load the last project on startup"), &pref.loadLastProjectOnStartup);

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::TextDisabled(editorText("ui.configuration_file", "Configuration file:"));
                        ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "%s", pref.getDefaultConfigPath().c_str());

                        const bool fileExists = std::filesystem::exists(pref.getDefaultConfigPath());
                        ImGui::TextDisabled(editorText("settings.file_status", "File status on disk: %s"), fileExists ? editorText("settings.file_exists", "Exists (Saved)") : editorText("settings.file_missing", "Missing (Will be created)"));
                    }

                    ImGui::EndChild();

                    // Bottom Action Bar
                    ImGui::Separator();
                    if (ImGui::Button(editorText("settings.save_preferences", " 💾 Save Preferences "))) {
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
                        std::snprintf(state.engineNotification, 128,
                                 ok ? "Zapisano preferencje do: %s" : "Blad zapisu preferencji!",
                                 pref.getDefaultConfigPath().c_str());
                        state.engineNotificationTimer = 4.0f;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(editorText("ui.apply", " Apply "))) {
                        engine->applyPreferences();
                        std::snprintf(state.engineNotification, 128, "Zastosowano preferencje");
                        state.engineNotificationTimer = 3.0f;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(editorText("ui.reset_to_defaults", " Reset to Defaults "))) {
                        engine->getPreferences() = core::EditorPreferences{};
                        engine->applyPreferences();
                        state.userLeftPanelW  = 260.0f * state.uiScale;
                        state.userRightPanelW = 310.0f * state.uiScale;
                        state.userOutlinerH   = 240.0f * state.uiScale;
                        state.userDrawerH     = 220.0f * state.uiScale;
                        state.showPlaceActors = true;
                        state.showOutliner    = true;
                        state.showDetails     = true;
                        std::snprintf(state.engineNotification, 128, "Przywrocono ustawienia domyslne");
                        state.engineNotificationTimer = 3.0f;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(editorText("ui.close", " Close "))) {
                        state.showPreferencesWindow = false;
                    }
                    ImGui::Columns(1);
                }
                ImGui::End();
            }


}
} // namespace engine::renderer
#endif
