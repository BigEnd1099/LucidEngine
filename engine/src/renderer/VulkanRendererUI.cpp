#include "renderer/VulkanRenderer.hpp"
#include "core/Logger.hpp"
#include "core/AssetManager.hpp"
#include "core/Engine.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <stdexcept>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#ifdef USE_GLFW
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

namespace engine::renderer {

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")

static std::string openProjectNativeDialog() {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Lucid / Unreal Project (*.uproject;game.json)\0*.uproject;game.json;project.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) {
        std::filesystem::path p(filename);
        if (p.has_filename() && (p.extension() == ".json" || p.extension() == ".uproject")) {
            return p.parent_path().string();
        }
        return p.string();
    }
    return "";
}

static std::string saveProjectNativeDialog() {
    char filename[MAX_PATH] = "MyProject.uproject";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Unreal Project Descriptor (*.uproject)\0*.uproject\0Game Package (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameA(&ofn)) {
        std::filesystem::path p(filename);
        if (p.has_filename() && (p.extension() == ".uproject" || p.extension() == ".json")) {
            return p.parent_path().string();
        }
        return p.string();
    }
    return "";
}
#else
static std::string openProjectNativeDialog() { return ""; }
static std::string saveProjectNativeDialog() { return ""; }
#endif


    extern uint32_t g_pendingRenderWidth;
    extern uint32_t g_pendingRenderHeight;
    extern int      g_pendingHdriIndex;

    static void applyUnrealWebTheme() {
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

    static void loadUnrealFont(ImGuiIO& io) {
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

    enum class VectorIconType {
        Cube, Character, Pawn, Light, SkyLight, PlayerStart, TriggerBox, TriggerSphere,
        Star, Clock, Shapes, Sparkles, Folder, WarningTri, Eye, Translate, Rotate, Scale,
        Globe, Camera, Speaker, Wand, Pointer, Mountain, Leaf, Brush, Gear,
        Play, Pause, Stop, SaveDisk
    };

    static void drawVectorIcon(ImDrawList* dl, ImVec2 center, float r, VectorIconType type, ImU32 col) {
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

    void VulkanRenderer::initImGui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        loadUnrealFont(io);
        applyUnrealWebTheme();

        ImGui_ImplGlfw_InitForVulkan(m_context->getWindow(), true);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(m_context->getWindow(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }

        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = m_context->getInstance();
        init_info.PhysicalDevice = m_context->getPhysicalDevice();
        init_info.Device = m_context->getDevice();
        init_info.QueueFamily = m_context->getGraphicsQueueFamily();
        init_info.Queue = m_context->getGraphicsQueue();
        init_info.DescriptorPool = m_context->getImGuiPool();
        init_info.MinImageCount = 2;
        init_info.ImageCount = 2;
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.UseDynamicRendering = true;

        static VkFormat colorFormat;
        colorFormat = m_context->getSwapchainImageFormat();
        init_info.PipelineRenderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;

        ImGui_ImplVulkan_Init(&init_info);

        vkResetCommandBuffer(m_commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
        ImGui_ImplVulkan_CreateFontsTexture();
        vkEndCommandBuffer(m_commandBuffer);

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffer;
        vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_context->getGraphicsQueue());
        ImGui_ImplVulkan_DestroyFontsTexture();

        if (!m_viewportSampler) {
            VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.maxLod = 1.0f;
            vkCreateSampler(m_context->getDevice(), &samplerInfo, nullptr, &m_viewportSampler);
        }

        if (!m_viewportDescriptor && m_viewportSampler && m_colorImageView) {
            m_viewportDescriptor = ImGui_ImplVulkan_AddTexture(
                m_viewportSampler, m_colorImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    enum class NativeGizmoMode { TRANSLATE = 0, ROTATE = 1, SCALE = 2 };

    static float distToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
        const float abx = b.x - a.x;
        const float aby = b.y - a.y;
        const float lenSq = abx * abx + aby * aby;
        if (lenSq < 1e-4f) return hypotf(p.x - a.x, p.y - a.y);
        const float t = std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / lenSq, 0.0f, 1.0f);
        return hypotf(p.x - (a.x + t * abx), p.y - (a.y + t * aby));
    }

    static float applyCtrlSnap(float val, float snapStep = 0.5f) {
        if (ImGui::GetIO().KeyCtrl) {
            return std::round(val / snapStep) * snapStep;
        }
        return val;
    }

    static bool drawVec3Control(const char* label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 70.0f) {
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

    void VulkanRenderer::renderImGui(VkCommandBuffer cmd, VkImageView targetView, ecs::Registry& registry, core::AssetManager* assetManager, core::Engine* engine) {
        (void)assetManager;
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* mainVp = ImGui::GetMainViewport();
        const float screenW = mainVp->Size.x;
        const float screenH = mainVp->Size.y;

        float dpiScale = 1.0f;
        if (m_context && m_context->getWindow()) {
            float xsc = 1.0f, ysc = 1.0f;
            glfwGetWindowContentScale(m_context->getWindow(), &xsc, &ysc);
            dpiScale = std::max(1.0f, std::max(xsc, ysc));
        }

        float s_uiScale = m_uiScale * (dpiScale > 1.0f ? (1.0f + (dpiScale - 1.0f) * 0.35f) : 1.0f);
        s_uiScale = std::clamp(s_uiScale, 0.65f, 2.00f);
        ImGui::GetIO().FontGlobalScale = s_uiScale;

        auto requestResolution = [&](uint32_t w, uint32_t h) {
            g_pendingRenderWidth  = std::clamp(w, 64u, 7680u);
            g_pendingRenderHeight = std::clamp(h, 64u, 4320u);
        };

        if (m_standaloneGameMode) {
            ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(320, 100), ImGuiCond_Always);
            ImGuiWindowFlags hudFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings;
            ImGui::Begin("GameHUD", nullptr, hudFlags);
            const int hp = engine ? engine->getHealth() : 100;
            const int score = engine ? engine->getScore() : 0;
            const std::string st = engine ? engine->getGameStatus() : "PLAYING";

            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.06f, 0.73f, 0.51f, 1.0f));
            ImGui::ProgressBar(hp / 100.0f, ImVec2(300, 20), ("HP: " + std::to_string(hp) + "%").c_str());
            ImGui::PopStyleColor();
            ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.08f, 1.0f), "SCORE: %d", score);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "  |  %s", st.c_str());
            ImGui::End();
        } else {
            static bool s_immersivePlay = false;
            static bool s_showPlaceActors = true;
            static bool s_showOutliner = true;
            static bool s_showDetails = true;
            static bool s_showContentDrawer = false;
            static bool s_showOutputLog = false;
            static bool s_showScriptStudio = false;

            if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
                s_immersivePlay = !s_immersivePlay;
            }

            static float s_userLeftPanelW = 260.0f;
            static float s_userRightPanelW = 310.0f;
            static float s_userOutlinerH = 240.0f;
            static float s_userDrawerH = 220.0f;
            static bool s_splittersInit = false;
            static ImGuiID s_activeSplitterId = 0;
            static bool s_anySplitterHovered = false;
            static bool s_wasSplitterHoveredLastFrame = false;

            if (!s_splittersInit) {
                s_splittersInit = true;
                s_userLeftPanelW  = 260.0f * s_uiScale;
                s_userRightPanelW = 310.0f * s_uiScale;
                s_userOutlinerH   = 240.0f * s_uiScale;
                s_userDrawerH     = 220.0f * s_uiScale;
            }

            const float topTitleBarH  = s_immersivePlay ? 0.0f : (28.0f * s_uiScale);
            const float topLevelTabsH = s_immersivePlay ? 0.0f : (26.0f * s_uiScale);
            const float topToolbarH   = 36.0f * s_uiScale;
            const float topTotalH     = topTitleBarH + topLevelTabsH + topToolbarH;

            const float minPanelW = 130.0f * s_uiScale;
            const float maxPanelW = std::max(minPanelW, (screenW - 200.0f * s_uiScale) * 0.5f);
            s_userLeftPanelW  = std::clamp(s_userLeftPanelW, minPanelW, maxPanelW);
            s_userRightPanelW = std::clamp(s_userRightPanelW, minPanelW, maxPanelW);

            const float bottomStatusH = s_immersivePlay ? 0.0f : (24.0f * s_uiScale);
            const float maxDrawerH    = std::max(80.0f * s_uiScale, screenH * 0.70f);
            s_userDrawerH             = std::clamp(s_userDrawerH, 80.0f * s_uiScale, maxDrawerH);

            const float drawerH       = (!s_immersivePlay && (s_showContentDrawer || s_showOutputLog)) ? s_userDrawerH : 0.0f;
            const float bottomTotalH  = bottomStatusH + drawerH;

            const float contentH        = std::max(120.0f, screenH - topTotalH - bottomTotalH);
            const float leftPanelW      = (!s_immersivePlay && s_showPlaceActors) ? s_userLeftPanelW : 0.0f;
            const float rightPanelW     = (!s_immersivePlay && (s_showOutliner || s_showDetails)) ? s_userRightPanelW : 0.0f;
            const float centerViewportW = std::max(100.0f, screenW - leftPanelW - rightPanelW);

            const float minOutlinerH = 60.0f * s_uiScale;
            const float maxOutlinerH = std::max(minOutlinerH, contentH - 60.0f * s_uiScale);
            s_userOutlinerH = std::clamp(s_userOutlinerH, minOutlinerH, maxOutlinerH);
            const float outlinerH = (s_showOutliner && s_showDetails) ? s_userOutlinerH : (s_showOutliner ? contentH : 0.0f);
            const float detailsH  = (s_showOutliner && s_showDetails) ? (contentH - outlinerH) : (s_showDetails ? contentH : 0.0f);

            s_anySplitterHovered = false;

            auto handleSplitter = [&](const char* id, ImVec2 pos, ImVec2 size, bool vertical, float* targetSize, float deltaSign, float minVal, float maxVal, float defaultVal) {
                ImGuiIO& io = ImGui::GetIO();
                ImDrawList* fgDl = ImGui::GetForegroundDrawList();

                const float grabMargin = 4.0f;
                const float x0 = vertical ? (pos.x - grabMargin) : pos.x;
                const float x1 = vertical ? (pos.x + size.x + grabMargin) : (pos.x + size.x);
                const float y0 = vertical ? pos.y : (pos.y - grabMargin);
                const float y1 = vertical ? (pos.y + size.y) : (pos.y + size.y + grabMargin);
                const bool hovered = (io.MousePos.x >= x0 && io.MousePos.x <= x1 && io.MousePos.y >= y0 && io.MousePos.y <= y1);

                ImGuiID myId = ImGui::GetID(id);
                const bool isDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);

                if (hovered && s_activeSplitterId == 0 && isDown) {
                    s_activeSplitterId = myId;
                }
                if (s_activeSplitterId == myId && !isDown) {
                    s_activeSplitterId = 0;
                }

                const bool active = (s_activeSplitterId == myId);
                if (hovered) s_anySplitterHovered = true;

                if (hovered || active) {
                    ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
                }

                if (active) {
                    const float delta = vertical ? io.MouseDelta.x : io.MouseDelta.y;
                    if (delta != 0.0f) {
                        *targetSize = std::clamp(*targetSize + delta * deltaSign, minVal, maxVal);
                    }
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        *targetSize = defaultVal;
                        s_activeSplitterId = 0;
                    }
                }

                ImU32 col;
                if (active) {
                    col = IM_COL32(0, 140, 255, 255);
                } else if (hovered) {
                    col = IM_COL32(56, 160, 250, 220);
                } else {
                    col = IM_COL32(28, 34, 46, 255);
                }

                fgDl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), col);

                if (hovered || active) {
                    if (vertical) {
                        const float midY = pos.y + size.y * 0.5f;
                        fgDl->AddRectFilled(ImVec2(pos.x - 1.5f, midY - 14.0f), ImVec2(pos.x + size.x + 1.5f, midY + 14.0f), col, 2.0f);
                    } else {
                        const float midX = pos.x + size.x * 0.5f;
                        fgDl->AddRectFilled(ImVec2(midX - 14.0f, pos.y - 1.5f), ImVec2(midX + 14.0f, pos.y + size.y + 1.5f), col, 2.0f);
                    }
                }
            };

            static entt::entity selectedEnt = entt::null;
            static NativeGizmoMode s_gizmoMode = NativeGizmoMode::TRANSLATE;
            static char s_engineNotification[128] = "";
            static float s_engineNotificationTimer = 0.0f;

            float camYaw   = engine ? engine->getCameraYaw() : -90.0f;
            float camPitch = engine ? engine->getCameraPitch() : -20.0f;
            float camFov   = engine ? engine->getCameraFov() : 45.0f;
            glm::vec3 camPos = engine ? engine->getCameraPosition() : glm::vec3(0.0f, 15.0f, 30.0f);

            glm::vec3 front = glm::normalize(glm::vec3(
                cosf(glm::radians(camYaw)) * cosf(glm::radians(camPitch)),
                sinf(glm::radians(camPitch)),
                sinf(glm::radians(camYaw)) * cosf(glm::radians(camPitch))
            ));
            glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
            glm::vec3 up    = glm::normalize(glm::cross(right, front));

            static std::string s_currentProjectPath = "projects/MyGame";

            auto saveCurrentGame = [&](bool askSaveAs = false) {
                if (!engine) return;
                std::string targetPath = s_currentProjectPath;
                if (askSaveAs || targetPath.empty()) {
                    std::string picked = saveProjectNativeDialog();
                    if (picked.empty()) return; // Anulowano wybor w oknie Windows
                    targetPath = picked;
                    s_currentProjectPath = targetPath;
                }
                const bool ok = engine->saveCurrentProject(targetPath);
                snprintf(s_engineNotification, sizeof(s_engineNotification),
                         ok ? "Zapisano projekt w: %s" : "Blad zapisu projektu w: %s", targetPath.c_str());
                s_engineNotificationTimer = 4.0f;
            };

            auto loadCurrentGame = [&](const std::string& p = "") {
                if (!engine) return;
                std::string targetPath = p;
                if (targetPath.empty()) {
                    std::string picked = openProjectNativeDialog();
                    if (picked.empty()) return; // Anulowano wybor w oknie Windows
                    targetPath = picked;
                }
                const bool ok = engine->loadCurrentProject(targetPath);
                if (ok) {
                    selectedEnt = entt::null;
                    s_currentProjectPath = targetPath;
                }
                snprintf(s_engineNotification, sizeof(s_engineNotification),
                         ok ? "Wczytano projekt: %s" : "Nie udalo sie wczytac projektu: %s", targetPath.c_str());
                s_engineNotificationTimer = 4.0f;
            };

            static int s_draggingGizmoAxis = 0;
            static glm::vec3 s_dragInitialEntityPos(0.0f);
            static glm::vec3 s_dragPlaneNormal(0.0f, 1.0f, 0.0f);
            static glm::vec3 s_dragAxisVec(1.0f, 0.0f, 0.0f);
            static glm::vec3 s_dragInitialHitPoint(0.0f);
            static float s_dragInitialOffset = 0.0f;
            static bool s_dragHasHit = false;
            static glm::vec3 s_dragInitialRotation(0.0f);
            static float s_lastRotAngle = 0.0f;
            static float s_accumulatedRotAngle = 0.0f;
            static ImVec2 s_dragPivotScreen(0.0f, 0.0f);
            static glm::vec3 s_dragInitialScale(1.0f);
            static float s_dragInitialDist = 1.0f;

            static bool s_isRmbNavigating = false;
            static bool s_isMmbPanning = false;
            static bool s_isAltOrbiting = false;
            static float s_camFlySpeed = 25.0f;
            static bool s_gizmoSpaceLocal = false;
            static bool s_snapEnabled = false;
            static float s_snapStep = 0.5f;
            static int s_cameraSpeed = 4;
            static int s_placeActorCategory = 2;
            static int s_detailsTab = 0;

            if (selectedEnt != entt::null && !registry.get().valid(selectedEnt)) {
                selectedEnt = entt::null;
                s_draggingGizmoAxis = 0;
                if (engine) engine->setSelectedEntity(entt::null);
            }
            if (engine && engine->getSelectedEntity() != entt::null && selectedEnt == entt::null) {
                selectedEnt = engine->getSelectedEntity();
            }

            // 1. TOP WINDOW BAR
            if (!s_immersivePlay) {
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
                    const float badgeR = 8.0f * s_uiScale;
                    ImVec2 badgeCenter(badgePos.x + badgeR + 2.0f, badgePos.y + topTitleBarH * 0.42f);
                    tdl->AddCircleFilled(badgeCenter, badgeR, IM_COL32(0, 140, 235, 255), 16);
                    tdl->AddText(ImVec2(badgeCenter.x - 3.5f * s_uiScale, badgeCenter.y - 6.5f * s_uiScale), IM_COL32(255, 255, 255, 255), "L");
                    ImGui::Dummy(ImVec2(badgeR * 2.0f + 4.0f, badgeR * 2.0f));
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "LUCID");
                    ImGui::SameLine(0.0f, 14.0f * s_uiScale);

                    if (ImGui::BeginMenu("File")) {
                        if (ImGui::MenuItem("New Level (Nowy poziom)", "Ctrl+N")) {
                            if (engine) { selectedEnt = entt::null; engine->setCurrentPreset("empty"); engine->resetScene("empty"); }
                        }
                        if (ImGui::MenuItem("Open Project... (Otworz z dysku)", "Ctrl+O")) {
                            loadCurrentGame("");
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Save Project (Zapisz)", "Ctrl+S")) {
                            saveCurrentGame(false);
                        }
                        if (ImGui::MenuItem("Save Project As... (Zapisz jako...)", "Ctrl+Shift+S")) {
                            saveCurrentGame(true);
                        }
                        if (ImGui::BeginMenu("Szybki wybor")) {
                            if (ImGui::MenuItem("Wczytaj 'projects/MyGame'")) loadCurrentGame("projects/MyGame");
                            ImGui::EndMenu();
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Kompiluj pelna gre (.exe)", "Ctrl+Shift+P")) {
                            saveCurrentGame();
                            if (engine && engine->packageStandaloneGame("Build/Windows/MyLucidGame", "MyLucidGame")) {
                                snprintf(s_engineNotification, sizeof(s_engineNotification), "Gra skompilowana do Build/Windows/MyLucidGame/!");
                                s_engineNotificationTimer = 5.0f;
                            }
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Edit")) {
                        if (ImGui::MenuItem("World & Graphics Settings")) { s_detailsTab = 1; s_showDetails = true; }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Window")) {
                        ImGui::MenuItem("Place Actors", nullptr, &s_showPlaceActors);
                        ImGui::MenuItem("Outliner", nullptr, &s_showOutliner);
                        ImGui::MenuItem("Details", nullptr, &s_showDetails);
                        ImGui::MenuItem("Content Drawer", "Ctrl+Space", &s_showContentDrawer);
                        ImGui::MenuItem("Output Log", nullptr, &s_showOutputLog);
                        ImGui::MenuItem("Blueprints / Scripts", nullptr, &s_showScriptStudio);
                        ImGui::Separator();
                        if (ImGui::BeginMenu("Skala Interfejsu (UI Scale)")) {
                            if (ImGui::MenuItem("85% (Kompaktowe)", nullptr, std::abs(m_uiScale - 0.85f) < 0.05f)) m_uiScale = 0.85f;
                            if (ImGui::MenuItem("100% (Standard)", nullptr, std::abs(m_uiScale - 1.0f) < 0.05f)) m_uiScale = 1.0f;
                            if (ImGui::MenuItem("125% (Duze)", nullptr, std::abs(m_uiScale - 1.25f) < 0.05f)) m_uiScale = 1.25f;
                            if (ImGui::MenuItem("150% (Bardzo duze / 4K)", nullptr, std::abs(m_uiScale - 1.50f) < 0.05f)) m_uiScale = 1.50f;
                            ImGui::EndMenu();
                        }
                        if (ImGui::MenuItem("Zresetuj uklad okien do domyslnego")) {
                            s_userLeftPanelW  = 260.0f * s_uiScale;
                            s_userRightPanelW = 310.0f * s_uiScale;
                            s_userOutlinerH   = 240.0f * s_uiScale;
                            s_userDrawerH     = 220.0f * s_uiScale;
                            s_showPlaceActors = true;
                            s_showOutliner    = true;
                            s_showDetails     = true;
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
                            if (ImGui::MenuItem("Off (Brak AA)", nullptr, m_antiAliasingMode == 0)) m_antiAliasingMode = 0;
                            if (ImGui::MenuItem("FXAA Fast", nullptr, m_antiAliasingMode == 1)) m_antiAliasingMode = 1;
                            if (ImGui::MenuItem("FXAA Quality (Subpixel)", nullptr, m_antiAliasingMode == 2)) m_antiAliasingMode = 2;
                            if (ImGui::MenuItem("FXAA Ultra", nullptr, m_antiAliasingMode == 3)) m_antiAliasingMode = 3;
                            ImGui::EndMenu();
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Tools")) {
                        if (ImGui::MenuItem("Execute QuickJS Script")) s_showScriptStudio = true;
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
                ImVec2 tabStart = ImGui::GetCursorScreenPos();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.086f, 0.086f, 0.125f, 1.0f));
                if (ImGui::Button("    CzystyPoziom.scene  (Persistent)   x ", ImVec2(0, topLevelTabsH - 4.0f)) && engine) {
                    selectedEnt = entt::null;
                    engine->resetScene(curPreset);
                }
                ImGui::PopStyleColor();
                ImVec2 tabEnd = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddLine(tabStart, ImVec2(tabEnd.x, tabStart.y), IM_COL32(0, 130, 240, 255), 2.0f);
                drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(tabStart.x + 11.0f * s_uiScale, (tabStart.y + tabEnd.y) * 0.5f), 5.0f * s_uiScale, VectorIconType::Folder, IM_COL32(245, 158, 11, 255));

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
            if (ImGui::Button("     Zapisz ", ImVec2(0, btnH))) saveCurrentGame();
            drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(sPos.x + 11.0f * s_uiScale, sPos.y + btnH * 0.5f), 5.5f * s_uiScale, VectorIconType::SaveDisk, IM_COL32(245, 158, 11, 255));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zapisz biezacy stan projektu [Ctrl+S]");
            ImGui::SameLine();

            // Studio Skryptow
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.72f, 1.0f, 1.0f));
            if (ImGui::Button(" </> Skrypty ", ImVec2(0, btnH))) s_showScriptStudio = !s_showScriptStudio;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Otworz QuickJS ESM Live Coding & Skrypty (F4)");
            ImGui::SameLine();

            // Szybki dostep do oswietlenia i renderera
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.32f, 1.0f));
            ImVec2 gPos = ImGui::GetCursorScreenPos();
            if (ImGui::Button("     Swiatlo & Grafika ", ImVec2(0, btnH))) { s_detailsTab = 1; s_showDetails = true; }
            drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(gPos.x + 11.0f * s_uiScale, gPos.y + btnH * 0.5f), 5.5f * s_uiScale, VectorIconType::Gear, IM_COL32(251, 191, 36, 255));
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
            drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(plPos.x + 11.0f * s_uiScale, plPos.y + btnH * 0.5f), 5.0f * s_uiScale, VectorIconType::Play, isPaused ? IM_COL32(34, 197, 94, 255) : IM_COL32(255, 255, 255, 255));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Uruchom symulacje fizyki Jolt i skrypty");
            ImGui::SameLine();

            ImVec2 paPos = ImGui::GetCursorScreenPos();
            if (ImGui::Button("     Pauza ", ImVec2(0, btnH)) && engine) engine->setPaused(true);
            drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(paPos.x + 11.0f * s_uiScale, paPos.y + btnH * 0.5f), 4.8f * s_uiScale, VectorIconType::Pause, IM_COL32(251, 191, 36, 255));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wstrzymaj symulacje fizyki");
            ImGui::SameLine();

            ImVec2 stPos = ImGui::GetCursorScreenPos();
            if (ImGui::Button("     Stop ", ImVec2(0, btnH)) && engine) engine->stopSimulation();
            drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(stPos.x + 11.0f * s_uiScale, stPos.y + btnH * 0.5f), 4.8f * s_uiScale, VectorIconType::Stop, IM_COL32(239, 68, 68, 255));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zatrzymaj i przywroc pozycje edycyjne obiektow");
            ImGui::SameLine();

            if (ImGui::Button(" Krok ", ImVec2(0, btnH)) && engine) engine->stepSimulation(1, 1.0f / 60.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wykonaj krok symulacji o 1 klatke fizyki (1/60s)");

            // Wybor perspektywy kamery po prawej stronie paska
            ImGui::SameLine(screenW - 170.0f * s_uiScale);
            static int s_camMode = 0;
            const char* camModes[] = { "Perspektywa", "Ortogonalna" };
            ImGui::SetNextItemWidth(155.0f * s_uiScale);
            ImGui::Combo("##CameraModeCombo", &s_camMode, camModes, IM_ARRAYSIZE(camModes));

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            // 4A. FAR-LEFT TOOL DOCK & 4B. PLACE ACTORS PALETTE
            const float toolDockW = (!s_immersivePlay && s_showPlaceActors) ? (36.0f * s_uiScale) : 0.0f;
            if (!s_immersivePlay && s_showPlaceActors) {
                ImGui::SetNextWindowPos(ImVec2(0, topTotalH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(toolDockW, contentH), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.055f, 0.075f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f * s_uiScale, 8.0f * s_uiScale));
                ImGui::Begin("FarLeftToolDock", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

                static int s_activeToolMode = 1;
                const VectorIconType dockIcons[6] = {
                    VectorIconType::Pointer, VectorIconType::Cube, VectorIconType::Mountain,
                    VectorIconType::Leaf, VectorIconType::Brush, VectorIconType::Gear
                };
                const float itemSz = 26.0f * s_uiScale;
                for (int i = 0; i < 6; ++i) {
                    ImGui::PushID(200 + i);
                    const bool active = (s_activeToolMode == i);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.0f, 0.44f, 0.95f, 1.0f) : ImVec4(0.086f, 0.106f, 0.133f, 1.0f));
                    if (ImGui::Button("##DockBtn", ImVec2(itemSz, itemSz))) {
                        s_activeToolMode = i;
                        if (i == 5) { s_detailsTab = 1; s_showDetails = true; }
                    }
                    ImGui::PopStyleColor();
                    drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + itemSz * 0.5f, p.y + itemSz * 0.5f), 7.5f * s_uiScale, dockIcons[i], IM_COL32(235, 242, 255, 240));
                    ImGui::PopID();
                    ImGui::Spacing();
                }
                ImGui::End();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();

                const float placePanelW = leftPanelW - toolDockW;
                ImGui::SetNextWindowPos(ImVec2(toolDockW, topTotalH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(placePanelW, contentH), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.051f, 0.067f, 0.090f, 1.0f));
                ImGui::Begin("PlaceActorsPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

                ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Place Actors");
                ImGui::SameLine(placePanelW - 22.0f * s_uiScale);
                if (ImGui::SmallButton("<")) s_showPlaceActors = false;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zwin panel Place Actors");
                static char classSearch[64] = "";
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##SearchClasses", "Search Classes...", classSearch, sizeof(classSearch));
                ImGui::Separator();

                auto spawnCard = [&](const char* title, const char* desc, int spawnType, VectorIconType iconType, ImU32 iconCol) {
                    if (classSearch[0] != '\0') {
                        std::string tL = title, sL = classSearch;
                        std::transform(tL.begin(), tL.end(), tL.begin(), ::tolower);
                        std::transform(sL.begin(), sL.end(), sL.begin(), ::tolower);
                        if (tL.find(sL) == std::string::npos) return;
                    }
                    ImGui::PushID(title);
                    const float cardH = 38.0f * s_uiScale;
                    ImGui::BeginChild(title, ImVec2(0, cardH), true, ImGuiWindowFlags_NoScrollbar);
                    ImDrawList* cdl = ImGui::GetWindowDrawList();
                    ImVec2 cPos = ImGui::GetWindowPos();
                    const float tileSz = 26.0f * s_uiScale;
                    ImVec2 tileMin(cPos.x + 5.0f * s_uiScale, cPos.y + (cardH - tileSz) * 0.5f);
                    cdl->AddRectFilled(tileMin, ImVec2(tileMin.x + tileSz, tileMin.y + tileSz), IM_COL32(18, 24, 34, 255), 4.0f);
                    drawVectorIcon(cdl, ImVec2(tileMin.x + tileSz * 0.5f, tileMin.y + tileSz * 0.5f), 6.5f * s_uiScale, iconType, iconCol);

                    ImGui::SetCursorPos(ImVec2(36.0f * s_uiScale, 3.5f * s_uiScale));
                    ImGui::TextColored(ImVec4(0.96f, 0.97f, 1.0f, 1.0f), "%s", title);
                    ImGui::SetCursorPos(ImVec2(36.0f * s_uiScale, 19.0f * s_uiScale));
                    ImGui::TextColored(ImVec4(0.58f, 0.64f, 0.74f, 1.0f), "%s", desc);
                    ImGui::EndChild();
                    const bool clicked = ImGui::IsItemClicked();
                    ImGui::PopID();

                    if (clicked) {
                        auto entity = registry.createEntity();
                        glm::vec3 spawnPos = camPos + front * 7.0f;
                        if (spawnPos.y < 0.5f) spawnPos.y = 0.5f;

                        if (spawnType == 0) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"PlatformBlock"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos, glm::vec3(0.0f), glm::vec3(2.0f, 0.8f, 2.0f));
                            registry.get().emplace<ecs::MeshComponent>(entity, ecs::MeshComponent{2});
                            ecs::RigidBodyComponent rb; rb.mass = 0.0f; rb.shapeType = 0;
                            registry.get().emplace<ecs::RigidBodyComponent>(entity, rb);
                            ecs::MaterialComponent mat; mat.baseColor = glm::vec4(0.2f, 0.45f, 0.75f, 1.0f); mat.roughness = 0.45f;
                            registry.get().emplace<ecs::MaterialComponent>(entity, mat);
                        } else if (spawnType == 1) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"Player"});
                            registry.get().emplace<ecs::TransformComponent>(entity, glm::vec3(spawnPos.x, std::max(2.0f, spawnPos.y), spawnPos.z));
                            registry.get().emplace<ecs::MeshComponent>(entity, ecs::MeshComponent{2});
                            ecs::RigidBodyComponent rb; rb.mass = 1.0f; rb.shapeType = 2; rb.capsuleRadius = 0.6f; rb.capsuleHalfHeight = 0.8f;
                            rb.lockRotationX = true; rb.lockRotationZ = true;
                            registry.get().emplace<ecs::RigidBodyComponent>(entity, rb);
                            ecs::MaterialComponent mat; mat.baseColor = glm::vec4(0.0f, 0.85f, 1.0f, 1.0f); mat.metallic = 0.25f; mat.roughness = 0.25f;
                            registry.get().emplace<ecs::MaterialComponent>(entity, mat);
                        } else if (spawnType == 3) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"PointLight"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos + glm::vec3(0.0f, 2.5f, 0.0f));
                            registry.get().emplace<ecs::LightComponent>(entity, ecs::LightComponent{"point", glm::vec3(1.0f, 0.95f, 0.8f), 4.0f, 20.0f});
                        } else if (spawnType == 7) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"ChromeSphere"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos + glm::vec3(0.0f, 2.0f, 0.0f));
                            registry.get().emplace<ecs::MeshComponent>(entity, ecs::MeshComponent{3});
                            ecs::RigidBodyComponent rb; rb.mass = 1.0f; rb.shapeType = 1; rb.restitution = 0.75f;
                            registry.get().emplace<ecs::RigidBodyComponent>(entity, rb);
                            ecs::MaterialComponent mat; mat.baseColor = glm::vec4(0.95f, 0.82f, 0.55f, 1.0f); mat.metallic = 1.0f; mat.roughness = 0.08f;
                            registry.get().emplace<ecs::MaterialComponent>(entity, mat);
                        } else if (spawnType == 8) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"FireVFX"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos);
                            registry.get().emplace<ecs::ParticleEmitterComponent>(entity, ecs::ParticleEmitterComponent{"fire", glm::vec3(1.0f, 0.4f, 0.0f), 200, 3.0f, 0.4f});
                        } else if (spawnType == 9) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"DirectionalLight (Sun)"});
                            registry.get().emplace<ecs::TransformComponent>(entity, camPos + glm::vec3(0.0f, 12.0f, 0.0f), glm::vec3(-45.0f, -40.0f, 0.0f), glm::vec3(1.0f));
                            registry.get().emplace<ecs::LightComponent>(entity, ecs::LightComponent{"directional", glm::vec3(1.0f, 0.98f, 0.92f), 3.5f, 0.0f});
                        } else if (spawnType == 10) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"SpotLight"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos + glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(-60.0f, 0.0f, 0.0f), glm::vec3(1.0f));
                            registry.get().emplace<ecs::LightComponent>(entity, ecs::LightComponent{"spot", glm::vec3(1.0f, 0.95f, 0.85f), 5.0f, 25.0f, 25.0f, 40.0f, 2.0f, 2.0f});
                        } else if (spawnType == 11) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"AreaLight"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos + glm::vec3(0.0f, 3.5f, 0.0f), glm::vec3(-45.0f, 0.0f, 0.0f), glm::vec3(1.0f));
                            registry.get().emplace<ecs::LightComponent>(entity, ecs::LightComponent{"area", glm::vec3(1.0f, 0.98f, 0.95f), 6.0f, 20.0f, 25.0f, 40.0f, 3.0f, 2.0f});
                        }
                        selectedEnt = entity;
                        if (engine) engine->setSelectedEntity(selectedEnt);
                    }
                };

                ImGui::BeginChild("PlaceActorListScroll", ImVec2(0, 0), false);
                spawnCard("Platform Cube",     "Static Jolt Box Actor",            0,  VectorIconType::Cube,      IM_COL32(148, 163, 184, 255));
                spawnCard("PBR Sphere",        "Metallic GGX / IBL Test Sphere",   7,  VectorIconType::TriggerSphere, IM_COL32(250, 204, 21, 255));
                spawnCard("Character (WASD)",  "Controllable Capsule Player",      1,  VectorIconType::Character, IM_COL32(52, 211, 153, 255));
                spawnCard("Directional Light", "4-Cascade Vogel PCF Sun",          9,  VectorIconType::Light,     IM_COL32(250, 204, 21, 255));
                spawnCard("Point Light",       "Omnidirectional Shadow Light",     3,  VectorIconType::Light,     IM_COL32(250, 204, 21, 255));
                spawnCard("Spot Light",        "Conical Spotlight Emitter",        10, VectorIconType::Light,     IM_COL32(245, 158, 11, 255));
                spawnCard("Area Light",        "Karis Rectangular Emitter",        11, VectorIconType::Light,     IM_COL32(56, 189, 248, 255));
                spawnCard("Fire VFX",          "Soft Gaussian HDR Particles",      8,  VectorIconType::Sparkles,  IM_COL32(251, 146, 60, 255));
                ImGui::EndChild();

                ImGui::End();
                ImGui::PopStyleColor();
            }

            // 5. CENTER PANEL: BORDERLESS 3D VIEWPORT
            ImGui::SetNextWindowPos(ImVec2(leftPanelW, topTotalH), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(centerViewportW, contentH), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.027f, 0.027f, 0.047f, 1.0f));
            ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
            if (viewportPanelSize.x > 10.0f && viewportPanelSize.y > 10.0f) {
                m_viewportAspectRatio = viewportPanelSize.x / viewportPanelSize.y;
                if (m_autoViewportResolution && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    const uint32_t desiredW = (static_cast<uint32_t>(viewportPanelSize.x * m_resolutionScale) + 1u) & ~1u;
                    const uint32_t desiredH = (static_cast<uint32_t>(viewportPanelSize.y * m_resolutionScale) + 1u) & ~1u;
                    if (std::abs(static_cast<int>(desiredW) - static_cast<int>(m_width)) >= 4 ||
                        std::abs(static_cast<int>(desiredH) - static_cast<int>(m_height)) >= 4) {
                        requestResolution(desiredW, desiredH);
                    }
                }
            }

            if (m_viewportDescriptor != VK_NULL_HANDLE && viewportPanelSize.x > 0 && viewportPanelSize.y > 0) {
                ImGui::Image((ImTextureID)m_viewportDescriptor, viewportPanelSize);
            }

            const ImVec2 vMin = ImGui::GetItemRectMin();
            const ImVec2 vSize = ImGui::GetItemRectSize();
            const bool isHovered = ImGui::IsItemHovered();
            const bool isWindowFocused = ImGui::IsWindowFocused();
            const ImVec2 mousePos = ImGui::GetIO().MousePos;
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            const float aspect = (m_viewportAspectRatio <= 0.1f) ? (16.0f / 9.0f) : m_viewportAspectRatio;
            glm::mat4 curProj = glm::perspective(glm::radians(camFov), aspect, 0.1f, 1000.0f);
            curProj[1][1] *= -1.0f;
            glm::mat4 curView = glm::lookAt(camPos, camPos + front, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 curVP = curProj * curView;
            glm::mat4 invVP = glm::inverse(curVP);

            auto worldToScreen = [&](const glm::vec3& worldPos, ImVec2& screenPt, float& outW) -> bool {
                const glm::vec4 clip = curVP * glm::vec4(worldPos, 1.0f);
                outW = clip.w;
                if (clip.w <= 0.05f) return false;
                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                screenPt.x = vMin.x + (ndc.x * 0.5f + 0.5f) * vSize.x;
                screenPt.y = vMin.y + (ndc.y * 0.5f + 0.5f) * vSize.y;
                return true;
            };

            auto getRayFromScreen = [&](const ImVec2& screenPt, glm::vec3& outOrig, glm::vec3& outDir) {
                const float relX = (screenPt.x - vMin.x) / std::max(1.0f, vSize.x);
                const float relY = (screenPt.y - vMin.y) / std::max(1.0f, vSize.y);
                glm::vec4 nearPt = invVP * glm::vec4(relX * 2.0f - 1.0f, relY * 2.0f - 1.0f, 0.0f, 1.0f);
                nearPt /= nearPt.w;
                glm::vec4 farPt = invVP * glm::vec4(relX * 2.0f - 1.0f, relY * 2.0f - 1.0f, 1.0f, 1.0f);
                farPt /= farPt.w;
                outOrig = camPos;
                outDir = glm::normalize(glm::vec3(farPt - nearPt));
            };

            auto intersectRayPlane = [](const glm::vec3& rayOrig, const glm::vec3& rayDir, const glm::vec3& planePos, const glm::vec3& planeNormal, glm::vec3& outHit) -> bool {
                const float denom = glm::dot(rayDir, planeNormal);
                if (std::abs(denom) < 1e-5f) return false;
                const float t = glm::dot(planePos - rayOrig, planeNormal) / denom;
                if (t < 0.05f) return false;
                outHit = rayOrig + t * rayDir;
                return true;
            };

            if ((isHovered || isWindowFocused) && !ImGui::GetIO().WantTextInput) {
                const bool ctrlDown = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
                const bool shiftDown = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
                if (ctrlDown && shiftDown && ImGui::IsKeyPressed(ImGuiKey_S)) {
                    saveCurrentGame(true);
                } else if (ctrlDown && ImGui::IsKeyPressed(ImGuiKey_S)) {
                    saveCurrentGame(false);
                }
                if (ctrlDown && ImGui::IsKeyPressed(ImGuiKey_O)) {
                    loadCurrentGame("");
                }
                if (!s_isRmbNavigating) {
                    if (ImGui::IsKeyPressed(ImGuiKey_W) || ImGui::IsKeyPressed(ImGuiKey_G)) s_gizmoMode = NativeGizmoMode::TRANSLATE;
                    if (ImGui::IsKeyPressed(ImGuiKey_E) || (!ctrlDown && ImGui::IsKeyPressed(ImGuiKey_R))) s_gizmoMode = NativeGizmoMode::ROTATE;
                    if (ImGui::IsKeyPressed(ImGuiKey_S) && !ctrlDown) s_gizmoMode = NativeGizmoMode::SCALE;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_F) && selectedEnt != entt::null && registry.get().valid(selectedEnt) && registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                    const auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                    if (engine) engine->setCamera(t.position - front * 8.0f, camYaw, camPitch, camFov);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedEnt != entt::null && registry.get().valid(selectedEnt)) {
                    registry.get().destroy(selectedEnt);
                    selectedEnt = entt::null;
                    if (engine) engine->setSelectedEntity(entt::null);
                }
            }

            entt::entity hoveredLightEntity = entt::null;
            float closestLightDist = 1e9f;
            if (isHovered && !s_isRmbNavigating && !s_isMmbPanning && !s_isAltOrbiting) {
                auto lightEntities = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
                for (auto lEnt : lightEntities) {
                    const auto& tc = lightEntities.get<ecs::TransformComponent>(lEnt);
                    ImVec2 sPos; float sW = 0.0f;
                    if (worldToScreen(tc.position, sPos, sW) && sW > 0.05f) {
                        const float d = hypotf(mousePos.x - sPos.x, mousePos.y - sPos.y);
                        if (d <= 24.0f * s_uiScale && d < closestLightDist) {
                            closestLightDist = d;
                            hoveredLightEntity = lEnt;
                        }
                    }
                }
            }

            int hoveredGizmoAxis = 0;
            ImVec2 originScreen(0, 0), ptX(0, 0), ptY(0, 0), ptZ(0, 0);
            bool hasSelectedTransform = (selectedEnt != entt::null && registry.get().valid(selectedEnt) && registry.get().all_of<ecs::TransformComponent>(selectedEnt));

            if (hasSelectedTransform) {
                auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                const float gizmoLen = std::max(0.7f, glm::length(camPos - t.position) * 0.14f);
                float originW = 0.0f, wX = 0.0f, wY = 0.0f, wZ = 0.0f;
                const bool originVisible = worldToScreen(t.position, originScreen, originW);
                const bool xVis = worldToScreen(t.position + glm::vec3(gizmoLen, 0.0f, 0.0f), ptX, wX);
                const bool yVis = worldToScreen(t.position + glm::vec3(0.0f, gizmoLen, 0.0f), ptY, wY);
                const bool zVis = worldToScreen(t.position + glm::vec3(0.0f, 0.0f, gizmoLen), ptZ, wZ);

                if (originVisible && isHovered && !s_isRmbNavigating && !s_isMmbPanning && !s_isAltOrbiting) {
                    if (hypotf(mousePos.x - originScreen.x, mousePos.y - originScreen.y) <= 14.0f) hoveredGizmoAxis = 4;
                    else if (xVis && distToSegment(mousePos, originScreen, ptX) <= 12.0f) hoveredGizmoAxis = 1;
                    else if (yVis && distToSegment(mousePos, originScreen, ptY) <= 12.0f) hoveredGizmoAxis = 2;
                    else if (zVis && distToSegment(mousePos, originScreen, ptZ) <= 12.0f) hoveredGizmoAxis = 3;
                }
            }

            const bool blockViewportClicks = (s_activeSplitterId != 0 || s_wasSplitterHoveredLastFrame);
            if (!blockViewportClicks && isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { s_isRmbNavigating = true; ImGui::SetWindowFocus("Viewport"); }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) s_isRmbNavigating = false;
            if (!blockViewportClicks && isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) { s_isMmbPanning = true; ImGui::SetWindowFocus("Viewport"); }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) s_isMmbPanning = false;

            const bool altDown = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
            if (!blockViewportClicks && isHovered && altDown && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { s_isAltOrbiting = true; ImGui::SetWindowFocus("Viewport"); }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { s_isAltOrbiting = false; s_draggingGizmoAxis = 0; }

            if (!blockViewportClicks && isHovered && !altDown && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (hoveredGizmoAxis > 0 && hasSelectedTransform) {
                    s_draggingGizmoAxis = hoveredGizmoAxis;
                    auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                    s_dragInitialEntityPos = t.position;
                    s_dragInitialRotation  = t.rotation;
                    s_dragInitialScale     = t.scale;
                    s_dragPivotScreen      = originScreen;

                    s_dragAxisVec = (s_draggingGizmoAxis == 1) ? glm::vec3(1,0,0) :
                                    (s_draggingGizmoAxis == 2) ? glm::vec3(0,1,0) :
                                    (s_draggingGizmoAxis == 3) ? glm::vec3(0,0,1) : glm::vec3(0);

                    if (s_draggingGizmoAxis >= 1 && s_draggingGizmoAxis <= 3) {
                        const glm::vec3 toCam = camPos - s_dragInitialEntityPos;
                        glm::vec3 vPerp = toCam - glm::dot(toCam, s_dragAxisVec) * s_dragAxisVec;
                        if (glm::length(vPerp) < 1e-4f) vPerp = glm::vec3(0.0f, 1.0f, 0.0f);
                        s_dragPlaneNormal = glm::normalize(vPerp);
                    } else {
                        s_dragPlaneNormal = -front;
                    }

                    glm::vec3 clickRayOrig, clickRayDir;
                    getRayFromScreen(mousePos, clickRayOrig, clickRayDir);
                    s_dragHasHit = intersectRayPlane(clickRayOrig, clickRayDir, s_dragInitialEntityPos, s_dragPlaneNormal, s_dragInitialHitPoint);
                    s_dragInitialOffset = (s_dragHasHit && s_draggingGizmoAxis <= 3) ? glm::dot(s_dragInitialHitPoint - s_dragInitialEntityPos, s_dragAxisVec) : 0.0f;
                    s_lastRotAngle = atan2f(mousePos.y - originScreen.y, mousePos.x - originScreen.x) * (180.0f / 3.14159265f);
                    s_accumulatedRotAngle = 0.0f;
                    s_dragInitialDist = std::max(10.0f, hypotf(mousePos.x - originScreen.x, mousePos.y - originScreen.y));
                } else if (hoveredLightEntity != entt::null) {
                    selectedEnt = hoveredLightEntity;
                    if (engine) engine->setSelectedEntity(selectedEnt);
                    hasSelectedTransform = true;
                } else {
                    glm::vec3 rayOrig, rayDir;
                    getRayFromScreen(mousePos, rayOrig, rayDir);
                    entt::entity hitEnt = entt::null;
                    float closestDist = 1e9f;

                    auto tView = registry.get().view<ecs::TransformComponent>();
                    for (auto entity : tView) {
                        const auto& tc = tView.get<ecs::TransformComponent>(entity);
                        if (registry.get().all_of<ecs::MeshComponent>(entity) && registry.get().get<ecs::MeshComponent>(entity).meshId == 1) continue;
                        if (registry.get().all_of<ecs::TagComponent>(entity)) {
                            const auto& tn = registry.get().get<ecs::TagComponent>(entity).name;
                            if (tn == "Ground" || tn == "GroundPlane") continue;
                        }
                        const glm::vec3 halfExt = registry.get().all_of<ecs::LightComponent>(entity)
                            ? glm::vec3(1.5f)
                            : glm::max(tc.scale * 0.5f, glm::vec3(0.05f));
                        const glm::quat rot = glm::dot(tc.orientation, tc.orientation) < 0.001f ? glm::quat(glm::radians(tc.rotation)) : glm::normalize(tc.orientation);
                        const glm::quat invRot = glm::inverse(rot);
                        const glm::vec3 rOrigLocal = invRot * (rayOrig - tc.position);
                        const glm::vec3 rDirLocal  = invRot * rayDir;
                        const glm::vec3 invD(
                            std::abs(rDirLocal.x) > 1e-7f ? 1.0f / rDirLocal.x : 1e7f,
                            std::abs(rDirLocal.y) > 1e-7f ? 1.0f / rDirLocal.y : 1e7f,
                            std::abs(rDirLocal.z) > 1e-7f ? 1.0f / rDirLocal.z : 1e7f
                        );
                        const glm::vec3 t0 = (-halfExt - rOrigLocal) * invD;
                        const glm::vec3 t1 = (+halfExt - rOrigLocal) * invD;
                        const glm::vec3 tMinV = glm::min(t0, t1), tMaxV = glm::max(t0, t1);
                        const float tNear = std::max({tMinV.x, tMinV.y, tMinV.z});
                        const float tFar  = std::min({tMaxV.x, tMaxV.y, tMaxV.z});
                        if (tNear <= tFar && tFar >= 0.0f) {
                            const float tHit = (tNear >= 0.0f) ? tNear : tFar;
                            if (tHit < closestDist) { closestDist = tHit; hitEnt = entity; }
                        }
                    }
                    selectedEnt = hitEnt;
                    if (engine) engine->setSelectedEntity(hitEnt);
                    hasSelectedTransform = (selectedEnt != entt::null && registry.get().valid(selectedEnt) && registry.get().all_of<ecs::TransformComponent>(selectedEnt));
                }
            }

            if (s_draggingGizmoAxis > 0 && hasSelectedTransform) {
                auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                if (s_gizmoMode == NativeGizmoMode::TRANSLATE) {
                    glm::vec3 curRayOrig, curRayDir;
                    getRayFromScreen(mousePos, curRayOrig, curRayDir);
                    const float denom = glm::dot(curRayDir, s_dragPlaneNormal);
                    if (std::abs(denom) > 0.03f) {
                        const float tDist = glm::dot(s_dragInitialEntityPos - curRayOrig, s_dragPlaneNormal) / denom;
                        if (tDist > 0.05f && tDist < 5000.0f) {
                            const glm::vec3 curHit = curRayOrig + tDist * curRayDir;
                            if (s_draggingGizmoAxis <= 3) {
                                float deltaAxis = glm::dot(curHit - s_dragInitialEntityPos, s_dragAxisVec) - s_dragInitialOffset;
                                if (s_snapEnabled && s_snapStep > 0.01f) deltaAxis = std::round(deltaAxis / s_snapStep) * s_snapStep;
                                t.position = s_dragInitialEntityPos + s_dragAxisVec * deltaAxis;
                            } else {
                                t.position = s_dragInitialEntityPos + (curHit - s_dragInitialHitPoint);
                            }
                            if (engine) engine->syncEntityTransformPhysics(selectedEnt);
                        }
                    }
                } else if (s_gizmoMode == NativeGizmoMode::ROTATE) {
                    const float curAngle = atan2f(mousePos.y - s_dragPivotScreen.y, mousePos.x - s_dragPivotScreen.x) * (180.0f / 3.14159265f);
                    float dAngle = curAngle - s_lastRotAngle;
                    while (dAngle > 180.0f) dAngle -= 360.0f;
                    while (dAngle < -180.0f) dAngle += 360.0f;
                    s_accumulatedRotAngle += dAngle;
                    s_lastRotAngle = curAngle;
                    const float finalAngle = s_snapEnabled ? std::round(s_accumulatedRotAngle / 15.0f) * 15.0f : s_accumulatedRotAngle;
                    t.rotation = s_dragInitialRotation;
                    if (s_draggingGizmoAxis == 1) t.rotation.x += finalAngle;
                    else if (s_draggingGizmoAxis == 2 || s_draggingGizmoAxis == 4) t.rotation.y += finalAngle;
                    else if (s_draggingGizmoAxis == 3) t.rotation.z += finalAngle;
                    t.orientation = glm::quat(glm::radians(t.rotation));
                    if (engine) engine->syncEntityTransformPhysics(selectedEnt);
                } else if (s_gizmoMode == NativeGizmoMode::SCALE) {
                    float scaleFactor = hypotf(mousePos.x - s_dragPivotScreen.x, mousePos.y - s_dragPivotScreen.y) / s_dragInitialDist;
                    if (s_snapEnabled) scaleFactor = std::max(0.1f, std::round(scaleFactor / 0.25f) * 0.25f);
                    if (s_draggingGizmoAxis == 1) t.scale.x = std::max(0.01f, s_dragInitialScale.x * scaleFactor);
                    else if (s_draggingGizmoAxis == 2) t.scale.y = std::max(0.01f, s_dragInitialScale.y * scaleFactor);
                    else if (s_draggingGizmoAxis == 3) t.scale.z = std::max(0.01f, s_dragInitialScale.z * scaleFactor);
                    else t.scale = glm::max(glm::vec3(0.01f), s_dragInitialScale * scaleFactor);
                }
            }

            if (s_isRmbNavigating && engine) {
                camYaw += ImGui::GetIO().MouseDelta.x * 0.20f;
                camPitch = glm::clamp(camPitch - ImGui::GetIO().MouseDelta.y * 0.20f, -89.0f, 89.0f);
                const glm::vec3 cf = glm::normalize(glm::vec3(cosf(glm::radians(camYaw)) * cosf(glm::radians(camPitch)), sinf(glm::radians(camPitch)), sinf(glm::radians(camYaw)) * cosf(glm::radians(camPitch))));
                const glm::vec3 cr = glm::normalize(glm::cross(cf, glm::vec3(0.0f, 1.0f, 0.0f)));
                float speed = s_camFlySpeed * ImGui::GetIO().DeltaTime * (s_cameraSpeed / 4.0f);
                if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) speed *= 3.0f;
                if (ImGui::IsKeyDown(ImGuiKey_W)) camPos += cf * speed;
                if (ImGui::IsKeyDown(ImGuiKey_S)) camPos -= cf * speed;
                if (ImGui::IsKeyDown(ImGuiKey_A)) camPos -= cr * speed;
                if (ImGui::IsKeyDown(ImGuiKey_D)) camPos += cr * speed;
                if (ImGui::IsKeyDown(ImGuiKey_E)) camPos.y += speed;
                if (ImGui::IsKeyDown(ImGuiKey_Q)) camPos.y -= speed;
                engine->setCamera(camPos, camYaw, camPitch, camFov);
            }
            if (s_isMmbPanning && engine) {
                camPos -= (right * ImGui::GetIO().MouseDelta.x - up * ImGui::GetIO().MouseDelta.y) * 0.04f;
                engine->setCamera(camPos, camYaw, camPitch, camFov);
            }
            if (s_isAltOrbiting && engine) {
                const glm::vec3 pivot = hasSelectedTransform ? registry.get().get<ecs::TransformComponent>(selectedEnt).position : (camPos + front * 10.0f);
                const float orbitDist = std::max(0.5f, glm::length(camPos - pivot));
                camYaw += ImGui::GetIO().MouseDelta.x * 0.25f;
                camPitch = glm::clamp(camPitch - ImGui::GetIO().MouseDelta.y * 0.25f, -89.0f, 89.0f);
                const glm::vec3 of = glm::normalize(glm::vec3(cosf(glm::radians(camYaw)) * cosf(glm::radians(camPitch)), sinf(glm::radians(camPitch)), sinf(glm::radians(camYaw)) * cosf(glm::radians(camPitch))));
                engine->setCamera(pivot - of * orbitDist, camYaw, camPitch, camFov);
            }
            if (isHovered && std::abs(ImGui::GetIO().MouseWheel) > 0.01f && engine) {
                engine->setCamera(camPos + front * (ImGui::GetIO().MouseWheel * 3.0f), camYaw, camPitch, camFov);
            }

            // Ikony świateł 3D w stylu Blender / UE5
            {
                auto lightEntities = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
                for (auto lEnt : lightEntities) {
                    const auto& tc = lightEntities.get<ecs::TransformComponent>(lEnt);
                    const auto& lc = lightEntities.get<ecs::LightComponent>(lEnt);
                    ImVec2 iconCenter; float iconW = 0.0f;
                    if (!worldToScreen(tc.position, iconCenter, iconW) || iconW <= 0.05f) continue;

                    const bool isSelected = (selectedEnt == lEnt);
                    const bool isHov = (hoveredLightEntity == lEnt);
                    const ImU32 lCol = IM_COL32((int)(lc.color.r * 255), (int)(lc.color.g * 255), (int)(lc.color.b * 255), isSelected ? 255 : 215);
                    if (isSelected || isHov) {
                        drawList->AddCircle(iconCenter, 19.0f * s_uiScale, IM_COL32(0, 240, 255, isSelected ? 255 : 170), 0, 2.2f);
                    }
                    drawList->AddCircleFilled(iconCenter, 10.5f * s_uiScale, lCol);
                    drawList->AddCircle(iconCenter, 10.5f * s_uiScale, IM_COL32(255, 255, 255, 235), 0, 1.5f);
                }
            }

            // Zaznaczenie i Gizmo 3D
            if (hasSelectedTransform) {
                auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                float origW = 0.0f;
                if (worldToScreen(t.position, originScreen, origW)) {
                    const int act = (s_draggingGizmoAxis > 0) ? s_draggingGizmoAxis : hoveredGizmoAxis;
                    const ImU32 colX = (act == 1) ? IM_COL32(255, 255, 60, 255) : IM_COL32(239, 68, 68, 255);
                    const ImU32 colY = (act == 2) ? IM_COL32(255, 255, 60, 255) : IM_COL32(34, 197, 94, 255);
                    const ImU32 colZ = (act == 3) ? IM_COL32(255, 255, 60, 255) : IM_COL32(59, 130, 246, 255);
                    drawList->AddLine(originScreen, ptX, colX, (act == 1) ? 4.0f : 2.8f);
                    drawList->AddLine(originScreen, ptY, colY, (act == 2) ? 4.0f : 2.8f);
                    drawList->AddLine(originScreen, ptZ, colZ, (act == 3) ? 4.0f : 2.8f);
                    drawList->AddCircleFilled(originScreen, 5.5f, IM_COL32(245, 245, 255, 230));
                }
            }

            if (!s_immersivePlay) {
                // Pigułka narzędzi Gizmo (lewy górny róg Viewportu)
                ImGui::SetCursorPos(ImVec2(10.0f * s_uiScale, 10.0f * s_uiScale));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.055f, 0.086f, 0.90f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
                const float leftOverlayW = (s_showPlaceActors ? 270.0f : 345.0f) * s_uiScale;
                ImGui::BeginChild("VpOverlayTopLeft", ImVec2(leftOverlayW, 30.0f * s_uiScale), true, ImGuiWindowFlags_NoScrollbar);
                if (!s_showPlaceActors) {
                    if (ImGui::Button("> Aktorzy", ImVec2(0, 22.0f * s_uiScale))) s_showPlaceActors = true;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rozwin panel Place Actors");
                    ImGui::SameLine(0.0f, 4.0f * s_uiScale);
                }
                auto gizmoToolBtn = [&](const char* id, NativeGizmoMode mode, VectorIconType icon) {
                    const bool active = (s_gizmoMode == mode);
                    ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.0f, 0.47f, 0.83f, 1.0f) : ImVec4(0.09f, 0.09f, 0.13f, 1.0f));
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    const float sz = 22.0f * s_uiScale;
                    if (ImGui::Button(id, ImVec2(sz, sz))) s_gizmoMode = mode;
                    ImGui::PopStyleColor();
                    drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + sz * 0.5f, p.y + sz * 0.5f), 5.2f * s_uiScale, icon, IM_COL32(255, 255, 255, 245));
                    ImGui::SameLine(0.0f, 3.0f * s_uiScale);
                };
                gizmoToolBtn("##GizT", NativeGizmoMode::TRANSLATE, VectorIconType::Translate);
                gizmoToolBtn("##GizR", NativeGizmoMode::ROTATE,    VectorIconType::Rotate);
                gizmoToolBtn("##GizS", NativeGizmoMode::SCALE,     VectorIconType::Scale);
                if (ImGui::Button(s_snapEnabled ? " Snap: 0.5m " : " Snap: Off ", ImVec2(0, 22.0f * s_uiScale))) s_snapEnabled = !s_snapEnabled;
                ImGui::SameLine();
                if (ImGui::Button((" Cam " + std::to_string(s_cameraSpeed) + " ").c_str(), ImVec2(0, 22.0f * s_uiScale))) s_cameraSpeed = (s_cameraSpeed % 8) + 1;
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();

                // Pigułka statusu (prawy górny róg Viewportu)
                const float trPillW = ((s_showOutliner || s_showDetails) ? 350.0f : 435.0f) * s_uiScale;
                ImGui::SetCursorPos(ImVec2(std::max(10.0f, viewportPanelSize.x - trPillW - 10.0f * s_uiScale), 10.0f * s_uiScale));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.055f, 0.086f, 0.90f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
                ImGui::BeginChild("VpOverlayTopRight", ImVec2(trPillW, 30.0f * s_uiScale), true, ImGuiWindowFlags_NoScrollbar);
                if (!s_showOutliner && !s_showDetails) {
                    if (ImGui::Button("Inspektor <", ImVec2(0, 22.0f * s_uiScale))) { s_showOutliner = true; s_showDetails = true; }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rozwin prawy panel inspektora");
                    ImGui::SameLine(0.0f, 4.0f * s_uiScale);
                }
                ImGui::TextColored(ImVec4(0.22f, 0.85f, 1.0f, 1.0f), "Cubemap IBL + ACES");
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.20f, 0.92f, 0.58f, 1.0f), "%ux%u (EV %.2f)", m_width, m_height, m_exposure);
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            // 6. OUTLINER PANEL
            if (!s_immersivePlay && s_showOutliner) {
                ImGui::SetNextWindowPos(ImVec2(leftPanelW + centerViewportW, topTotalH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(rightPanelW, outlinerH), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.055f, 0.086f, 1.0f));
                ImGui::Begin("OutlinerPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

                ImDrawList* odl = ImGui::GetWindowDrawList();
                ImVec2 oHdrPos = ImGui::GetCursorScreenPos();
                drawVectorIcon(odl, ImVec2(oHdrPos.x + 8.0f * s_uiScale, oHdrPos.y + 9.0f * s_uiScale), 5.5f * s_uiScale, VectorIconType::Folder, IM_COL32(245, 158, 11, 255));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f * s_uiScale);
                ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Hierarchia Sceny");

                auto viewAll = registry.get().view<ecs::TransformComponent>();
                ImGui::SameLine(ImGui::GetWindowWidth() - 95.0f * s_uiScale);
                ImGui::TextDisabled("[%zu]", viewAll.size());
                ImGui::SameLine(ImGui::GetWindowWidth() - 24.0f * s_uiScale);
                if (ImGui::SmallButton(">")) { s_showOutliner = false; s_showDetails = false; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zwin prawy panel inspektora");

                static int s_outlinerCategory = 0; // 0: Wszystko, 1: Siatki, 2: Swiatla, 3: Fizyka, 4: Gracz
                auto drawFilterChip = [&](const char* name, int cIdx) {
                    const bool active = (s_outlinerCategory == cIdx);
                    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.44f, 0.95f, 1.0f));
                    else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
                    if (ImGui::SmallButton(name)) s_outlinerCategory = cIdx;
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0.0f, 3.0f * s_uiScale);
                };
                drawFilterChip("Wszystko", 0);
                drawFilterChip("Siatki", 1);
                drawFilterChip("Swiatla", 2);
                drawFilterChip("Fizyka", 3);
                drawFilterChip("Gracz", 4);
                ImGui::NewLine();

                static char outlinerFilter[64] = "";
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 62.0f * s_uiScale);
                ImGui::InputTextWithHint("##OutlinerSearch", "Filtruj scene...", outlinerFilter, sizeof(outlinerFilter));
                ImGui::SameLine();
                if (ImGui::Button("+ Spawn", ImVec2(58.0f * s_uiScale, 0))) {
                    auto newEnt = registry.createEntity();
                    glm::vec3 sp = camPos + front * 6.0f;
                    if (sp.y < 0.5f) sp.y = 0.5f;
                    registry.get().emplace<ecs::TagComponent>(newEnt, ecs::TagComponent{"NowyAktor_" + std::to_string(static_cast<uint32_t>(newEnt))});
                    registry.get().emplace<ecs::TransformComponent>(newEnt, sp, glm::vec3(0.0f), glm::vec3(1.0f));
                    registry.get().emplace<ecs::MeshComponent>(newEnt, ecs::MeshComponent{2});
                    selectedEnt = newEnt;
                    if (engine) engine->setSelectedEntity(selectedEnt);
                }
                ImGui::Separator();

                ImGui::BeginChild("OutlinerTreeScroll", ImVec2(0, 0), false);
                auto view = registry.get().view<ecs::TransformComponent>();
                for (auto entity : view) {
                    std::string label = "Entity_" + std::to_string(static_cast<uint32_t>(entity));
                    if (registry.get().all_of<ecs::TagComponent>(entity)) {
                        label = registry.get().get<ecs::TagComponent>(entity).name;
                    }
                    if (outlinerFilter[0] != '\0') {
                        std::string f = outlinerFilter, l = label;
                        std::transform(f.begin(), f.end(), f.begin(), ::tolower);
                        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
                        if (l.find(f) == std::string::npos) continue;
                    }
                    const bool isSelected = (selectedEnt == entity);
                    ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity)));
                    if (ImGui::Selectable(label.c_str(), isSelected)) {
                        selectedEnt = entity;
                        if (engine) engine->setSelectedEntity(selectedEnt);
                    }

                    if (ImGui::BeginPopupContextItem("EntityRowCtxMenu")) {
                        selectedEnt = entity;
                        if (engine) engine->setSelectedEntity(selectedEnt);
                        ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "Encja #%u: %s", static_cast<uint32_t>(entity), label.c_str());
                        ImGui::Separator();
                        if (ImGui::MenuItem("Skup kamere [F]")) {
                            if (registry.get().all_of<ecs::TransformComponent>(entity)) {
                                const auto& tc = registry.get().get<ecs::TransformComponent>(entity);
                                if (engine) engine->setCamera(tc.position - front * 8.0f, camYaw, camPitch, camFov);
                            }
                        }
                        if (ImGui::MenuItem("Zresetuj pozycje (0, 0, 0)")) {
                            if (registry.get().all_of<ecs::TransformComponent>(entity)) {
                                auto& tc = registry.get().get<ecs::TransformComponent>(entity);
                                tc.position = glm::vec3(0.0f);
                                if (engine) engine->syncEntityTransformPhysics(entity);
                            }
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Usun obiekt [Delete]")) {
                            registry.get().destroy(entity);
                            if (selectedEnt == entity) selectedEnt = entt::null;
                            if (engine) engine->setSelectedEntity(entt::null);
                            ImGui::EndPopup();
                            ImGui::PopID();
                            break;
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::EndChild();
                ImGui::End();
                ImGui::PopStyleColor();
            }

            // 7. DETAILS & WORLD / GRAPHICS SETTINGS PANEL
            if (!s_immersivePlay && s_showDetails) {
                const float detailsY = topTotalH + (s_showOutliner ? outlinerH : 0.0f);

                ImGui::SetNextWindowPos(ImVec2(leftPanelW + centerViewportW, detailsY), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(rightPanelW, detailsH), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.055f, 0.086f, 1.0f));
                ImGui::Begin("DetailsPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

                if (ImGui::Button("  Details  ", ImVec2(0, 24.0f * s_uiScale))) s_detailsTab = 0;
                ImGui::SameLine();
                if (ImGui::Button("  World & Graphics  ", ImVec2(0, 24.0f * s_uiScale))) s_detailsTab = 1;
                ImGui::Separator();

                if (s_detailsTab == 0) {
                    if (selectedEnt == entt::null || !registry.get().valid(selectedEnt)) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Wybierz obiekt ze sceny");
                        ImGui::TextDisabled("Kliknij dowolny obiekt w oknie 3D lub w Outlinerze powyzej,");
                        ImGui::TextDisabled("aby wyswietlic pelne wlasciwosci (Transform, Siatka, PBR, Fizyka, Chemia, Swiatlo).");
                        ImGui::Spacing();
                        ImGui::SeparatorText("Szybkie spawnowanie obiektow");
                        auto quickSpawnBtn = [&](const char* name, int mId, glm::vec3 col) {
                            if (ImGui::Button(name)) {
                                auto ent = registry.createEntity();
                                glm::vec3 sp = camPos + front * 6.0f;
                                if (sp.y < 0.5f) sp.y = 0.5f;
                                registry.get().emplace<ecs::TagComponent>(ent, ecs::TagComponent{name});
                                registry.get().emplace<ecs::TransformComponent>(ent, sp, glm::vec3(0.0f), glm::vec3(1.0f));
                                registry.get().emplace<ecs::MeshComponent>(ent, ecs::MeshComponent{static_cast<uint32_t>(mId)});
                                ecs::RigidBodyComponent rb{}; rb.mass = 1.0f;
                                registry.get().emplace<ecs::RigidBodyComponent>(ent, rb);
                                ecs::MaterialComponent mat{}; mat.baseColor = glm::vec4(col, 1.0f); mat.roughness = 0.35f;
                                registry.get().emplace<ecs::MaterialComponent>(ent, mat);
                                selectedEnt = ent;
                                if (engine) engine->setSelectedEntity(selectedEnt);
                            }
                        };
                        quickSpawnBtn("+ Szescian Fizyczny", 2, glm::vec3(0.3f, 0.6f, 0.9f));
                        quickSpawnBtn("+ Kula Metaliczna", 3, glm::vec3(0.95f, 0.8f, 0.3f));
                        quickSpawnBtn("+ Kapsula / Postac", 5, glm::vec3(0.2f, 0.9f, 0.4f));
                        ImGui::Spacing();
                        ImGui::TextDisabled("Mozesz takze przelaczyc sie na zakladke 'World & Graphics' u gory,");
                        ImGui::TextDisabled("aby sterowac Niebem HDRI, Sloncem, Mgla i Ray Tracingiem.");
                    } else {
                        std::string tagStr = registry.get().all_of<ecs::TagComponent>(selectedEnt)
                            ? registry.get().get<ecs::TagComponent>(selectedEnt).name
                            : "Actor";
                        char nameBuf[64];
                        strncpy(nameBuf, tagStr.c_str(), sizeof(nameBuf) - 1);
                        nameBuf[sizeof(nameBuf) - 1] = '\0';
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f * s_uiScale);
                        if (ImGui::InputText("##EntityName", nameBuf, sizeof(nameBuf))) {
                            if (registry.get().all_of<ecs::TagComponent>(selectedEnt)) {
                                registry.get().get<ecs::TagComponent>(selectedEnt).name = nameBuf;
                            } else {
                                registry.get().emplace<ecs::TagComponent>(selectedEnt, ecs::TagComponent{nameBuf});
                            }
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("#%u", static_cast<uint32_t>(selectedEnt));
                        ImGui::Separator();

                        if (registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                            auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                                const bool pCh = drawVec3Control("Location", t.position, 0.0f);
                                const bool rCh = drawVec3Control("Rotation", t.rotation, 0.0f);
                                const bool sCh = drawVec3Control("Scale", t.scale, 1.0f);
                                if (rCh) t.orientation = glm::quat(glm::radians(t.rotation));
                                if ((pCh || rCh || sCh) && engine) engine->syncEntityTransformPhysics(selectedEnt);

                                ImGui::Spacing();
                                if (ImGui::Button(" Zresetuj pozycje (0, 0, 0) ")) {
                                    t.position = glm::vec3(0.0f);
                                    if (engine) engine->syncEntityTransformPhysics(selectedEnt);
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(" Skup kamere [F] ")) {
                                    if (engine) engine->setCamera(t.position - front * 8.0f, camYaw, camPitch, camFov);
                                }
                            }
                        }

                        if (registry.get().all_of<ecs::LightComponent>(selectedEnt)) {
                            auto& lc = registry.get().get<ecs::LightComponent>(selectedEnt);
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
                                    if (registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                                        auto& lt = registry.get().get<ecs::TransformComponent>(selectedEnt);
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
                                        if (drawVec3Control("Pozycja (XYZ)", lt.position, 0.0f)) changed = true;
                                        if (ImGui::DragFloat("Wysokosc / Altitude (Y)", &lt.position.y, 0.5f, -200.0f, 2000.0f, "%.1f m")) changed = true;

                                        if (changed) {
                                            lt.orientation = glm::quat(glm::radians(lt.rotation));
                                            if (engine) {
                                                engine->syncEntityTransformPhysics(selectedEnt);
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
                                    if (registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                                        auto& lt = registry.get().get<ecs::TransformComponent>(selectedEnt);
                                        bool changed = false;
                                        if (ImGui::SliderFloat("Spot Pitch", &lt.rotation.x, -89.0f, 89.0f, "%.1f deg")) changed = true;
                                        if (ImGui::SliderFloat("Spot Yaw", &lt.rotation.y, 0.0f, 360.0f, "%.1f deg")) changed = true;
                                        if (changed) {
                                            lt.orientation = glm::quat(glm::radians(lt.rotation));
                                            if (engine) engine->syncEntityTransformPhysics(selectedEnt);
                                        }
                                    }
                                } else if (curType == 3) { // Area
                                    ImGui::DragFloat("Range (m)", &lc.range, 0.5f, 1.0f, 200.0f, "%.1f m");
                                    ImGui::SliderFloat("Area Width X", &lc.areaSizeX, 0.2f, 20.0f, "%.2f m");
                                    ImGui::SliderFloat("Area Height Y", &lc.areaSizeY, 0.2f, 20.0f, "%.2f m");
                                    if (registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                                        auto& lt = registry.get().get<ecs::TransformComponent>(selectedEnt);
                                        bool changed = false;
                                        if (ImGui::SliderFloat("Area Pitch", &lt.rotation.x, -89.0f, 89.0f, "%.1f deg")) changed = true;
                                        if (ImGui::SliderFloat("Area Yaw", &lt.rotation.y, 0.0f, 360.0f, "%.1f deg")) changed = true;
                                        if (changed) {
                                            lt.orientation = glm::quat(glm::radians(lt.rotation));
                                            if (engine) engine->syncEntityTransformPhysics(selectedEnt);
                                        }
                                    }
                                }
                            }
                        }

                        if (registry.get().all_of<ecs::MeshComponent>(selectedEnt)) {
                            auto& mc = registry.get().get<ecs::MeshComponent>(selectedEnt);
                            if (ImGui::CollapsingHeader("Static Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
                                const char* meshes[] = { "None", "Plane (Ground)", "Cube (1x1x1)", "Sphere (R=0.5)", "Cylinder", "Capsule" };
                                int curMesh = std::clamp(static_cast<int>(mc.meshId), 0, 5);
                                if (ImGui::Combo("Mesh Asset", &curMesh, meshes, IM_ARRAYSIZE(meshes))) {
                                    mc.meshId = static_cast<uint32_t>(curMesh);
                                }
                            }
                        }

                        // Żywa edycja komponentu materiału PBR (Kulla-Conty Multiscatter + Cubemap IBL)
                        if (registry.get().all_of<ecs::MeshComponent>(selectedEnt)) {
                            if (ImGui::CollapsingHeader("PBR Material (Multiscatter GGX)", ImGuiTreeNodeFlags_DefaultOpen)) {
                                if (!registry.get().all_of<ecs::MaterialComponent>(selectedEnt)) {
                                    if (ImGui::Button("+ Dodaj wlasny Material PBR")) {
                                        ecs::MaterialComponent m{};
                                        m.baseColor = glm::vec4(0.85f, 0.85f, 0.90f, 1.0f);
                                        m.metallic = 0.0f;
                                        m.roughness = 0.35f;
                                        m.emissiveIntensity = 0.0f;
                                        registry.get().emplace<ecs::MaterialComponent>(selectedEnt, m);
                                    }
                                } else {
                                    auto& mat = registry.get().get<ecs::MaterialComponent>(selectedEnt);
                                    ImGui::ColorEdit3("Base Color (Albedo)", &mat.baseColor.x);
                                    ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f, "%.2f");
                                    ImGui::SliderFloat("Roughness", &mat.roughness, 0.045f, 1.0f, "%.3f");
                                    ImGui::DragFloat("Emissive HDR", &mat.emissiveIntensity, 0.05f, 0.0f, 25.0f, "%.2f");
                                }
                            }
                        }

                        if (registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt)) {
                            auto& rb = registry.get().get<ecs::RigidBodyComponent>(selectedEnt);
                            if (ImGui::CollapsingHeader("Jolt Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
                                ImGui::DragFloat("Mass (kg)", &rb.mass, 0.1f, 0.0f, 500.0f);
                                ImGui::SliderFloat("Restitution", &rb.restitution, 0.0f, 1.0f);
                                ImGui::SliderFloat("Friction", &rb.friction, 0.0f, 1.0f);
                                ImGui::Checkbox("Is Sensor (Trigger)", &rb.isSensor);
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
                        ImGui::EndDisabled();
                        ImGui::Checkbox("Ray-Traced Diffuse GI", &m_rtSettings.giEnabled);
                        ImGui::BeginDisabled(!m_rtSettings.giEnabled);
                        ImGui::SliderFloat("RT GI Intensity", &m_rtSettings.giIntensity, 0.0f, 3.0f, "%.2f");
                        ImGui::SliderInt("RT Diffuse GI Bounces", &m_rtSettings.giBounces, 1, 15, "%d");
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
                                if (drawVec3Control("Sun Pos (XYZ)##World", sunLt.position, 0.0f)) changed = true;
                                if (ImGui::DragFloat("Sun Height (Y)##World", &sunLt.position.y, 0.5f, -200.0f, 2000.0f, "%.1f m")) changed = true;

                                if (changed) {
                                    sunLt.orientation = glm::quat(glm::radians(sunLt.rotation));
                                    engine->syncEntityTransformPhysics(sunEntity);
                                }
                            } else {
                                if (ImGui::ColorEdit3("Sun Color", (float*)&env.sunColor)) changed = true;
                                if (ImGui::DragFloat("Sun Intensity", &env.sunIntensity, 0.1f, 0.0f, 25.0f, "%.2f")) changed = true;
                                if (drawVec3Control("Sun Direction##World", env.sunDirection, 0.0f)) {
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
                        if (ImGui::SliderFloat("Resolution Scale", &resSc, 0.50f, 2.00f, "%.2fx")) {
                            m_resolutionScale = std::clamp(resSc, 0.50f, 2.00f);
                            if (!m_autoViewportResolution) {
                                requestResolution(
                                    (static_cast<uint32_t>(m_baseWidth  * m_resolutionScale) + 1u) & ~1u,
                                    (static_cast<uint32_t>(m_baseHeight * m_resolutionScale) + 1u) & ~1u);
                            }
                        }
                        ImGui::SliderFloat("UI Scale", &m_uiScale, 0.75f, 1.65f, "%.2fx");
                    }
                }

                ImGui::End();
                ImGui::PopStyleColor();
            }

            // 8. BOTTOM DRAWERS & 9. STATUS BAR
            if (!s_immersivePlay && (s_showContentDrawer || s_showOutputLog)) {
                ImGui::SetNextWindowPos(ImVec2(0, screenH - bottomTotalH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(screenW, drawerH), ImGuiCond_Always);
                ImGui::Begin("BottomDrawerWindow", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
                if (s_showContentDrawer) {
                    ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.08f, 1.0f), "Content Drawer");
                    ImGui::SameLine(screenW - 28.0f * s_uiScale);
                    if (ImGui::SmallButton("X##CloseDrawer")) { s_showContentDrawer = false; }
                    ImGui::Separator();
                    const char* assets[] = { "BP_Player", "BP_Coin", "M_ChromeIBL", "M_SlateFloor", "VFX_Fire", "Lvl_MainLevel" };
                    for (const char* a : assets) { ImGui::Button(a, ImVec2(115.0f * s_uiScale, 60.0f * s_uiScale)); ImGui::SameLine(); }
                } else {
                    ImGui::TextColored(ImVec4(0.22f, 0.85f, 1.0f, 1.0f), ">_ Output Log");
                    ImGui::SameLine(screenW - 28.0f * s_uiScale);
                    if (ImGui::SmallButton("X##CloseLog")) { s_showOutputLog = false; }
                    ImGui::Separator();
                    ImGui::BeginChild("OutputLogScroll", ImVec2(0, 0), false);
                    for (const auto& line : core::Logger::getRecentLogs(40)) ImGui::TextUnformatted(line.c_str());
                    ImGui::EndChild();
                }
                ImGui::End();
            }

            if (!s_immersivePlay) {
                ImGui::SetNextWindowPos(ImVec2(0, screenH - bottomStatusH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(screenW, bottomStatusH), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 3));
                ImGui::Begin("BottomStatusRail", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse);
                if (ImGui::Button(" Content Drawer ")) { s_showContentDrawer = !s_showContentDrawer; if (s_showContentDrawer) s_showOutputLog = false; }
                ImGui::SameLine();
                if (ImGui::Button(" >_ Output Log ")) { s_showOutputLog = !s_showOutputLog; if (s_showOutputLog) s_showContentDrawer = false; }
                ImGui::SameLine(screenW - 220.0f * s_uiScale);
                ImGui::TextColored(ImVec4(0.22f, 0.85f, 1.0f, 1.0f), "Vulkan 1.3: %.0f FPS (%ux%u)", ImGui::GetIO().Framerate, m_width, m_height);
                ImGui::End();
                ImGui::PopStyleVar();
            }

            if (s_showScriptStudio) {
                ImGui::SetNextWindowSize(ImVec2(750 * s_uiScale, 520 * s_uiScale), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSizeConstraints(ImVec2(360 * s_uiScale, 240 * s_uiScale), ImVec2(screenW, screenH));
                if (ImGui::Begin("Blueprints / QuickJS Script Studio", &s_showScriptStudio)) {
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

            if (s_engineNotificationTimer > 0.0f) {
                s_engineNotificationTimer -= ImGui::GetIO().DeltaTime;
                ImGui::SetNextWindowPos(ImVec2(screenW * 0.5f - 200.0f * s_uiScale, topTotalH + 12.0f * s_uiScale), ImGuiCond_Always);
                if (ImGui::Begin("EngineNotification", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
                    ImGui::TextColored(ImVec4(0.35f, 0.98f, 0.68f, 1.0f), "%s", s_engineNotification);
                }
                ImGui::End();
            }

            // Render and process interactive splitters
            if (!s_immersivePlay) {
                // 1. Left Vertical Splitter (between Place Actors and Viewport)
                if (s_showPlaceActors) {
                    handleSplitter("##SplitterLeft",
                                   ImVec2(leftPanelW - 2.0f, topTotalH),
                                   ImVec2(4.0f, contentH),
                                   true,
                                   &s_userLeftPanelW,
                                   +1.0f,
                                   140.0f * s_uiScale,
                                   std::max(140.0f * s_uiScale, screenW - rightPanelW - 180.0f * s_uiScale),
                                   260.0f * s_uiScale);
                }

                // 2. Right Vertical Splitter (between Viewport and Outliner/Details)
                if (s_showOutliner || s_showDetails) {
                    handleSplitter("##SplitterRight",
                                   ImVec2(leftPanelW + centerViewportW - 2.0f, topTotalH),
                                   ImVec2(4.0f, contentH),
                                   true,
                                   &s_userRightPanelW,
                                   -1.0f,
                                   180.0f * s_uiScale,
                                   std::max(180.0f * s_uiScale, screenW - leftPanelW - 180.0f * s_uiScale),
                                   310.0f * s_uiScale);
                }

                // 3. Outliner / Details Horizontal Splitter
                if (s_showOutliner && s_showDetails) {
                    handleSplitter("##SplitterOutliner",
                                   ImVec2(leftPanelW + centerViewportW, topTotalH + outlinerH - 2.0f),
                                   ImVec2(rightPanelW, 4.0f),
                                   false,
                                   &s_userOutlinerH,
                                   +1.0f,
                                   70.0f * s_uiScale,
                                   std::max(70.0f * s_uiScale, contentH - 70.0f * s_uiScale),
                                   contentH * 0.42f);
                }

                // 4. Bottom Drawer Horizontal Splitter
                if (s_showContentDrawer || s_showOutputLog) {
                    handleSplitter("##SplitterDrawer",
                                   ImVec2(0.0f, screenH - bottomTotalH - 2.0f),
                                   ImVec2(screenW, 4.0f),
                                   false,
                                   &s_userDrawerH,
                                   -1.0f,
                                   80.0f * s_uiScale,
                                   screenH * 0.70f,
                                   220.0f * s_uiScale);
                }
            }

            s_wasSplitterHoveredLastFrame = s_anySplitterHovered;
        }

        ImGui::Render();

        VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        colorAttachment.imageView = targetView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = m_standaloneGameMode ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = { {0.027f, 0.027f, 0.047f, 1.0f} };

        VkRenderingInfo renderInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        renderInfo.renderArea.extent = m_context->getSwapchainExtent();
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(cmd, &renderInfo);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        vkCmdEndRendering(cmd);
    }

}
#endif
