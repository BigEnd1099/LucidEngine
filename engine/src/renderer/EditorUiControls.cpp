#include "renderer/EditorUiControls.hpp"
#include "renderer/EditorLocalization.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef USE_GLFW
namespace engine::renderer {

static ImGuiID s_activeTextEditId = 0;

bool drawEditorVec3Control(const char* label, glm::vec3& values, float resetValue, float columnWidth) {
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

    const bool isShift = ImGui::GetIO().KeyShift;
    const bool isAlt   = ImGui::GetIO().KeyAlt;
    const bool isCtrl  = ImGui::GetIO().KeyCtrl;
    const float dragSpeed = isShift ? 0.01f : 0.1f;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));

    auto editChannel = [&](const char* tag, float& val, const ImVec4& btnCol, const ImVec4& btnHov, const ImVec4& btnAct, int channelIdx) {
        ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnHov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, btnAct);
        if (ImGui::Button(tag, buttonSize)) {
            val = resetValue;
            if (isAlt) { values.x = values.y = values.z = resetValue; }
            changed = true;
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(editorText("ui.vector_reset_tooltip", "Click to reset to %.1f\n[Alt] Reset all axes"), resetValue);
        ImGui::SameLine();

        ImGui::SetNextItemWidth(itemWidth);
        float oldVal = val;
        char dragLabel[16];
        std::snprintf(dragLabel, sizeof(dragLabel), "##%s", tag);
        if (ImGui::DragFloat(dragLabel, &val, dragSpeed, 0.0f, 0.0f, "%.2f")) {
            if (isCtrl) {
                const float snapStep = 0.5f;
                val = std::round(val / snapStep) * snapStep;
            }
            float delta = val - oldVal;
            if (isAlt) {
                if (channelIdx != 0) values.x += delta;
                if (channelIdx != 1) values.y += delta;
                if (channelIdx != 2) values.z += delta;
            }
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                val = resetValue;
                if (isAlt) { values.x = values.y = values.z = resetValue; }
                changed = true;
            } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                const ImVec2 m = ImGui::GetMousePos();
                const ImVec2 rmin = ImGui::GetItemRectMin();
                const ImVec2 rmax = ImGui::GetItemRectMax();
                const float w = rmax.x - rmin.x;
                if (m.x < rmin.x + w * 0.22f || m.x > rmin.x + w * 0.78f) {
                    val = resetValue;
                    if (isAlt) { values.x = values.y = values.z = resetValue; }
                    changed = true;
                }
            }
            ImGui::SetTooltip(editorText("ui.vector_drag_tooltip", "[Shift] Precision\n[Ctrl] Snap (0.5m)\n[Alt] Adjust all 3 axes\n[PPM/Dwuklik] Reset do %.1f"), resetValue);
        }
    };

    editChannel("X", values.x, ImVec4(0.85f, 0.18f, 0.20f, 1.0f), ImVec4(1.0f, 0.28f, 0.30f, 1.0f), ImVec4(0.7f, 0.12f, 0.14f, 1.0f), 0);
    ImGui::SameLine();
    editChannel("Y", values.y, ImVec4(0.18f, 0.75f, 0.25f, 1.0f), ImVec4(0.28f, 0.90f, 0.35f, 1.0f), ImVec4(0.12f, 0.60f, 0.18f, 1.0f), 1);
    ImGui::SameLine();
    editChannel("Z", values.z, ImVec4(0.18f, 0.35f, 0.90f, 1.0f), ImVec4(0.28f, 0.48f, 1.0f, 1.0f), ImVec4(0.12f, 0.28f, 0.75f, 1.0f), 2);

    ImGui::PopStyleVar();
    ImGui::Columns(1);
    ImGui::PopID();
    return changed;
}

bool drawEditorSliderFloat(const char* label, float* v, float v_min, float v_max, float v_default, const char* format) {
    if (!v) return false;
    bool changed = false;
    ImGuiID id = ImGui::GetID(label);

    if (s_activeTextEditId == id) {
        char inputLabel[128];
        std::snprintf(inputLabel, sizeof(inputLabel), "##Input_%s", label);
        const float labelW = ImGui::CalcTextSize(label).x;
        const float availW = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(std::max(60.0f, availW - labelW - 16.0f));
        ImGui::SetKeyboardFocusHere(0);
        if (ImGui::InputFloat(inputLabel, v, 0.0f, 0.0f, format, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            *v = std::clamp(*v, v_min, v_max);
            changed = true;
            s_activeTextEditId = 0;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(label);

        if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            *v = std::clamp(*v, v_min, v_max);
            s_activeTextEditId = 0;
        }
    } else {
        if (ImGui::SliderFloat(label, v, v_min, v_max, format)) {
            changed = true;
        }

        if (ImGui::IsItemHovered()) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                *v = v_default;
                changed = true;
            } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                const ImVec2 m = ImGui::GetMousePos();
                const ImVec2 rmin = ImGui::GetItemRectMin();
                const ImVec2 rmax = ImGui::GetItemRectMax();
                const float w = rmax.x - rmin.x;
                // Center 50% is the numeric text; sides are the slider track
                if (m.x >= rmin.x + w * 0.25f && m.x <= rmin.x + w * 0.75f) {
                    s_activeTextEditId = id;
                } else {
                    *v = v_default;
                    changed = true;
                }
            }
            ImGui::SetTooltip("[PPM / Dwuklik po bokach] Reset do domyslnej (%.2f)\n[Dwuklik na liczbie / Ctrl+Klik] Wpisz wartosc recznie", v_default);
        }
    }
    return changed;
}

bool drawEditorDragFloat(const char* label, float* v, float v_speed, float v_min, float v_max, float v_default, const char* format) {
    if (!v) return false;
    bool changed = false;
    ImGuiID id = ImGui::GetID(label);

    if (s_activeTextEditId == id) {
        char inputLabel[128];
        std::snprintf(inputLabel, sizeof(inputLabel), "##Input_%s", label);
        const float labelW = ImGui::CalcTextSize(label).x;
        const float availW = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(std::max(60.0f, availW - labelW - 16.0f));
        ImGui::SetKeyboardFocusHere(0);
        if (ImGui::InputFloat(inputLabel, v, 0.0f, 0.0f, format, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            *v = std::clamp(*v, v_min, v_max);
            changed = true;
            s_activeTextEditId = 0;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(label);

        if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            *v = std::clamp(*v, v_min, v_max);
            s_activeTextEditId = 0;
        }
    } else {
        if (ImGui::DragFloat(label, v, v_speed, v_min, v_max, format)) {
            changed = true;
        }

        if (ImGui::IsItemHovered()) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                *v = v_default;
                changed = true;
            } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                const ImVec2 m = ImGui::GetMousePos();
                const ImVec2 rmin = ImGui::GetItemRectMin();
                const ImVec2 rmax = ImGui::GetItemRectMax();
                const float w = rmax.x - rmin.x;
                if (m.x >= rmin.x + w * 0.25f && m.x <= rmin.x + w * 0.75f) {
                    s_activeTextEditId = id;
                } else {
                    *v = v_default;
                    changed = true;
                }
            }
            ImGui::SetTooltip("[PPM / Dwuklik po bokach] Reset do domyslnej (%.2f)\n[Dwuklik na liczbie / Ctrl+Klik] Wpisz wartosc recznie", v_default);
        }
    }
    return changed;
}

} // namespace engine::renderer
#endif
