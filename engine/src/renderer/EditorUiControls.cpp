#include "renderer/EditorUiControls.hpp"

#include <algorithm>
#include <cmath>

#ifdef USE_GLFW
namespace engine::renderer {
namespace {
float applyCtrlSnap(float value, float snapStep = 0.5f) {
    if (ImGui::GetIO().KeyCtrl) {
        return std::round(value / snapStep) * snapStep;
    }
    return value;
}
} // namespace

bool drawEditorVec3Control(const char* label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 70.0f) {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.92f, 1.0f), "%s", label);
        ImGui::NextColumn();

        const float lineHeight = ImGui::GetFrameHeight();
        const ImVec2 buttonSize = { lineHeight + 2.0f, lineHeight };
        const float availW = ImGui::GetContentRegionAvail().x;
        const float itemWidth = std::max(35.0f, (availW - 3.0f * buttonSize.x - 24.0f) / 3.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.18f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.28f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.12f, 0.14f, 1.0f));
        if (ImGui::Button("X", buttonSize)) { values.x = resetValue; changed = true; }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(itemWidth);
        if (ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f")) {
            values.x = applyCtrlSnap(values.x);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("[Ctrl] Przyciagaj do siatki (0.5m)");
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.75f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.90f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.60f, 0.18f, 1.0f));
        if (ImGui::Button("Y", buttonSize)) { values.y = resetValue; changed = true; }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(itemWidth);
        if (ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f")) {
            values.y = applyCtrlSnap(values.y);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("[Ctrl] Przyciagaj do siatki (0.5m)");
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.35f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.48f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.28f, 0.75f, 1.0f));
        if (ImGui::Button("Z", buttonSize)) { values.z = resetValue; changed = true; }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(itemWidth);
        if (ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f")) {
            values.z = applyCtrlSnap(values.z);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("[Ctrl] Przyciagaj do siatki (0.5m)");

        ImGui::PopStyleVar();
        ImGui::Columns(1);
        ImGui::PopID();
        return changed;
    }

} // namespace engine::renderer
#endif
