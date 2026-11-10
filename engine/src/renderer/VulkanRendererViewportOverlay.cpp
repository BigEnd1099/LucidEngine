#include "renderer/VulkanRenderer.hpp"
#include "renderer/EditorViewportOverlay.hpp"
#include "renderer/EditorUiStyle.hpp"
#include "renderer/EditorLocalization.hpp"
#ifdef USE_GLFW
#include "imgui.h"
namespace engine::renderer {
void VulkanRenderer::drawEditorViewportOverlay(EditorViewportOverlayState& state,
                                               const glm::vec2& viewportPanelSize) {
            if (!state.immersivePlay) {
                // Pigulka narzedzi Gizmo (lewy gorny rog Viewportu)
                ImGui::SetCursorPos(ImVec2(10.0f * state.uiScale, 10.0f * state.uiScale));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.05f, 0.07f, 0.92f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
                const float leftOverlayW = (state.showPlaceActors ? 230.0f : 310.0f) * state.uiScale;
                ImGui::BeginChild("VpOverlayTopLeft", ImVec2(leftOverlayW, 30.0f * state.uiScale), true, ImGuiWindowFlags_NoScrollbar);
                if (!state.showPlaceActors) {
                    if (ImGui::Button(editorText("ui.expand_actors", "> Actors"), ImVec2(0, 22.0f * state.uiScale))) state.showPlaceActors = true;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("ui.expand_place_actors_panel", "Expand Place Actors panel"));
                    ImGui::SameLine(0.0f, 4.0f * state.uiScale);
                }
                if (state.gizmoEnabled) {
                auto gizmoToolBtn = [&](const char* id, EditorGizmoMode mode, VectorIconType icon) {
                    const bool active = (state.gizmoMode == mode);
                    ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.0f, 0.44f, 0.95f, 1.0f) : ImVec4(0.08f, 0.10f, 0.13f, 1.0f));
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    const float sz = 22.0f * state.uiScale;
                    if (ImGui::Button(id, ImVec2(sz, sz))) state.gizmoMode = mode;
                    ImGui::PopStyleColor();
                    drawEditorVectorIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + sz * 0.5f, p.y + sz * 0.5f), 5.2f * state.uiScale, icon, IM_COL32(255, 255, 255, 245));
                    ImGui::SameLine(0.0f, 3.0f * state.uiScale);
                };
                gizmoToolBtn("##GizT", EditorGizmoMode::TRANSLATE, VectorIconType::Translate);
                gizmoToolBtn("##GizR", EditorGizmoMode::ROTATE,    VectorIconType::Rotate);
                gizmoToolBtn("##GizS", EditorGizmoMode::SCALE,     VectorIconType::Scale);
                if (ImGui::Button(state.snapEnabled ? editorText("ui.snap_half_meter", " Snap: 0.5m ") : editorText("ui.snap_off", " Snap: Off "), ImVec2(0, 22.0f * state.uiScale))) state.snapEnabled = !state.snapEnabled;
                ImGui::SameLine();
                } else {
                    ImGui::TextDisabled(editorText("ui.gizmo_locked_until_stop", "Gizmo locked until Stop"));
                    ImGui::SameLine(0.0f, 6.0f * state.uiScale);
                }
                if (ImGui::Button((" Cam " + std::to_string(state.cameraSpeed) + " ").c_str(), ImVec2(0, 22.0f * state.uiScale))) state.cameraSpeed = (state.cameraSpeed % 8) + 1;
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();

                // Pigulka statusu (prawy gorny rog Viewportu - wyswietlana tylko jesli miesci sie obok lewej pigulki)
                const float trPillW = ((state.showOutliner || state.showDetails) ? 230.0f : 310.0f) * state.uiScale;
                const float minNeededW = leftOverlayW + trPillW + 24.0f * state.uiScale;
                if (viewportPanelSize.x >= minNeededW) {
                    ImGui::SetCursorPos(ImVec2(viewportPanelSize.x - trPillW - 10.0f * state.uiScale, 10.0f * state.uiScale));
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.05f, 0.07f, 0.92f));
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
                    ImGui::BeginChild("VpOverlayTopRight", ImVec2(trPillW, 30.0f * state.uiScale), true, ImGuiWindowFlags_NoScrollbar);
                    if (!state.showOutliner && !state.showDetails) {
                        if (ImGui::Button(editorText("ui.expand_inspector", "Inspector <"), ImVec2(0, 22.0f * state.uiScale))) { state.showOutliner = true; state.showDetails = true; }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("ui.expand_right_inspector", "Expand the right inspector panel"));
                        ImGui::SameLine(0.0f, 4.0f * state.uiScale);
                    }
                    ImGui::TextColored(ImVec4(0.22f, 0.85f, 1.0f, 1.0f), editorText("ui.cubemap_ibl", "Cubemap IBL"));
                    ImGui::SameLine();
                    ImGui::TextDisabled("|");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.20f, 0.92f, 0.58f, 1.0f), "%ux%u (EV %.1f)", m_width, m_height, m_exposure);
                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                }
            }

}
}
#endif
