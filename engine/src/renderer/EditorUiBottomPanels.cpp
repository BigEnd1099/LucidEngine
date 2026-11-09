#include "renderer/EditorUiBottomPanels.hpp"
#include "renderer/EditorUiStyle.hpp"
#include "core/Logger.hpp"
#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#ifdef USE_GLFW
namespace engine::renderer {

namespace fs = std::filesystem;

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
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.055f, 0.075f, 1.0f));
        ImGui::Begin("BottomDrawerWindow", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        const bool isDrawer = showContentDrawer;
        ImGui::PushStyleColor(ImGuiCol_Button, isDrawer ? ImVec4(0.10f, 0.34f, 0.58f, 1.0f) : ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
        if (ImGui::Button("  Content Drawer  ", ImVec2(0, 22.0f * uiScale))) {
            showContentDrawer = true;
            showOutputLog = false;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, !isDrawer ? ImVec4(0.10f, 0.34f, 0.58f, 1.0f) : ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
        if (ImGui::Button("  >_ Output Log  ", ImVec2(0, 22.0f * uiScale))) {
            showContentDrawer = false;
            showOutputLog = true;
        }
        ImGui::PopStyleColor();

        ImGui::SameLine(screenW - 28.0f * uiScale);
        if (ImGui::SmallButton("X##CloseDrawerWindow")) {
            showContentDrawer = false;
            showOutputLog = false;
        }
        ImGui::Separator();

        if (showContentDrawer) {
            // Blender/BUI inspired file browser navigation bar
            static fs::path s_currentBrowsePath = "engine/assets";
            if (!fs::exists(s_currentBrowsePath)) {
                if (fs::exists("assets")) s_currentBrowsePath = "assets";
                else s_currentBrowsePath = ".";
            }

            // Top breadcrumbs & navigation strip
            if (ImGui::Button(" .. [Do gory] ") && s_currentBrowsePath.has_parent_path()) {
                fs::path parent = s_currentBrowsePath.parent_path();
                if (!parent.empty() && fs::exists(parent)) {
                    s_currentBrowsePath = parent;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(" engine/assets ")) s_currentBrowsePath = "engine/assets";
            ImGui::SameLine();
            if (fs::exists("assets")) {
                if (ImGui::Button(" assets/ ")) s_currentBrowsePath = "assets";
                ImGui::SameLine();
            }
            if (fs::exists("scripts")) {
                if (ImGui::Button(" scripts/ ")) s_currentBrowsePath = "scripts";
                ImGui::SameLine();
            }

            // Filter chips
            static int s_assetFilterCategory = 0; // 0: Wszystko, 1: Tekstury/HDR, 2: Modele, 3: Skrypty
            auto drawAssetChip = [&](const char* name, int cIdx) {
                const bool active = (s_assetFilterCategory == cIdx);
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.44f, 0.95f, 1.0f));
                else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
                if (ImGui::SmallButton(name)) s_assetFilterCategory = cIdx;
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, 3.0f * uiScale);
            };
            drawAssetChip("Wszystko", 0);
            drawAssetChip("Tekstury/HDR", 1);
            drawAssetChip("Modele 3D", 2);
            drawAssetChip("Skrypty JS", 3);

            ImGui::TextDisabled("Lokalizacja: %s", s_currentBrowsePath.string().c_str());
            ImGui::Separator();

            // Grid of files & folders
            ImGui::BeginChild("AssetGridScroll", ImVec2(0, 0), false);
            const float itemWidth = 120.0f * uiScale;
            const float itemHeight = 65.0f * uiScale;
            const float panelWidth = ImGui::GetContentRegionAvail().x;
            const int columns = std::max(1, static_cast<int>(panelWidth / (itemWidth + 10.0f * uiScale)));

            int colIdx = 0;
            std::error_code ec;
            std::vector<fs::directory_entry> entries;
            for (const auto& entry : fs::directory_iterator(s_currentBrowsePath, ec)) {
                entries.push_back(entry);
            }

            // Sort: folders first, then alphabetical
            std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
                if (a.is_directory() != b.is_directory()) return a.is_directory();
                return a.path().filename().string() < b.path().filename().string();
            });

            for (const auto& entry : entries) {
                const std::string filename = entry.path().filename().string();
                const std::string ext = entry.path().extension().string();

                // Category filtering
                if (!entry.is_directory()) {
                    if (s_assetFilterCategory == 1 && ext != ".hdr" && ext != ".exr" && ext != ".png" && ext != ".jpg") continue;
                    if (s_assetFilterCategory == 2 && ext != ".gltf" && ext != ".glb" && ext != ".obj" && ext != ".bin") continue;
                    if (s_assetFilterCategory == 3 && ext != ".js" && ext != ".mjs" && ext != ".ts") continue;
                }

                ImGui::PushID(filename.c_str());
                ImVec2 cardPos = ImGui::GetCursorScreenPos();

                VectorIconType cardIcon = VectorIconType::Cube;
                ImU32 iconColor = IM_COL32(100, 180, 245, 255);
                if (entry.is_directory()) {
                    cardIcon = VectorIconType::Folder;
                    iconColor = IM_COL32(245, 158, 11, 255); // orange
                } else if (ext == ".hdr" || ext == ".exr") {
                    cardIcon = VectorIconType::Globe;
                    iconColor = IM_COL32(251, 191, 36, 255); // gold
                } else if (ext == ".js" || ext == ".mjs") {
                    cardIcon = VectorIconType::Wand;
                    iconColor = IM_COL32(168, 85, 247, 255); // purple
                } else if (ext == ".scene") {
                    cardIcon = VectorIconType::Sparkles;
                    iconColor = IM_COL32(34, 197, 94, 255); // green
                }

                std::string btnLabel = "##Card_" + filename;
                if (ImGui::Button(btnLabel.c_str(), ImVec2(itemWidth, itemHeight))) {
                    if (entry.is_directory()) {
                        s_currentBrowsePath = entry.path();
                    }
                }

                // Draw icon & title inside card
                ImDrawList* dl = ImGui::GetWindowDrawList();
                drawEditorVectorIcon(dl, ImVec2(cardPos.x + itemWidth * 0.5f, cardPos.y + 20.0f * uiScale), 7.0f * uiScale, cardIcon, iconColor);

                std::string displayLabel = filename;
                if (displayLabel.length() > 14) {
                    displayLabel = displayLabel.substr(0, 12) + "..";
                }
                const float textW = ImGui::CalcTextSize(displayLabel.c_str()).x;
                dl->AddText(ImVec2(cardPos.x + std::max(4.0f, (itemWidth - textW) * 0.5f), cardPos.y + itemHeight - 22.0f * uiScale), IM_COL32(230, 235, 245, 255), displayLabel.c_str());

                if (ImGui::IsItemHovered()) {
                    if (entry.is_directory()) ImGui::SetTooltip("Folder: %s\nKliknij dwukrotnie, aby otworzyc", filename.c_str());
                    else ImGui::SetTooltip("Plik: %s\nRozmiar: %zu B", filename.c_str(), entry.file_size(ec));
                }

                colIdx++;
                if (colIdx < columns) {
                    ImGui::SameLine(0.0f, 6.0f * uiScale);
                } else {
                    colIdx = 0;
                }
                ImGui::PopID();
            }

            ImGui::EndChild();
        } else {
            static bool s_autoScroll = true;
            ImGui::Checkbox("Auto-scroll", &s_autoScroll);
            ImGui::Separator();
            ImGui::BeginChild("OutputLogScroll", ImVec2(0, 0), false);
            for (const auto& line : core::Logger::getRecentLogs(60)) {
                ImGui::TextUnformatted(line.c_str());
            }
            if (s_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::PopStyleColor();
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
