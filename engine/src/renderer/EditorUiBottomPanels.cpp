#include "renderer/EditorUiBottomPanels.hpp"
#include "core/Logger.hpp"
#include "imgui.h"

#ifdef USE_GLFW
namespace engine::renderer {

void drawEditorBottomPanels(bool immersivePlay,
                            bool& showContentDrawer,
                            bool& showOutputLog,
                            float screenW,
                            float screenH,
                            float bottomTotalH,
                            float drawerH,
                            float bottomStatusH,
                            float uiScale,
                            float frameRate,
                            unsigned int renderWidth,
                            unsigned int renderHeight) {

    if (!immersivePlay && (showContentDrawer || showOutputLog)) {
        ImGui::SetNextWindowPos(ImVec2(0, screenH - bottomTotalH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(screenW, drawerH), ImGuiCond_Always);
        ImGui::Begin("BottomDrawerWindow", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        if (showContentDrawer) {
            ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.08f, 1.0f), "Content Drawer");
            ImGui::SameLine(screenW - 28.0f * uiScale);
            if (ImGui::SmallButton("X##CloseDrawer")) { showContentDrawer = false; }
            ImGui::Separator();
            const char* assets[] = { "BP_Player", "BP_Coin", "M_ChromeIBL", "M_SlateFloor", "VFX_Fire", "Lvl_MainLevel" };
            for (const char* a : assets) { ImGui::Button(a, ImVec2(115.0f * uiScale, 60.0f * uiScale)); ImGui::SameLine(); }
        } else {
            ImGui::TextColored(ImVec4(0.22f, 0.85f, 1.0f, 1.0f), ">_ Output Log");
            ImGui::SameLine(screenW - 28.0f * uiScale);
            if (ImGui::SmallButton("X##CloseLog")) { showOutputLog = false; }
            ImGui::Separator();
            ImGui::BeginChild("OutputLogScroll", ImVec2(0, 0), false);
            for (const auto& line : core::Logger::getRecentLogs(40)) ImGui::TextUnformatted(line.c_str());
            ImGui::EndChild();
        }
        ImGui::End();
    }

    if (!immersivePlay) {
        ImGui::SetNextWindowPos(ImVec2(0, screenH - bottomStatusH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(screenW, bottomStatusH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 3));
        ImGui::Begin("BottomStatusRail", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGui::Button(" Content Drawer ")) { showContentDrawer = !showContentDrawer; if (showContentDrawer) showOutputLog = false; }
        ImGui::SameLine();
        if (ImGui::Button(" >_ Output Log ")) { showOutputLog = !showOutputLog; if (showOutputLog) showContentDrawer = false; }
        ImGui::SameLine(screenW - 220.0f * uiScale);
        ImGui::TextColored(ImVec4(0.22f, 0.85f, 1.0f, 1.0f), "Vulkan 1.3: %.0f FPS (%ux%u)", frameRate, renderWidth, renderHeight);
        ImGui::End();
        ImGui::PopStyleVar();
    }

}

} // namespace engine::renderer
#endif
