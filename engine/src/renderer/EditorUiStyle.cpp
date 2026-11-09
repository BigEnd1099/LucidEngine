#include "renderer/EditorUiStyle.hpp"
#include "core/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#ifdef USE_GLFW
namespace engine::renderer {

// Blender-engineered hover highlight from BUI (interface_widgets.cc widget_active_color):
// HSL saturation x1.15, lightness x1.2 for crisp, organic hover response.
ImVec4 buiColorHover(const ImVec4& c) {
    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(c.x, c.y, c.z, h, s, v);
    float l = v * (1.0f - s * 0.5f);
    float sl = (l > 0.0f && l < 1.0f) ? (v - l) / std::min(l, 1.0f - l) : 0.0f;
    l = std::min(l * 1.2f, 1.0f);
    sl = std::min(sl * 1.15f, 1.0f);
    const float nv = l + sl * std::min(l, 1.0f - l);
    const float ns = nv > 0.0f ? 2.0f * (1.0f - l / nv) : 0.0f;
    ImVec4 out(0, 0, 0, c.w);
    ImGui::ColorConvertHSVtoRGB(h, ns, nv, out.x, out.y, out.z);
    return out;
}

void applyEditorUiTheme(EditorThemePreset preset) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    if (preset == EditorThemePreset::BlenderDarkPro) {
        // Blender Dark Theme from BUI (release/datafiles/userdef/userdef_default_theme.c)
        const ImVec4 text         = ImVec4(0.90f, 0.90f, 0.90f, 1.0f); // #e6e6e6
        const ImVec4 textDisabled = ImVec4(0.60f, 0.60f, 0.60f, 1.0f); // #999999
        const ImVec4 region       = ImVec4(0.19f, 0.19f, 0.19f, 1.0f); // #303030
        const ImVec4 inner        = ImVec4(0.33f, 0.33f, 0.33f, 1.0f); // #545454
        const ImVec4 outline      = ImVec4(0.24f, 0.24f, 0.24f, 1.0f); // #3d3d3d
        const ImVec4 select       = ImVec4(0.28f, 0.45f, 0.70f, 1.0f); // #4772b3 Blender Blue
        const ImVec4 dark         = ImVec4(0.11f, 0.11f, 0.11f, 1.0f); // #1d1d1d
        const ImVec4 popup        = ImVec4(0.09f, 0.09f, 0.09f, 0.98f);// #181818

        colors[ImGuiCol_WindowBg]             = region;
        colors[ImGuiCol_ChildBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
        colors[ImGuiCol_PopupBg]              = popup;
        colors[ImGuiCol_Border]               = outline;
        colors[ImGuiCol_BorderShadow]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        colors[ImGuiCol_TitleBg]              = dark;
        colors[ImGuiCol_TitleBgActive]        = outline;
        colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(dark.x, dark.y, dark.z, 0.75f);
        colors[ImGuiCol_MenuBarBg]            = dark;

        colors[ImGuiCol_Text]                 = text;
        colors[ImGuiCol_TextDisabled]         = textDisabled;

        colors[ImGuiCol_FrameBg]              = inner;
        colors[ImGuiCol_FrameBgHovered]       = buiColorHover(inner);
        colors[ImGuiCol_FrameBgActive]        = ImVec4(0.13f, 0.13f, 0.13f, 1.0f); // #222222

        colors[ImGuiCol_Button]               = inner;
        colors[ImGuiCol_ButtonHovered]        = buiColorHover(inner);
        colors[ImGuiCol_ButtonActive]         = select;

        colors[ImGuiCol_Header]               = outline;
        colors[ImGuiCol_HeaderHovered]        = ImVec4(select.x, select.y, select.z, 0.70f);
        colors[ImGuiCol_HeaderActive]         = select;

        colors[ImGuiCol_Tab]                  = dark;
        colors[ImGuiCol_TabHovered]           = outline;
        colors[ImGuiCol_TabActive]            = region;
        colors[ImGuiCol_TabUnfocused]         = dark;
        colors[ImGuiCol_TabUnfocusedActive]   = region;

        colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.13f, 0.13f, 0.13f, 0.40f);
        colors[ImGuiCol_ScrollbarGrab]        = inner;
        colors[ImGuiCol_ScrollbarGrabHovered] = buiColorHover(inner);
        colors[ImGuiCol_ScrollbarGrabActive]  = select;
        colors[ImGuiCol_CheckMark]            = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        colors[ImGuiCol_SliderGrab]           = select;
        colors[ImGuiCol_SliderGrabActive]     = buiColorHover(select);
        colors[ImGuiCol_Separator]            = ImVec4(outline.x, outline.y, outline.z, 0.7f);
        colors[ImGuiCol_SeparatorHovered]     = select;
        colors[ImGuiCol_SeparatorActive]      = select;
    } else {
        // Unreal Slate Theme, tuned with BUI's HSL hover math
        const ImVec4 bgWindow     = ImVec4(0.04f, 0.05f, 0.07f, 1.00f);
        const ImVec4 bgChild      = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
        const ImVec4 bgPopup      = ImVec4(0.06f, 0.07f, 0.09f, 0.98f);
        const ImVec4 border       = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
        const ImVec4 btn          = ImVec4(0.10f, 0.12f, 0.15f, 1.00f);
        const ImVec4 accentBlue   = ImVec4(0.00f, 0.44f, 0.95f, 1.00f);

        colors[ImGuiCol_WindowBg]             = bgWindow;
        colors[ImGuiCol_ChildBg]              = bgChild;
        colors[ImGuiCol_PopupBg]              = bgPopup;
        colors[ImGuiCol_Border]               = border;
        colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        colors[ImGuiCol_TitleBg]              = ImVec4(0.03f, 0.04f, 0.06f, 1.00f);
        colors[ImGuiCol_TitleBgActive]        = ImVec4(0.04f, 0.05f, 0.07f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.03f, 0.04f, 0.06f, 0.75f);
        colors[ImGuiCol_MenuBarBg]            = ImVec4(0.03f, 0.04f, 0.06f, 1.00f);

        colors[ImGuiCol_Text]                 = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);
        colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.55f, 0.60f, 1.00f);

        colors[ImGuiCol_FrameBg]              = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]       = buiColorHover(colors[ImGuiCol_FrameBg]);
        colors[ImGuiCol_FrameBgActive]        = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);

        colors[ImGuiCol_Button]               = btn;
        colors[ImGuiCol_ButtonHovered]        = buiColorHover(btn);
        colors[ImGuiCol_ButtonActive]         = accentBlue;

        colors[ImGuiCol_Header]               = ImVec4(0.00f, 0.40f, 0.90f, 0.50f);
        colors[ImGuiCol_HeaderHovered]        = ImVec4(0.00f, 0.40f, 0.90f, 0.75f);
        colors[ImGuiCol_HeaderActive]         = accentBlue;

        colors[ImGuiCol_Tab]                  = ImVec4(0.04f, 0.05f, 0.07f, 1.00f);
        colors[ImGuiCol_TabHovered]           = buiColorHover(btn);
        colors[ImGuiCol_TabActive]            = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
        colors[ImGuiCol_TabUnfocused]         = ImVec4(0.03f, 0.04f, 0.06f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.05f, 0.06f, 0.08f, 1.00f);

        colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.04f, 0.05f, 0.07f, 0.50f);
        colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = buiColorHover(colors[ImGuiCol_ScrollbarGrab]);
        colors[ImGuiCol_ScrollbarGrabActive]  = accentBlue;
        colors[ImGuiCol_CheckMark]            = ImVec4(0.00f, 0.80f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrab]           = accentBlue;
        colors[ImGuiCol_SliderGrabActive]     = buiColorHover(accentBlue);
        colors[ImGuiCol_Separator]            = border;
        colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.00f, 0.40f, 0.90f, 0.78f);
        colors[ImGuiCol_SeparatorActive]      = accentBlue;
    }

    // Blender & Slate Pro rounding and proportional padding metrics
    style.WindowRounding    = 4.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;

    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(8.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
    style.ScrollbarSize     = 12.0f;
}

void loadEditorUiFont(ImGuiIO& io) {
    const std::vector<std::string> fontCandidates = {
        "engine/assets/fonts/Inter-Variable.ttf",
        "assets/fonts/Inter-Variable.ttf",
        "public/fonts/Inter-Variable.ttf",
        "public/fonts/Roboto-Medium.ttf",
        "engine/assets/fonts/Roboto-Medium.ttf",
        "assets/fonts/Roboto-Medium.ttf",
        "D:/UE_5.8/Engine/Content/Slate/Fonts/Roboto-Medium.ttf",
        "public/fonts/Roboto-Regular.ttf",
        "engine/assets/fonts/Roboto-Regular.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf"
    };

    static const ImWchar glyphRanges[] = {
        0x0020, 0x00FF,
        0x0100, 0x017F,
        0x2000, 0x206F,
        0x2200, 0x22FF,
        0x25A0, 0x25FF,
        0,
    };

    ImFont* loadedFont = nullptr;
    for (const auto& path : fontCandidates) {
        if (std::filesystem::exists(path)) {
            ImFontConfig config;
            config.OversampleH = 2;
            config.OversampleV = 2;
            config.RasterizerMultiply = 1.05f;
            loadedFont = io.Fonts->AddFontFromFileTTF(path.c_str(), 16.0f, &config, glyphRanges);
            if (loadedFont) {
                core::Logger::info("Loaded Slate/BUI UI font: " + path);
                break;
            }
        }
    }
    if (!loadedFont) {
        io.Fonts->AddFontDefault();
    }
}

void drawEditorVectorIcon(ImDrawList* dl, ImVec2 center, float r, VectorIconType type, ImU32 col) {
    switch (type) {
        case VectorIconType::Cube: {
            ImVec2 t0(center.x, center.y - r);
            ImVec2 t1(center.x + r * 0.86f, center.y - r * 0.5f);
            ImVec2 t2(center.x, center.y);
            ImVec2 t3(center.x - r * 0.86f, center.y - r * 0.5f);
            ImVec2 b1(center.x + r * 0.86f, center.y + r * 0.5f);
            ImVec2 b2(center.x, center.y + r);
            ImVec2 b3(center.x - r * 0.86f, center.y + r * 0.5f);
            dl->AddLine(t0, t1, col, 1.4f); dl->AddLine(t1, t2, col, 1.4f);
            dl->AddLine(t2, t3, col, 1.4f); dl->AddLine(t3, t0, col, 1.4f);
            dl->AddLine(t1, b1, col, 1.4f); dl->AddLine(t2, b2, col, 1.4f); dl->AddLine(t3, b3, col, 1.4f);
            dl->AddLine(b3, b2, col, 1.4f); dl->AddLine(b2, b1, col, 1.4f);
            break;
        }
        case VectorIconType::Character: {
            dl->AddCircle(ImVec2(center.x, center.y - r * 0.42f), r * 0.34f, col, 12, 1.5f);
            dl->PathArcTo(ImVec2(center.x, center.y + r * 0.75f), r * 0.65f, 3.45f, 5.97f, 12);
            dl->PathStroke(col, 0, 1.5f);
            break;
        }
        case VectorIconType::Pawn: {
            dl->AddCircle(ImVec2(center.x, center.y - r * 0.48f), r * 0.28f, col, 12, 1.5f);
            dl->AddRect(ImVec2(center.x - r * 0.40f, center.y - r * 0.10f), ImVec2(center.x + r * 0.40f, center.y + r * 0.78f), col, r * 0.35f, 0, 1.5f);
            break;
        }
        case VectorIconType::Light: {
            dl->AddCircle(center, r * 0.40f, col, 12, 1.5f);
            for (int i = 0; i < 8; ++i) {
                float a = i * 0.785398f;
                dl->AddLine(ImVec2(center.x + cosf(a) * r * 0.60f, center.y + sinf(a) * r * 0.60f),
                            ImVec2(center.x + cosf(a) * r * 0.95f, center.y + sinf(a) * r * 0.95f), col, 1.3f);
            }
            break;
        }
        case VectorIconType::PlayerStart: {
            dl->AddLine(ImVec2(center.x - r * 0.55f, center.y - r * 0.85f), ImVec2(center.x - r * 0.55f, center.y + r * 0.85f), col, 1.6f);
            dl->AddTriangleFilled(ImVec2(center.x - r * 0.50f, center.y - r * 0.80f),
                                  ImVec2(center.x + r * 0.75f, center.y - r * 0.30f),
                                  ImVec2(center.x - r * 0.50f, center.y + r * 0.20f), col);
            break;
        }
        case VectorIconType::TriggerBox: {
            dl->AddRect(ImVec2(center.x - r * 0.78f, center.y - r * 0.68f), ImVec2(center.x + r * 0.78f, center.y + r * 0.68f), col, 2.0f, 0, 1.5f);
            dl->AddCircleFilled(center, r * 0.22f, col);
            break;
        }
        case VectorIconType::TriggerSphere: {
            dl->AddCircle(center, r * 0.80f, col, 16, 1.5f);
            dl->AddCircle(center, r * 0.32f, col, 12, 1.2f);
            break;
        }
        case VectorIconType::Shapes: {
            dl->AddTriangle(ImVec2(center.x, center.y - r * 0.85f), ImVec2(center.x + r * 0.75f, center.y + r * 0.45f), ImVec2(center.x - r * 0.75f, center.y + r * 0.45f), col, 1.5f);
            break;
        }
        case VectorIconType::Sparkles: {
            dl->AddLine(ImVec2(center.x, center.y - r * 0.90f), ImVec2(center.x, center.y + r * 0.90f), col, 1.5f);
            dl->AddLine(ImVec2(center.x - r * 0.90f, center.y), ImVec2(center.x + r * 0.90f, center.y), col, 1.5f);
            dl->AddCircleFilled(center, r * 0.25f, col);
            break;
        }
        case VectorIconType::Folder: {
            dl->AddRectFilled(ImVec2(center.x - r * 0.85f, center.y - r * 0.65f), ImVec2(center.x - r * 0.10f, center.y - r * 0.35f), col, 1.5f);
            dl->AddRectFilled(ImVec2(center.x - r * 0.85f, center.y - r * 0.42f), ImVec2(center.x + r * 0.85f, center.y + r * 0.65f), col, 2.0f);
            break;
        }
        case VectorIconType::WarningTri: {
            dl->AddTriangleFilled(ImVec2(center.x, center.y - r * 0.82f), ImVec2(center.x + r * 0.85f, center.y + r * 0.72f), ImVec2(center.x - r * 0.85f, center.y + r * 0.72f), col);
            break;
        }
        case VectorIconType::Eye: {
            dl->AddCircle(center, r * 0.62f, col, 12, 1.3f);
            dl->AddCircleFilled(center, r * 0.25f, col);
            break;
        }
        case VectorIconType::EyeClosed: {
            dl->AddCircle(center, r * 0.62f, col, 12, 1.3f);
            dl->AddLine(ImVec2(center.x - r * 0.80f, center.y + r * 0.80f),
                        ImVec2(center.x + r * 0.80f, center.y - r * 0.80f), col, 1.6f);
            break;
        }
        case VectorIconType::Translate: {
            dl->AddLine(ImVec2(center.x - r * 0.85f, center.y), ImVec2(center.x + r * 0.85f, center.y), col, 1.5f);
            dl->AddLine(ImVec2(center.x, center.y - r * 0.85f), ImVec2(center.x, center.y + r * 0.85f), col, 1.5f);
            break;
        }
        case VectorIconType::Rotate: {
            dl->PathArcTo(center, r * 0.72f, 0.4f, 5.3f, 14);
            dl->PathStroke(col, 0, 1.6f);
            break;
        }
        case VectorIconType::Scale: {
            dl->AddRect(ImVec2(center.x - r * 0.75f, center.y - r * 0.75f), ImVec2(center.x + r * 0.75f, center.y + r * 0.75f), col, 1.5f, 0, 1.4f);
            dl->AddLine(ImVec2(center.x - r * 0.45f, center.y + r * 0.45f), ImVec2(center.x + r * 0.45f, center.y - r * 0.45f), col, 1.5f);
            break;
        }
        case VectorIconType::Speaker: {
            dl->AddRectFilled(ImVec2(center.x - r * 0.75f, center.y - r * 0.32f), ImVec2(center.x - r * 0.30f, center.y + r * 0.32f), col, 1.0f);
            dl->AddTriangleFilled(ImVec2(center.x - r * 0.35f, center.y), ImVec2(center.x + r * 0.25f, center.y - r * 0.68f), ImVec2(center.x + r * 0.25f, center.y + r * 0.68f), col);
            dl->PathArcTo(ImVec2(center.x + r * 0.25f, center.y), r * 0.48f, -0.8f, 0.8f, 8);
            dl->PathStroke(col, 0, 1.4f);
            break;
        }
        case VectorIconType::Wand: {
            dl->AddLine(ImVec2(center.x - r * 0.75f, center.y + r * 0.75f), ImVec2(center.x + r * 0.55f, center.y - r * 0.55f), col, 1.8f);
            dl->AddCircleFilled(ImVec2(center.x + r * 0.65f, center.y - r * 0.65f), r * 0.22f, col);
            break;
        }
        case VectorIconType::Pointer: {
            ImVec2 p0(center.x - r * 0.6f, center.y - r * 0.8f);
            ImVec2 p1(center.x - r * 0.6f, center.y + r * 0.6f);
            ImVec2 p2(center.x - r * 0.1f, center.y + r * 0.2f);
            ImVec2 p3(center.x + r * 0.5f, center.y + r * 0.7f);
            dl->AddTriangleFilled(p0, p1, p2, col);
            dl->AddLine(p2, p3, col, 1.6f);
            break;
        }
        case VectorIconType::Mountain: {
            dl->AddTriangle(ImVec2(center.x - r * 0.8f, center.y + r * 0.6f), ImVec2(center.x - r * 0.2f, center.y - r * 0.5f), ImVec2(center.x + r * 0.4f, center.y + r * 0.6f), col, 1.4f);
            dl->AddTriangle(ImVec2(center.x + r * 0.1f, center.y + r * 0.6f), ImVec2(center.x + r * 0.5f, center.y - r * 0.2f), ImVec2(center.x + r * 0.9f, center.y + r * 0.6f), col, 1.4f);
            break;
        }
        case VectorIconType::Leaf: {
            dl->PathArcTo(ImVec2(center.x - r * 0.2f, center.y), r * 0.6f, -1.57f, 1.57f, 8);
            dl->PathStroke(col, 0, 1.4f);
            dl->PathArcTo(ImVec2(center.x + r * 0.2f, center.y), r * 0.6f, 1.57f, 4.71f, 8);
            dl->PathStroke(col, 0, 1.4f);
            dl->AddLine(ImVec2(center.x, center.y - r * 0.6f), ImVec2(center.x, center.y + r * 0.7f), col, 1.2f);
            break;
        }
        case VectorIconType::Brush: {
            dl->AddLine(ImVec2(center.x + r * 0.6f, center.y - r * 0.6f), ImVec2(center.x - r * 0.1f, center.y + r * 0.1f), col, 2.0f);
            dl->AddTriangleFilled(ImVec2(center.x - r * 0.4f, center.y + r * 0.2f), ImVec2(center.x - r * 0.1f, center.y + r * 0.5f), ImVec2(center.x - r * 0.6f, center.y + r * 0.7f), col);
            break;
        }
        case VectorIconType::Gear: {
            dl->AddCircle(center, r * 0.45f, col, 12, 1.5f);
            for (int i = 0; i < 6; ++i) {
                float a = i * 1.047197f;
                dl->AddLine(ImVec2(center.x + cosf(a) * r * 0.45f, center.y + sinf(a) * r * 0.45f),
                            ImVec2(center.x + cosf(a) * r * 0.85f, center.y + sinf(a) * r * 0.85f), col, 2.0f);
            }
            break;
        }
        case VectorIconType::Play: {
            const float ox = r * 0.12f;
            ImVec2 p0(center.x - r * 0.55f + ox, center.y - r * 0.75f);
            ImVec2 p1(center.x + r * 0.75f + ox, center.y);
            ImVec2 p2(center.x - r * 0.55f + ox, center.y + r * 0.75f);
            dl->AddTriangleFilled(p0, p1, p2, col);
            break;
        }
        case VectorIconType::Pause: {
            const float barW = r * 0.28f;
            const float barH = r * 0.70f;
            const float gap = r * 0.20f;
            dl->AddRectFilled(ImVec2(center.x - gap - barW, center.y - barH),
                              ImVec2(center.x - gap,        center.y + barH), col, 1.0f);
            dl->AddRectFilled(ImVec2(center.x + gap,        center.y - barH),
                              ImVec2(center.x + gap + barW, center.y + barH), col, 1.0f);
            break;
        }
        case VectorIconType::Stop: {
            const float s = r * 0.62f;
            dl->AddRectFilled(ImVec2(center.x - s, center.y - s),
                              ImVec2(center.x + s, center.y + s), col, 1.2f);
            break;
        }
        case VectorIconType::SaveDisk: {
            const float s = r * 0.78f;
            dl->AddRectFilled(ImVec2(center.x - s, center.y - s),
                              ImVec2(center.x + s, center.y + s), col, 1.5f);
            const ImU32 cutCol = IM_COL32(20, 20, 28, 255);
            dl->AddRectFilled(ImVec2(center.x - s * 0.52f, center.y - s),
                              ImVec2(center.x + s * 0.40f, center.y - s * 0.25f), cutCol);
            dl->AddRectFilled(ImVec2(center.x - s * 0.15f, center.y - s * 0.85f),
                              ImVec2(center.x + s * 0.15f, center.y - s * 0.45f), col);
            dl->AddRectFilled(ImVec2(center.x - s * 0.62f, center.y + s * 0.12f),
                              ImVec2(center.x + s * 0.62f, center.y + s * 0.78f), cutCol, 1.0f);
            break;
        }
        default: {
            dl->AddCircle(center, r * 0.5f, col, 12, 1.5f);
            break;
        }
    }
}

} // namespace engine::renderer
#endif
