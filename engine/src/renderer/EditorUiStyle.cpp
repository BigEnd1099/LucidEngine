#include "renderer/EditorUiStyle.hpp"
#include "core/Logger.hpp"

#include <filesystem>
#include <string>
#include <vector>

#ifdef USE_GLFW
namespace engine::renderer {

void applyEditorUiTheme() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        colors[ImGuiCol_WindowBg]             = ImVec4(0.051f, 0.067f, 0.090f, 1.00f);
        colors[ImGuiCol_ChildBg]              = ImVec4(0.086f, 0.106f, 0.133f, 1.00f);
        colors[ImGuiCol_PopupBg]              = ImVec4(0.086f, 0.106f, 0.133f, 0.98f);
        colors[ImGuiCol_Border]               = ImVec4(0.188f, 0.212f, 0.239f, 1.00f);
        colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

        colors[ImGuiCol_TitleBg]              = ImVec4(0.043f, 0.055f, 0.075f, 1.00f);
        colors[ImGuiCol_TitleBgActive]        = ImVec4(0.051f, 0.067f, 0.090f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.043f, 0.055f, 0.075f, 0.75f);
        colors[ImGuiCol_MenuBarBg]            = ImVec4(0.043f, 0.055f, 0.075f, 1.00f);

        colors[ImGuiCol_Text]                 = ImVec4(0.945f, 0.960f, 0.980f, 1.00f);
        colors[ImGuiCol_TextDisabled]         = ImVec4(0.580f, 0.635f, 0.720f, 1.00f);

        colors[ImGuiCol_FrameBg]              = ImVec4(0.086f, 0.106f, 0.133f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.129f, 0.149f, 0.176f, 1.00f);
        colors[ImGuiCol_FrameBgActive]        = ImVec4(0.188f, 0.212f, 0.239f, 1.00f);

        colors[ImGuiCol_Button]               = ImVec4(0.129f, 0.149f, 0.176f, 1.00f);
        colors[ImGuiCol_ButtonHovered]        = ImVec4(0.188f, 0.212f, 0.239f, 1.00f);
        colors[ImGuiCol_ButtonActive]         = ImVec4(0.000f, 0.440f, 0.950f, 1.00f);

        colors[ImGuiCol_Header]               = ImVec4(0.000f, 0.440f, 0.950f, 0.50f);
        colors[ImGuiCol_HeaderHovered]        = ImVec4(0.000f, 0.440f, 0.950f, 0.75f);
        colors[ImGuiCol_HeaderActive]         = ImVec4(0.000f, 0.440f, 0.950f, 1.00f);

        colors[ImGuiCol_Tab]                  = ImVec4(0.051f, 0.067f, 0.090f, 1.00f);
        colors[ImGuiCol_TabHovered]           = ImVec4(0.129f, 0.149f, 0.176f, 1.00f);
        colors[ImGuiCol_TabActive]            = ImVec4(0.086f, 0.106f, 0.133f, 1.00f);
        colors[ImGuiCol_TabUnfocused]         = ImVec4(0.043f, 0.055f, 0.075f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.065f, 0.080f, 0.105f, 1.00f);

        colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.051f, 0.067f, 0.090f, 0.50f);
        colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.188f, 0.212f, 0.239f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.250f, 0.280f, 0.320f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.000f, 0.440f, 0.950f, 1.00f);
        colors[ImGuiCol_CheckMark]            = ImVec4(0.000f, 0.850f, 1.000f, 1.00f);
        colors[ImGuiCol_SliderGrab]           = ImVec4(0.000f, 0.440f, 0.950f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.200f, 0.650f, 1.000f, 1.00f);
        colors[ImGuiCol_Separator]            = ImVec4(0.188f, 0.212f, 0.239f, 1.00f);
        colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.000f, 0.440f, 0.950f, 0.78f);
        colors[ImGuiCol_SeparatorActive]      = ImVec4(0.000f, 0.440f, 0.950f, 1.00f);

        style.WindowRounding    = 0.0f;
        style.ChildRounding     = 6.0f;
        style.FrameRounding     = 5.0f;
        style.PopupRounding     = 6.0f;
        style.ScrollbarRounding = 4.0f;
        style.GrabRounding      = 4.0f;
        style.TabRounding       = 4.0f;

        style.WindowBorderSize  = 1.0f;
        style.ChildBorderSize   = 1.0f;
        style.PopupBorderSize   = 1.0f;
        style.FrameBorderSize   = 1.0f;

        style.WindowPadding     = ImVec2(6.0f, 5.0f);
        style.FramePadding      = ImVec2(6.0f, 3.5f);
        style.ItemSpacing       = ImVec2(5.0f, 4.0f);
        style.ItemInnerSpacing  = ImVec2(4.0f, 3.0f);
        style.ScrollbarSize     = 10.0f;
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
                    core::Logger::info("Loaded Slate UI font: " + path);
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
