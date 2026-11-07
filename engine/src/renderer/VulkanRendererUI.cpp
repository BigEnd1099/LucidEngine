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
            0x0020, 0x00FF, // Basic Latin + Latin Supplement
            0x0100, 0x017F, // Latin Extended-A (Polish characters: ąćęłńóśźż)
            0x2000, 0x206F, // General Punctuation
            0x2200, 0x22FF, // Mathematical Operators
            0x25A0, 0x25FF, // Geometric Shapes
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
            case VectorIconType::Play: {
                ImVec2 p0(center.x - r * 0.5f, center.y - r * 0.65f);
                ImVec2 p1(center.x - r * 0.5f, center.y + r * 0.65f);
                ImVec2 p2(center.x + r * 0.75f, center.y);
                dl->AddTriangleFilled(p0, p1, p2, col);
                break;
            }
            case VectorIconType::Pause: {
                dl->AddRectFilled(ImVec2(center.x - r * 0.6f, center.y - r * 0.65f), ImVec2(center.x - r * 0.15f, center.y + r * 0.65f), col, 1.0f);
                dl->AddRectFilled(ImVec2(center.x + r * 0.15f, center.y - r * 0.65f), ImVec2(center.x + r * 0.6f, center.y + r * 0.65f), col, 1.0f);
                break;
            }
            case VectorIconType::Stop: {
                dl->AddRectFilled(ImVec2(center.x - r * 0.6f, center.y - r * 0.6f), ImVec2(center.x + r * 0.6f, center.y + r * 0.6f), col, 2.0f);
                break;
            }
            case VectorIconType::SaveDisk: {
                dl->AddRect(ImVec2(center.x - r * 0.75f, center.y - r * 0.75f), ImVec2(center.x + r * 0.75f, center.y + r * 0.75f), col, 2.0f, 0, 1.4f);
                dl->AddRectFilled(ImVec2(center.x - r * 0.45f, center.y - r * 0.75f), ImVec2(center.x + r * 0.45f, center.y - r * 0.2f), col, 1.0f);
                break;
            }
            case VectorIconType::Gear:
            default: {
                dl->AddCircle(center, r * 0.45f, col, 12, 1.5f);
                for (int i = 0; i < 6; ++i) {
                    float a = i * 1.047197f;
                    dl->AddLine(ImVec2(center.x + cosf(a) * r * 0.45f, center.y + sinf(a) * r * 0.45f),
                                ImVec2(center.x + cosf(a) * r * 0.85f, center.y + sinf(a) * r * 0.85f), col, 2.0f);
                }
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
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s: Przytrzymaj [Ctrl] podczas przeciagania aby przyciagac do siatki (0.5m)", label);
        }
        ImGui::NextColumn();

        const float lineHeight = ImGui::GetFrameHeight();
        const ImVec2 buttonSize = { lineHeight + 2.0f, lineHeight };
        const float availW = ImGui::GetContentRegionAvail().x;
        const float itemWidth = std::max(35.0f, (availW - 3.0f * buttonSize.x - 24.0f) / 3.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));

        // Os X (Karmazynowy)
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
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Os X: Przytrzymaj [Ctrl] podczas przeciagania aby przyciagac do siatki (0.5m)");
        }
        ImGui::SameLine();

        // Os Y (Zielony)
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
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Os Y: Przytrzymaj [Ctrl] podczas przeciagania aby przyciagac do siatki (0.5m)");
        }
        ImGui::SameLine();

        // Os Z (Niebieski)
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
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Os Z: Przytrzymaj [Ctrl] podczas przeciagania aby przyciagac do siatki (0.5m)");
        }

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

            // --- PRZESTRZENIE ROBOCZE (WORKSPACES) ---
            enum class EditorWorkspace {
                SCENE_LAYOUT = 0,    // 0: Układ Sceny i Aktorzy
                PHYSICS_SIM = 1,     // 1: Fizyka Jolt & Zelda TotK Chemia
                MATERIALS_WORLD = 2, // 2: Materiały PBR & Oświetlenie IBL
                SCRIPTING = 3,       // 3: Skrypty QuickJS & Kod
                RENDER_PROFILER = 4  // 4: Sprzętowy RT & Profiler GPU
            };
            static EditorWorkspace s_activeWorkspace = EditorWorkspace::SCENE_LAYOUT;

            // --- KATEGORIE INSPEKTORA (DETAILS TABS) ---
            enum class InspectorCategory {
                TRANSFORM = 0,
                MESH_PBR = 1,
                LIGHT = 2,
                PHYSICS = 3,
                CHEMISTRY = 4,
                SCRIPTS = 5,
                WORLD_IBL = 6,
                POST_PROCESS = 7,
                RENDER_RT = 8
            };
            static int s_inspectorMode = 0; // 0: Obiekt / Encja, 1: Świat & Silnik
            static InspectorCategory s_inspectorCategory = InspectorCategory::TRANSFORM;

            // --- INTERAKTYWNE ROZMIARY PANELI (RESIZABLE SPLITTERS) ---
            static float s_leftPanelW = 270.0f;
            static float s_rightPanelW = 350.0f;
            static float s_outlinerSplitRatio = 0.42f;
            static int s_outlinerFilterCategory = 0; // 0: Wszystko, 1: Siatka, 2: Światło, 3: Fizyka, 4: Gracz/Audio

            const float topTitleBarH  = s_immersivePlay ? 0.0f : (28.0f * s_uiScale);
            const float topLevelTabsH = s_immersivePlay ? 0.0f : (30.0f * s_uiScale);
            const float topToolbarH   = 36.0f * s_uiScale;
            const float topTotalH     = topTitleBarH + topLevelTabsH + topToolbarH;

            const float bottomStatusH = s_immersivePlay ? 0.0f : (24.0f * s_uiScale);
            const float drawerH       = (!s_immersivePlay && (s_showContentDrawer || s_showOutputLog)) ? std::min(220.0f * s_uiScale, screenH * 0.35f) : 0.0f;
            const float bottomTotalH  = bottomStatusH + drawerH;

            const float contentH      = std::max(180.0f, screenH - topTotalH - bottomTotalH);

            s_leftPanelW  = std::clamp(s_leftPanelW, 160.0f * s_uiScale, screenW * 0.40f);
            s_rightPanelW = std::clamp(s_rightPanelW, 240.0f * s_uiScale, screenW * 0.48f);
            s_outlinerSplitRatio = std::clamp(s_outlinerSplitRatio, 0.15f, 0.85f);

            const float leftPanelW      = (!s_immersivePlay && s_showPlaceActors) ? s_leftPanelW : 0.0f;
            const float rightPanelW     = (!s_immersivePlay && (s_showOutliner || s_showDetails)) ? s_rightPanelW : 0.0f;
            const float centerViewportW = std::max(180.0f, screenW - leftPanelW - rightPanelW);

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

            auto saveCurrentGame = [&]() {
                if (engine) {
                    const bool ok = engine->saveCurrentProject("projects/MyGame");
                    snprintf(s_engineNotification, sizeof(s_engineNotification),
                             ok ? "Projekt zapisany w projects/MyGame oraz game.json!" : "Blad zapisu projektu!");
                    s_engineNotificationTimer = 4.0f;
                }
            };

            auto loadCurrentGame = [&](const std::string& p) {
                if (engine) {
                    const bool ok = engine->loadCurrentProject(p);
                    if (ok) selectedEnt = entt::null;
                    snprintf(s_engineNotification, sizeof(s_engineNotification),
                             ok ? "Wczytano projekt: %s" : "Nie udalo sie wczytac projektu: %s", p.c_str());
                    s_engineNotificationTimer = 4.0f;
                }
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

            if (selectedEnt != entt::null && !registry.get().valid(selectedEnt)) {
                selectedEnt = entt::null;
                s_draggingGizmoAxis = 0;
                if (engine) engine->setSelectedEntity(entt::null);
            }
            if (engine && engine->getSelectedEntity() != entt::null && selectedEnt == entt::null) {
                selectedEnt = engine->getSelectedEntity();
            }

            // 1. TOP TITLE / MENU BAR
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
                    tdl->AddCircleFilled(badgeCenter, badgeR, IM_COL32(0, 110, 230, 255));
                    tdl->AddCircleFilled(badgeCenter, badgeR * 0.55f, IM_COL32(255, 255, 255, 230));

                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + badgeR * 2.2f + 6.0f);
                    ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Lucid Studio");
                    ImGui::SameLine(0.0f, 8.0f * s_uiScale);

                    if (ImGui::BeginMenu("Plik")) {
                        if (ImGui::MenuItem("Zapisz projekt", "Ctrl+S")) saveCurrentGame();
                        if (ImGui::MenuItem("Otworz projekt...", "Ctrl+O")) loadCurrentGame("projects/MyGame");
                        ImGui::Separator();
                        if (ImGui::MenuItem("Eksportuj gre (.exe)")) {
                            if (engine) engine->packageStandaloneGame("Build/Windows/MyLucidGame", "MyLucidGame");
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Zamknij studio")) {
                            if (m_context && m_context->getWindow()) glfwSetWindowShouldClose(m_context->getWindow(), GLFW_TRUE);
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Edycja")) {
                        if (ImGui::MenuItem("Cofnij", "Ctrl+Z")) {}
                        if (ImGui::MenuItem("Ponow", "Ctrl+Y")) {}
                        ImGui::Separator();
                        if (ImGui::MenuItem("Zduplikuj zaznaczony", "Ctrl+D")) {}
                        if (ImGui::MenuItem("Usun obiekt", "Delete")) {
                            if (selectedEnt != entt::null && registry.get().valid(selectedEnt)) {
                                registry.get().destroy(selectedEnt);
                                selectedEnt = entt::null;
                                if (engine) engine->setSelectedEntity(entt::null);
                            }
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Widok")) {
                        ImGui::MenuItem("Biblioteka aktorow", nullptr, &s_showPlaceActors);
                        ImGui::MenuItem("Hierarchia sceny (Outliner)", nullptr, &s_showOutliner);
                        ImGui::MenuItem("Panel wlasciwosci (Details)", nullptr, &s_showDetails);
                        ImGui::MenuItem("Dziennik zdarzen (Log)", nullptr, &s_showOutputLog);
                        ImGui::Separator();
                        ImGui::MenuItem("Pelny ekran (Tryb immersyjny)", "F11", &s_immersivePlay);
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Grafika")) {
                        if (ImGui::BeginMenu("Anti-Aliasing (FXAA)")) {
                            if (ImGui::MenuItem("Wylaczone", nullptr, m_antiAliasingMode == 0)) m_antiAliasingMode = 0;
                            if (ImGui::MenuItem("FXAA Fast", nullptr, m_antiAliasingMode == 1)) m_antiAliasingMode = 1;
                            if (ImGui::MenuItem("FXAA Quality", nullptr, m_antiAliasingMode == 2)) m_antiAliasingMode = 2;
                            if (ImGui::MenuItem("FXAA Ultra", nullptr, m_antiAliasingMode == 3)) m_antiAliasingMode = 3;
                            ImGui::EndMenu();
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Narzedzia")) {
                        if (ImGui::MenuItem("Studio Skryptow QuickJS (F4)")) s_showScriptStudio = true;
                        if (ImGui::MenuItem("Eksportuj TypeScript (engine.d.ts)") && engine) engine->exportTypeDefinitions();
                        ImGui::EndMenu();
                    }

                    const float projOffset = screenW * 0.55f;
                    if (ImGui::GetCursorPosX() < projOffset) ImGui::SameLine(projOffset);
                    ImGui::TextDisabled("Vulkan 1.3 | Cubemap IBL + ACES HDR");
                    ImGui::EndMenuBar();
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar();

                // 2. WORKSPACE SELECTION BAR (Blender/Unreal Mode Bar)
                ImGui::SetNextWindowPos(ImVec2(0, topTitleBarH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(screenW, topLevelTabsH), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.043f, 0.067f, 1.0f));
                ImGui::Begin("WorkspaceSelectorBar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);

                // Etykieta aktywnego poziomu
                ImVec2 tabStart = ImGui::GetCursorScreenPos();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.086f, 0.086f, 0.125f, 1.0f));
                const std::string curPreset = engine ? engine->getCurrentPreset() : "empty";
                if (ImGui::Button("    Scena: CzystyPoziom ", ImVec2(0, topLevelTabsH - 4.0f)) && engine) {
                    selectedEnt = entt::null;
                    engine->resetScene(curPreset);
                }
                ImGui::PopStyleColor();
                ImVec2 tabEnd = ImGui::GetItemRectMax();
                drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(tabStart.x + 11.0f * s_uiScale, (tabStart.y + tabEnd.y) * 0.5f), 5.0f * s_uiScale, VectorIconType::Folder, IM_COL32(245, 158, 11, 255));

                ImGui::SameLine(0.0f, 16.0f * s_uiScale);

                // Przyciski przestrzeni roboczych (Workspaces)
                auto drawWorkspaceTab = [&](const char* title, EditorWorkspace ws) {
                    const bool active = (s_activeWorkspace == ws);
                    if (active) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.44f, 0.95f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.07f, 0.075f, 0.10f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.76f, 0.85f, 1.0f));
                    }
                    if (ImGui::Button(title, ImVec2(0, topLevelTabsH - 4.0f))) {
                        s_activeWorkspace = ws;
                        if (ws == EditorWorkspace::PHYSICS_SIM) {
                            s_inspectorMode = 0;
                            s_inspectorCategory = InspectorCategory::PHYSICS;
                        } else if (ws == EditorWorkspace::MATERIALS_WORLD) {
                            s_inspectorMode = 1;
                            s_inspectorCategory = InspectorCategory::WORLD_IBL;
                        } else if (ws == EditorWorkspace::SCRIPTING) {
                            s_showScriptStudio = true;
                            s_inspectorMode = 0;
                            s_inspectorCategory = InspectorCategory::SCRIPTS;
                        } else if (ws == EditorWorkspace::RENDER_PROFILER) {
                            s_inspectorMode = 1;
                            s_inspectorCategory = InspectorCategory::RENDER_RT;
                        } else if (ws == EditorWorkspace::SCENE_LAYOUT) {
                            s_inspectorMode = 0;
                            s_inspectorCategory = InspectorCategory::TRANSFORM;
                        }
                    }
                    ImGui::PopStyleColor(2);
                    ImGui::SameLine(0.0f, 4.0f * s_uiScale);
                };

                drawWorkspaceTab("  Scena 3D  ", EditorWorkspace::SCENE_LAYOUT);
                drawWorkspaceTab("  Fizyka & Symulacja  ", EditorWorkspace::PHYSICS_SIM);
                drawWorkspaceTab("  Materialy & Swiat  ", EditorWorkspace::MATERIALS_WORLD);
                drawWorkspaceTab("  Skrypty QuickJS  ", EditorWorkspace::SCRIPTING);
                drawWorkspaceTab("  Render & Profiler  ", EditorWorkspace::RENDER_PROFILER);

                ImGui::End();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }

            // 3. MAIN TOOLBAR (Transport Dock, Narzedzia projektu i perspektywa)
            ImGui::SetNextWindowPos(ImVec2(0, topTitleBarH + topLevelTabsH), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(screenW, topToolbarH), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 5));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.055f, 0.082f, 1.0f));
            ImGui::Begin("MainToolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);

            const float btnH = topToolbarH - 10.0f;

            // Zapisz projekt
            ImVec2 sPos = ImGui::GetCursorScreenPos();
            if (ImGui::Button("     Zapisz ", ImVec2(0, btnH))) saveCurrentGame();
            drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(sPos.x + 11.0f * s_uiScale, sPos.y + btnH * 0.5f), 5.5f * s_uiScale, VectorIconType::SaveDisk, IM_COL32(245, 158, 11, 255));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zapisz biezacy stan projektu [Ctrl+S]");
            ImGui::SameLine();

            // Studio Skryptow
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.72f, 1.0f, 1.0f));
            if (ImGui::Button(" </> Blueprints ", ImVec2(0, btnH))) s_showScriptStudio = !s_showScriptStudio;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Otworz QuickJS ESM Live Coding & Skrypty (F4)");
            ImGui::SameLine();

            // Szybkie ustawienia oswietlenia i renderera
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.32f, 1.0f));
            ImVec2 gPos = ImGui::GetCursorScreenPos();
            if (ImGui::Button("     Swiatlo & Grafika ", ImVec2(0, btnH))) {
                s_inspectorMode = 1;
                s_inspectorCategory = InspectorCategory::WORLD_IBL;
                s_showDetails = true;
            }
            drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(gPos.x + 11.0f * s_uiScale, gPos.y + btnH * 0.5f), 5.5f * s_uiScale, VectorIconType::Gear, IM_COL32(251, 191, 36, 255));
            ImGui::PopStyleColor();
            ImGui::SameLine();

            // Transport Dock (Play / Pause / Stop / Step)
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
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wykonaj krok symulacji o dokladnie 1 klatke (1/60s)");

            // Tryb rzutu kamery
            ImGui::SameLine(screenW - 170.0f * s_uiScale);
            static int s_camMode = 0;
            const char* camModes[] = { "Perspektywa", "Ortogonalna" };
            ImGui::SetNextItemWidth(155.0f * s_uiScale);
            ImGui::Combo("##CameraModeCombo", &s_camMode, camModes, IM_ARRAYSIZE(camModes));

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            // 4. LEWY PANEL: BIBLIOTEKA AKTOROW (Zwijany i skalowalny)
            const float toolDockW = 40.0f * s_uiScale;
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
                        if (i == 5) {
                            s_inspectorMode = 1;
                            s_inspectorCategory = InspectorCategory::WORLD_IBL;
                            s_showDetails = true;
                        }
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

                ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Biblioteka Aktorow");
                ImGui::SameLine(ImGui::GetWindowWidth() - 30.0f * s_uiScale);
                if (ImGui::SmallButton("<##CollapseLeft")) s_showPlaceActors = false;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zwin lewy panel");

                static char classSearch[64] = "";
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##SearchClasses", "Szukaj szablonu...", classSearch, sizeof(classSearch));
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
                            ecs::MaterialComponent mat; mat.baseColor = glm::vec4(0.2f, 0.8f, 0.4f, 1.0f);
                            registry.get().emplace<ecs::MaterialComponent>(entity, mat);
                            registry.get().emplace<ecs::CharacterMovementComponent>(entity, ecs::CharacterMovementComponent{});
                        } else if (spawnType == 3) {
                            registry.get().emplace<ecs::TagComponent>(entity, ecs::TagComponent{"PointLight"});
                            registry.get().emplace<ecs::TransformComponent>(entity, spawnPos + glm::vec3(0.0f, 2.0f, 0.0f));
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

            // 5. VIEWPORT 3D (W centrum z interaktywnymi nakladkami i splitterami)
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
                if (ctrlDown && ImGui::IsKeyPressed(ImGuiKey_S)) saveCurrentGame();
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

            // Obsluga nawigacji kamery w Viewporcie (RMB / MMB / Alt)
            if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                s_isRmbNavigating = true;
            }
            if (s_isRmbNavigating) {
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    s_isRmbNavigating = false;
                } else {
                    ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
                    camYaw   += mouseDelta.x * 0.18f;
                    camPitch -= mouseDelta.y * 0.18f;
                    camPitch = std::clamp(camPitch, -89.0f, 89.0f);

                    float speed = s_camFlySpeed * ImGui::GetIO().DeltaTime * (s_cameraSpeed * 0.5f);
                    if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) speed *= 3.0f;

                    if (ImGui::IsKeyDown(ImGuiKey_W)) camPos += front * speed;
                    if (ImGui::IsKeyDown(ImGuiKey_S)) camPos -= front * speed;
                    if (ImGui::IsKeyDown(ImGuiKey_D)) camPos += right * speed;
                    if (ImGui::IsKeyDown(ImGuiKey_A)) camPos -= right * speed;
                    if (ImGui::IsKeyDown(ImGuiKey_E)) camPos += glm::vec3(0.0f, 1.0f, 0.0f) * speed;
                    if (ImGui::IsKeyDown(ImGuiKey_Q)) camPos -= glm::vec3(0.0f, 1.0f, 0.0f) * speed;

                    if (engine) engine->setCamera(camPos, camYaw, camPitch, camFov);
                }
            }

            // Selekcja promieniem (Raycast) przy kliknieciu lewym przyciskiem
            if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && s_draggingGizmoAxis == 0) {
                if (hoveredLightEntity != entt::null) {
                    selectedEnt = hoveredLightEntity;
                    if (engine) engine->setSelectedEntity(selectedEnt);
                } else {
                    glm::vec3 rOrig, rDir;
                    getRayFromScreen(mousePos, rOrig, rDir);

                    entt::entity bestHitEnt = entt::null;
                    float bestDist = 1e9f;

                    auto viewEntities = registry.get().view<ecs::TransformComponent, ecs::MeshComponent>();
                    for (auto ent : viewEntities) {
                        const auto& t = viewEntities.get<ecs::TransformComponent>(ent);
                        const float radius = std::max(t.scale.x, std::max(t.scale.y, t.scale.z)) * 0.75f;
                        glm::vec3 m = rOrig - t.position;
                        float b = glm::dot(m, rDir);
                        float c = glm::dot(m, m) - radius * radius;
                        if (c <= 0.0f || b <= 0.0f) {
                            float discr = b * b - c;
                            if (discr >= 0.0f) {
                                float tHit = -b - sqrtf(discr);
                                if (tHit > 0.0f && tHit < bestDist) {
                                    bestDist = tHit;
                                    bestHitEnt = ent;
                                }
                            }
                        }
                    }
                    if (bestHitEnt != entt::null) {
                        selectedEnt = bestHitEnt;
                        if (engine) engine->setSelectedEntity(selectedEnt);
                    }
                }
            }

            // Rysowanie Gizmo 3D dla zaznaczonego obiektu
            int hoveredGizmoAxis = 0;
            if (selectedEnt != entt::null && registry.get().valid(selectedEnt) && registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                ImVec2 originScreen;
                float originW = 0.0f;
                if (worldToScreen(t.position, originScreen, originW) && originW > 0.05f) {
                    const float gizmoLen = 65.0f * s_uiScale;
                    ImVec2 ptX, ptY, ptZ;
                    float wX = 0.0f, wY = 0.0f, wZ = 0.0f;
                    worldToScreen(t.position + glm::vec3(1.5f, 0.0f, 0.0f), ptX, wX);
                    worldToScreen(t.position + glm::vec3(0.0f, 1.5f, 0.0f), ptY, wY);
                    worldToScreen(t.position + glm::vec3(0.0f, 0.0f, 1.5f), ptZ, wZ);

                    const ImU32 colX = (s_draggingGizmoAxis == 1) ? IM_COL32(255, 255, 60, 255) : IM_COL32(239, 68, 68, 255);
                    const ImU32 colY = (s_draggingGizmoAxis == 2) ? IM_COL32(255, 255, 60, 255) : IM_COL32(34, 197, 94, 255);
                    const ImU32 colZ = (s_draggingGizmoAxis == 3) ? IM_COL32(255, 255, 60, 255) : IM_COL32(59, 130, 246, 255);

                    drawList->AddLine(originScreen, ptX, colX, (s_draggingGizmoAxis == 1) ? 4.0f : 2.8f);
                    drawList->AddLine(originScreen, ptY, colY, (s_draggingGizmoAxis == 2) ? 4.0f : 2.8f);
                    drawList->AddLine(originScreen, ptZ, colZ, (s_draggingGizmoAxis == 3) ? 4.0f : 2.8f);
                    drawList->AddCircleFilled(originScreen, 5.0f, IM_COL32(255, 255, 255, 240));
                }
            }

            if (!s_immersivePlay) {
                // Nakladka lewa gorna: Tryb Gizmo, Snap i Szybkosc kamery
                ImGui::SetCursorPos(ImVec2(10.0f * s_uiScale, 10.0f * s_uiScale));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.055f, 0.086f, 0.90f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
                ImGui::BeginChild("VpOverlayTopLeft", ImVec2(340.0f * s_uiScale, 30.0f * s_uiScale), true, ImGuiWindowFlags_NoScrollbar);
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
                if (ImGui::Button((" Cam " + std::to_string(s_cameraSpeed) + "x ").c_str(), ImVec2(0, 22.0f * s_uiScale))) s_cameraSpeed = (s_cameraSpeed % 8) + 1;
                ImGui::SameLine();
                if (ImGui::Button(" Przod ", ImVec2(0, 22.0f * s_uiScale)) && engine) {
                    engine->setCamera(glm::vec3(0.0f, 5.0f, 25.0f), -90.0f, 0.0f, camFov);
                }
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();

                // Nakladka prawa gorna: Telemetria i format bufora
                const float trPillW = 320.0f * s_uiScale;
                ImGui::SetCursorPos(ImVec2(std::max(10.0f, viewportPanelSize.x - trPillW - 10.0f * s_uiScale), 10.0f * s_uiScale));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.055f, 0.086f, 0.90f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
                ImGui::BeginChild("VpOverlayTopRight", ImVec2(trPillW, 30.0f * s_uiScale), true, ImGuiWindowFlags_NoScrollbar);
                ImGui::TextColored(ImVec4(0.22f, 0.85f, 1.0f, 1.0f), "PBR + IBL");
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.20f, 0.92f, 0.58f, 1.0f), "%ux%u (EV %.2f)", m_width, m_height, m_exposure);
                ImGui::SameLine();
                if (!s_showDetails) {
                    if (ImGui::SmallButton("< Rozwin")) s_showDetails = true;
                }
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            // 6. OUTLINER PANEL (Hierarchia Sceny z filtrami kategorialnymi)
            const float outlinerH = s_showDetails ? (contentH * s_outlinerSplitRatio) : contentH;
            if (!s_immersivePlay && s_showOutliner) {
                ImGui::SetNextWindowPos(ImVec2(leftPanelW + centerViewportW, topTotalH), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(rightPanelW, outlinerH), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.055f, 0.086f, 1.0f));
                ImGui::Begin("OutlinerPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

                // Naglowek panelu z tytulem i przyciskiem zwijania
                ImDrawList* odl = ImGui::GetWindowDrawList();
                ImVec2 oHdrPos = ImGui::GetCursorScreenPos();
                drawVectorIcon(odl, ImVec2(oHdrPos.x + 8.0f * s_uiScale, oHdrPos.y + 9.0f * s_uiScale), 5.5f * s_uiScale, VectorIconType::Folder, IM_COL32(245, 158, 11, 255));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f * s_uiScale);
                ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Hierarchia Sceny");

                auto view = registry.get().view<ecs::TransformComponent>();
                size_t totalEnts = view.size();
                ImGui::SameLine(ImGui::GetWindowWidth() - 105.0f * s_uiScale);
                ImGui::TextDisabled("[%zu encji]", totalEnts);
                ImGui::SameLine();
                if (ImGui::SmallButton(">##CollapseRight")) { s_showOutliner = false; s_showDetails = false; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zwin prawy panel");

                // Filtry kategorialne (Wszystko / Siatki / Swiatla / Fizyka / Gracz)
                auto filterChip = [&](const char* name, int catIdx) {
                    const bool active = (s_outlinerFilterCategory == catIdx);
                    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.44f, 0.95f, 1.0f));
                    else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
                    if (ImGui::SmallButton(name)) s_outlinerFilterCategory = catIdx;
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0.0f, 3.0f * s_uiScale);
                };
                filterChip("Wszystko", 0);
                filterChip("Siatki", 1);
                filterChip("Swiatla", 2);
                filterChip("Fizyka", 3);
                filterChip("Gracz", 4);
                ImGui::NewLine();

                // Wyszukiwarka i przycisk spawnowania
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

                // Lista encji z rozpoznawaniem typow i filtrowaniem
                ImGui::BeginChild("OutlinerTreeScroll", ImVec2(0, 0), false);
                for (auto entity : view) {
                    const bool isLight = registry.get().all_of<ecs::LightComponent>(entity);
                    const bool isPlayer = registry.get().all_of<ecs::CharacterMovementComponent>(entity) || registry.get().all_of<ecs::AudioComponent>(entity);
                    const bool isPhys = registry.get().all_of<ecs::RigidBodyComponent>(entity) || registry.get().all_of<ecs::DestructibleComponent>(entity);
                    const bool isMesh = registry.get().all_of<ecs::MeshComponent>(entity);

                    if (s_outlinerFilterCategory == 1 && !isMesh) continue;
                    if (s_outlinerFilterCategory == 2 && !isLight) continue;
                    if (s_outlinerFilterCategory == 3 && !isPhys) continue;
                    if (s_outlinerFilterCategory == 4 && !isPlayer) continue;

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

                    VectorIconType eIcon = VectorIconType::Cube;
                    ImU32 eCol = IM_COL32(96, 165, 250, 240);
                    const char* typeBadge = "Mesh";

                    if (isLight) {
                        eIcon = VectorIconType::Light;
                        eCol = IM_COL32(250, 204, 21, 255);
                        typeBadge = "Light";
                    } else if (registry.get().all_of<ecs::AudioComponent>(entity)) {
                        eIcon = VectorIconType::Speaker;
                        eCol = IM_COL32(251, 146, 60, 255);
                        typeBadge = "Audio";
                    } else if (registry.get().all_of<ecs::CharacterMovementComponent>(entity)) {
                        eIcon = VectorIconType::Character;
                        eCol = IM_COL32(52, 211, 153, 255);
                        typeBadge = "Player";
                    } else if (registry.get().all_of<ecs::DestructibleComponent>(entity)) {
                        eIcon = VectorIconType::WarningTri;
                        eCol = IM_COL32(249, 115, 22, 255);
                        typeBadge = "Destr";
                    } else if (isPhys) {
                        eIcon = VectorIconType::Cube;
                        eCol = IM_COL32(147, 197, 253, 255);
                        typeBadge = "Phys";
                    }

                    ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity)));
                    ImVec2 rowPos = ImGui::GetCursorScreenPos();
                    const float rowH = 22.0f * s_uiScale;

                    drawVectorIcon(ImGui::GetWindowDrawList(), ImVec2(rowPos.x + 10.0f * s_uiScale, rowPos.y + rowH * 0.5f), 5.5f * s_uiScale, eIcon, eCol);

                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0f * s_uiScale);
                    const std::string selectableText = label + "##Row";
                    if (ImGui::Selectable(selectableText.c_str(), isSelected, 0, ImVec2(ImGui::GetContentRegionAvail().x - 48.0f * s_uiScale, rowH))) {
                        selectedEnt = entity;
                        if (engine) engine->setSelectedEntity(selectedEnt);
                    }

                    // Menu kontekstowe pod prawym przyciskiem myszy
                    if (ImGui::BeginPopupContextItem("EntityCtxMenu")) {
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

                    // Etykieta typu encji po prawej stronie wiersza
                    ImGui::SameLine(ImGui::GetWindowWidth() - 44.0f * s_uiScale);
                    ImGui::TextDisabled("[%s]", typeBadge);

                    ImGui::PopID();
                }
                ImGui::EndChild();

                ImGui::End();
                ImGui::PopStyleColor();
            }

            // 7. DETAILS PANEL: KATEGORYZOWANY INSPEKTOR (Zamiast jednego wielkiego worka!)
            if (!s_immersivePlay && s_showDetails) {
                const float detailsY = topTotalH + (s_showOutliner ? outlinerH : 0.0f);
                const float detailsH = s_showOutliner ? (contentH - outlinerH) : contentH;
                ImGui::SetNextWindowPos(ImVec2(leftPanelW + centerViewportW, detailsY), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(rightPanelW, detailsH), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.055f, 0.086f, 1.0f));
                ImGui::Begin("DetailsPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

                // Przelacznik kontekstu: Obiekt vs Swiat
                const float halfW = (ImGui::GetContentRegionAvail().x - 6.0f * s_uiScale) * 0.5f;
                auto drawContextBtn = [&](const char* label, int mode) {
                    const bool active = (s_inspectorMode == mode);
                    if (active) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.44f, 0.95f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.68f, 0.72f, 0.82f, 1.0f));
                    }
                    if (ImGui::Button(label, ImVec2(halfW, 23.0f * s_uiScale))) {
                        s_inspectorMode = mode;
                    }
                    ImGui::PopStyleColor(2);
                };
                drawContextBtn("Obiekt / Encja", 0);
                ImGui::SameLine(0.0f, 4.0f * s_uiScale);
                drawContextBtn("Swiat & Grafika", 1);
                ImGui::Separator();

                // Pasek podkategorii (Tabs)
                auto drawCategoryChip = [&](const char* title, InspectorCategory cat) {
                    const bool active = (s_inspectorCategory == cat);
                    if (active) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.52f, 0.95f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.09f, 0.10f, 0.14f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.74f, 0.84f, 1.0f));
                    }
                    if (ImGui::SmallButton(title)) s_inspectorCategory = cat;
                    ImGui::PopStyleColor(2);
                    ImGui::SameLine(0.0f, 3.0f * s_uiScale);
                };

                if (s_inspectorMode == 0) {
                    // Tryb edycji zaznaczonego obiektu
                    drawCategoryChip("Transform", InspectorCategory::TRANSFORM);
                    drawCategoryChip("Siatka/PBR", InspectorCategory::MESH_PBR);
                    drawCategoryChip("Swiatlo", InspectorCategory::LIGHT);
                    drawCategoryChip("Fizyka", InspectorCategory::PHYSICS);
                    drawCategoryChip("Chemia", InspectorCategory::CHEMISTRY);
                    drawCategoryChip("Skrypt", InspectorCategory::SCRIPTS);
                    ImGui::NewLine();
                    ImGui::Separator();

                    if (selectedEnt == entt::null || !registry.get().valid(selectedEnt)) {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Nie zaznaczono zadnego obiektu.");
                        ImGui::TextDisabled("Kliknij obiekt w Viewporcie lub wybierz z Outlinera powyzej.");
                    } else {
                        std::string tagStr = registry.get().all_of<ecs::TagComponent>(selectedEnt)
                            ? registry.get().get<ecs::TagComponent>(selectedEnt).name
                            : "Actor";

                        // 1. KATEGORIA: TRANSFORM
                        if (s_inspectorCategory == InspectorCategory::TRANSFORM) {
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
                                const bool pCh = drawVec3Control("Pozycja (m)", t.position, 0.0f);
                                const bool rCh = drawVec3Control("Rotacja (deg)", t.rotation, 0.0f);
                                const bool sCh = drawVec3Control("Skala", t.scale, 1.0f);
                                if (rCh) t.orientation = glm::quat(glm::radians(t.rotation));
                                if ((pCh || rCh || sCh) && engine) engine->syncEntityTransformPhysics(selectedEnt);

                                ImGui::Spacing();
                                ImGui::Separator();
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

                        // 2. KATEGORIA: SIATKA & PBR
                        else if (s_inspectorCategory == InspectorCategory::MESH_PBR) {
                            ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Siatka Geometrii & Material PBR");
                            ImGui::Separator();
                            if (registry.get().all_of<ecs::MeshComponent>(selectedEnt)) {
                                auto& mc = registry.get().get<ecs::MeshComponent>(selectedEnt);
                                const char* meshes[] = { "Brak", "Plaszczyzna (Ground)", "Szescian (1x1x1)", "Kula (R=0.5)", "Walec", "Kapsula" };
                                int curMesh = std::clamp(static_cast<int>(mc.meshId), 0, 5);
                                if (ImGui::Combo("Model Siatki", &curMesh, meshes, IM_ARRAYSIZE(meshes))) {
                                    mc.meshId = static_cast<uint32_t>(curMesh);
                                }
                            } else {
                                if (ImGui::Button("+ Dodaj komponent Siatki (Mesh)")) {
                                    registry.get().emplace<ecs::MeshComponent>(selectedEnt, ecs::MeshComponent{2});
                                }
                            }

                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), "Parametry Materialu PBR (GGX)");
                            if (!registry.get().all_of<ecs::MaterialComponent>(selectedEnt)) {
                                if (ImGui::Button("+ Utworz wlasny Material PBR")) {
                                    ecs::MaterialComponent m{};
                                    m.baseColor = glm::vec4(0.85f, 0.85f, 0.90f, 1.0f);
                                    m.metallic = 0.0f;
                                    m.roughness = 0.35f;
                                    m.emissiveIntensity = 0.0f;
                                    registry.get().emplace<ecs::MaterialComponent>(selectedEnt, m);
                                }
                            } else {
                                auto& mat = registry.get().get<ecs::MaterialComponent>(selectedEnt);
                                ImGui::ColorEdit4("Kolor Albedo", &mat.baseColor.x);
                                ImGui::SliderFloat("Metaliczność", &mat.metallic, 0.0f, 1.0f, "%.2f");
                                ImGui::SliderFloat("Chropowatość", &mat.roughness, 0.045f, 1.0f, "%.3f");
                                ImGui::DragFloat("Emisja HDR", &mat.emissiveIntensity, 0.05f, 0.0f, 25.0f, "%.2f");

                                ImGui::Spacing();
                                ImGui::TextDisabled("Szablony PBR:");
                                if (ImGui::SmallButton("Chrom/Lustro")) { mat.metallic = 1.0f; mat.roughness = 0.05f; }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Zloto")) { mat.baseColor = glm::vec4(1.0f, 0.76f, 0.33f, 1.0f); mat.metallic = 1.0f; mat.roughness = 0.15f; }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Plastik")) { mat.metallic = 0.0f; mat.roughness = 0.35f; }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Neon")) { mat.emissiveIntensity = 12.0f; }
                            }
                        }

                        // 3. KATEGORIA: SWIATLO (PBR LIGHT)
                        else if (s_inspectorCategory == InspectorCategory::LIGHT) {
                            ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Zrodlo Swiatla PBR");
                            ImGui::Separator();
                            if (registry.get().all_of<ecs::LightComponent>(selectedEnt)) {
                                auto& lc = registry.get().get<ecs::LightComponent>(selectedEnt);
                                const char* lightTypes[] = { "point", "sun", "spot", "area" };
                                int curType = (lc.type == "directional" || lc.type == "sun") ? 1 : (lc.type == "spot" ? 2 : (lc.type == "area" ? 3 : 0));
                                if (ImGui::Combo("Typ Swiatla", &curType, "Punktowe (Point)\0Kierunkowe (Slonce)\0Reflektor (Spot)\0Obszarowe (Area)\0\0")) {
                                    lc.type = lightTypes[curType];
                                }
                                ImGui::ColorEdit3("Kolor", (float*)&lc.color);
                                ImGui::DragFloat("Jasność / Moc", &lc.intensity, 0.1f, 0.0f, 100.0f, "%.2f");
                                ImGui::DragFloat("Zasięg (m)", &lc.range, 0.5f, 1.0f, 200.0f, "%.1f m");

                                if (curType == 2) {
                                    ImGui::SliderFloat("Kąt stożka", &lc.outerConeAngle, 5.0f, 85.0f, "%.1f deg");
                                }
                            } else {
                                ImGui::TextDisabled("Ten obiekt nie emituje swiatla.");
                                if (ImGui::Button("+ Dodaj komponent Swiatla (PBR Light)")) {
                                    registry.get().emplace<ecs::LightComponent>(selectedEnt, ecs::LightComponent{"point", glm::vec3(1.0f), 5.0f, 20.0f});
                                }
                            }
                        }

                        // 4. KATEGORIA: FIZYKA JOLT
                        else if (s_inspectorCategory == InspectorCategory::PHYSICS) {
                            ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Fizyka Jolt (RigidBody)");
                            ImGui::Separator();
                            if (registry.get().all_of<ecs::RigidBodyComponent>(selectedEnt)) {
                                auto& rb = registry.get().get<ecs::RigidBodyComponent>(selectedEnt);
                                const char* motionTypes[] = { "Dynamiczny (Symulowany)", "Statyczny (Nieruchomy)", "Kinematyczny" };
                                int mType = (rb.mass <= 0.0f) ? 1 : 0;
                                if (ImGui::Combo("Tryb Ruchu", &mType, motionTypes, IM_ARRAYSIZE(motionTypes))) {
                                    rb.mass = (mType == 1) ? 0.0f : 1.0f;
                                }
                                if (mType == 0) {
                                    ImGui::DragFloat("Masa (kg)", &rb.mass, 0.1f, 0.01f, 1000.0f, "%.2f kg");
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("1kg")) rb.mass = 1.0f;
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("10kg")) rb.mass = 10.0f;
                                }
                                ImGui::SliderFloat("Sprezystosc (Restitution)", &rb.restitution, 0.0f, 1.0f, "%.2f");
                                ImGui::SliderFloat("Tarcie (Friction)", &rb.friction, 0.0f, 1.0f, "%.2f");
                                ImGui::Checkbox("Jest czujnikiem (Sensor/Trigger)", &rb.isSensor);
                                ImGui::Checkbox("Blokada obrotu X", &rb.lockRotationX);
                                ImGui::SameLine();
                                ImGui::Checkbox("Blokada obrotu Z", &rb.lockRotationZ);
                            } else {
                                ImGui::TextDisabled("Ten obiekt nie bierze udzialu w symulacji fizyki.");
                                if (ImGui::Button("+ Dodaj Cialo Sztywne Jolt")) {
                                    ecs::RigidBodyComponent rb{};
                                    rb.mass = 1.0f;
                                    rb.shapeType = 0;
                                    registry.get().emplace<ecs::RigidBodyComponent>(selectedEnt, rb);
                                }
                            }
                        }

                        // 5. KATEGORIA: CHEMIA / ZYWIOŁY (ZELDA TOTK)
                        else if (s_inspectorCategory == InspectorCategory::CHEMISTRY) {
                            ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Reakcje Chemiczne & Żywioły (Zelda TotK)");
                            ImGui::Separator();
                            static float s_elemTemp = 20.0f;
                            static float s_elemWet = 0.0f;
                            static float s_elemConduct = 0.5f;

                            ImGui::SliderFloat("Temperatura (°C)", &s_elemTemp, -50.0f, 600.0f, "%.1f °C");
                            if (s_elemTemp > 400.0f) {
                                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.1f, 1.0f), "[STATUS: OBIEKT PŁONIE]");
                            } else if (s_elemTemp < 0.0f) {
                                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[STATUS: ZAMROŻONY]");
                            } else {
                                ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "[STATUS: NORMALNY]");
                            }

                            ImGui::SliderFloat("Wilgotność (Woda)", &s_elemWet, 0.0f, 1.0f, "%.2f");
                            ImGui::SliderFloat("Przewodnictwo", &s_elemConduct, 0.0f, 1.0f, "%.2f");

                            ImGui::Spacing();
                            ImGui::TextDisabled("Interakcje na zywo:");
                            if (ImGui::Button(" Podpal (450°C) ")) s_elemTemp = 450.0f;
                            ImGui::SameLine();
                            if (ImGui::Button(" Ugas woda ")) { s_elemTemp = 20.0f; s_elemWet = 1.0f; }
                            ImGui::SameLine();
                            if (ImGui::Button(" Zamroz (-20°C) ")) s_elemTemp = -20.0f;
                        }

                        // 6. KATEGORIA: SKRYPTY QUICKJS
                        else if (s_inspectorCategory == InspectorCategory::SCRIPTS) {
                            ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Skrypty QuickJS & Rozgrywka");
                            ImGui::Separator();
                            static char scriptBindingPath[128] = "scripts/gameplay.js";
                            ImGui::InputText("Plik Skryptu", scriptBindingPath, sizeof(scriptBindingPath));
                            ImGui::TextColored(ImVec4(0.25f, 0.95f, 0.55f, 1.0f), "Status: ESM Modul Gotowy");

                            ImGui::Spacing();
                            if (ImGui::Button(" Przeprowadz Hot-Reload Skryptu ")) {
                                if (engine) engine->loadScript(engine->getCurrentScript());
                            }
                            if (ImGui::Button(" Otworz Pelne Studio Skryptow (F4) ")) {
                                s_showScriptStudio = true;
                            }
                        }
                    }
                } else {
                    // Tryb edycji globalnego swiata, oswietlenia i renderera
                    drawCategoryChip("Swiat & IBL", InspectorCategory::WORLD_IBL);
                    drawCategoryChip("Postprocesy", InspectorCategory::POST_PROCESS);
                    drawCategoryChip("Ray Tracing & Profiler", InspectorCategory::RENDER_RT);
                    ImGui::NewLine();
                    ImGui::Separator();

                    // G1. SWIAT & IBL
                    if (s_inspectorCategory == InspectorCategory::WORLD_IBL) {
                        ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Otoczenie, Niebo HDRI & Slonce");
                        ImGui::Separator();

                        const char* hdriPresets[] = { "Sunset (Zachod)", "Clear Sky (Czyste niebo)", "City (Miasto)", "Courtyard (Dziedziniec)", "Forest (Las)", "Interior (Wnetrze)", "Night (Noc)", "Sunrise (Wschod)" };
                        static int curHdri = 0;
                        if (ImGui::Combo("Preset Nieba HDRI", &curHdri, hdriPresets, IM_ARRAYSIZE(hdriPresets))) {
                            if (engine) {
                                auto env = engine->getEnvironmentSettings();
                                switch (curHdri) {
                                    case 0: env.sunColor = glm::vec3(1.0f, 0.78f, 0.45f); env.sunIntensity = 3.5f; break;
                                    case 1: env.sunColor = glm::vec3(1.0f, 1.0f, 0.95f); env.sunIntensity = 2.5f; break;
                                    case 2: env.sunColor = glm::vec3(1.0f, 0.96f, 0.88f); env.sunIntensity = 3.2f; break;
                                    case 4: env.sunColor = glm::vec3(1.0f, 0.94f, 0.82f); env.sunIntensity = 3.0f; break;
                                    case 6: env.sunColor = glm::vec3(0.65f, 0.80f, 1.0f); env.sunIntensity = 0.8f; break;
                                    default: break;
                                }
                                engine->setEnvironmentSettings(env);
                            }
                        }

                        ImGui::InputTextWithHint("##CustomHdri", "Wlasny plik .exr/.hdr...", m_customHdriPath, sizeof(m_customHdriPath));
                        ImGui::SameLine();
                        if (ImGui::Button("Wczytaj")) {
                            if (m_customHdriPath[0] != '\0') loadEnvironmentHdriFile(m_customHdriPath);
                        }

                        ImGui::Checkbox("Renderuj Skybox & Tarcze Sloneczna", &m_skyboxEnabled);
                        ImGui::Checkbox("Oswietlenie IBL (Image-Based Lighting)", &m_iblEnabled);

                        if (engine) {
                            auto env = engine->getEnvironmentSettings();
                            if (ImGui::DragFloat("Moc Slonca (Sun Intensity)", &env.sunIntensity, 0.1f, 0.0f, 20.0f)) engine->setEnvironmentSettings(env);
                            if (ImGui::SliderFloat("Wysokosc Slonca (Pitch)", &env.sunDirection.y, -1.0f, 1.0f)) engine->setEnvironmentSettings(env);
                            if (ImGui::SliderFloat("Azymut Slonca (Yaw)", &env.sunDirection.x, -1.0f, 1.0f)) engine->setEnvironmentSettings(env);
                            if (ImGui::ColorEdit3("Kolor Slonca", &env.sunColor.x)) engine->setEnvironmentSettings(env);
                        }
                    }

                    // G2. POSTPROCESY
                    else if (s_inspectorCategory == InspectorCategory::POST_PROCESS) {
                        ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Post-Processing & Kamera");
                        ImGui::Separator();
                        ImGui::DragFloat("Ekspozycja (Exposure EV)", &m_exposure, 0.02f, 0.1f, 10.0f, "%.2f");
                        ImGui::SliderFloat("Skala Rozdzielczosci", &m_resolutionScale, 0.5f, 2.0f, "%.2fx");

                        const char* aaModes[] = { "Wylaczony", "FXAA Szybki", "FXAA Jakosc", "FXAA Ultra" };
                        ImGui::Combo("Anti-Aliasing", &m_antiAliasingMode, aaModes, IM_ARRAYSIZE(aaModes));

                        static float bloomThresh = 1.0f;
                        static float bloomStrength = 0.4f;
                        ImGui::SliderFloat("Bloom (Prog)", &bloomThresh, 0.5f, 3.0f, "%.2f");
                        ImGui::SliderFloat("Bloom (Moc)", &bloomStrength, 0.0f, 1.0f, "%.2f");
                    }

                    // G3. HARDWARE RAY TRACING & PROFILER
                    else if (s_inspectorCategory == InspectorCategory::RENDER_RT) {
                        ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.0f, 1.0f), "Sprzetowy Ray Tracing (Vulkan Ray Query)");
                        ImGui::Separator();
                        const bool rtSupported = m_context && m_context->isRayTracingSupported();
                        if (!rtSupported) ImGui::TextDisabled("Ray tracing niedostepny na biezacym GPU.");

                        ImGui::BeginDisabled(!rtSupported);
                        ImGui::Checkbox("Aktywuj Hardware Ray Tracing", &m_rtSettings.enabled);
                        ImGui::BeginDisabled(!m_rtSettings.enabled);
                        ImGui::Checkbox("RT Cienie (Shadows)", &m_rtSettings.shadowsEnabled);
                        ImGui::Checkbox("RT Ambient Occlusion (AO)", &m_rtSettings.aoEnabled);
                        ImGui::Checkbox("RT Odbicia (Reflections)", &m_rtSettings.reflectionsEnabled);
                        ImGui::Checkbox("RT Global Illumination (GI)", &m_rtSettings.giEnabled);
                        ImGui::Checkbox("SVGF Denoiser (Filtr wariancji)", &m_rtSettings.svgfEnabled);

                        ImGui::Spacing();
                        ImGui::SeparatorText("Diagnostyka i Profiler GPU");
                        const auto& timings = m_rtDenoiser.getGpuTimings();
                        ImGui::Text("GI Trace:      %.3f ms", timings.traceMs);
                        ImGui::Text("Refl Trace:    %.3f ms", timings.reflTraceMs);
                        ImGui::Text("Denoiser SVGF: %.3f ms", timings.atrous0Ms + timings.atrous1Ms);
                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Laczny czas RT Compute: %.3f ms", timings.totalMs);

                        ImGui::EndDisabled();
                        ImGui::EndDisabled();
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
                    ImGui::Separator();
                    const char* assets[] = { "BP_Player", "BP_Coin", "M_ChromeIBL", "M_SlateFloor", "VFX_Fire", "Lvl_MainLevel" };
                    for (const char* a : assets) { ImGui::Button(a, ImVec2(115.0f * s_uiScale, 60.0f * s_uiScale)); ImGui::SameLine(); }
                } else {
                    ImGui::TextColored(ImVec4(0.22f, 0.85f, 1.0f, 1.0f), ">_ Output Log");
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

                if (ImGui::Button(s_showContentDrawer ? "[x] Zawartosc" : "[-] Zawartosc")) {
                    s_showContentDrawer = !s_showContentDrawer;
                    if (s_showContentDrawer) s_showOutputLog = false;
                }
                ImGui::SameLine();
                if (ImGui::Button(s_showOutputLog ? "[x] Dziennik" : ">_ Dziennik")) {
                    s_showOutputLog = !s_showOutputLog;
                    if (s_showOutputLog) s_showContentDrawer = false;
                }
                ImGui::SameLine(0.0f, 10.0f * s_uiScale);

                // Szybka konsola wykonawcza QuickJS w pasku stanu
                ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), ">_ QuickJS:");
                ImGui::SameLine();
                static char s_quickCmd[256] = "";
                const float cmdW = std::max(120.0f, screenW - 650.0f * s_uiScale);
                ImGui::SetNextItemWidth(cmdW);
                if (ImGui::InputTextWithHint("##QuickCmdBar", "Wpisz kod JS i nacisnij [Enter]...", s_quickCmd, sizeof(s_quickCmd), ImGuiInputTextFlags_EnterReturnsTrue) && engine) {
                    if (s_quickCmd[0] != '\0') {
                        engine->loadScript(s_quickCmd);
                        core::Logger::info("QuickJS Console: " + std::string(s_quickCmd));
                        s_quickCmd[0] = '\0';
                    }
                }

                // Telemetria renderera i silnika po prawej stronie
                ImGui::SameLine(screenW - 310.0f * s_uiScale);
                ImGui::TextColored(ImVec4(0.25f, 0.95f, 0.52f, 1.0f), "Vulkan 1.3: %.0f FPS (%.2f ms) | %ux%u",
                                   ImGui::GetIO().Framerate,
                                   1000.0f / std::max(1.0f, ImGui::GetIO().Framerate),
                                   m_width, m_height);
                ImGui::End();
                ImGui::PopStyleVar();
            }

            if (s_showScriptStudio) {
                ImGui::SetNextWindowSize(ImVec2(700 * s_uiScale, 500 * s_uiScale), ImGuiCond_FirstUseEver);
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
