#include "renderer/EditorUiBottomPanels.hpp"
#include "renderer/EditorUiStyle.hpp"
#include "renderer/EditorLocalization.hpp"
#include "core/Logger.hpp"
#include "imgui.h"

#include <algorithm>
#include <cctype>
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
        if (ImGui::Button(editorText("ui.content_drawer", "  Content Drawer  "), ImVec2(0, 22.0f * uiScale))) {
            showContentDrawer = true;
            showOutputLog = false;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, !isDrawer ? ImVec4(0.10f, 0.34f, 0.58f, 1.0f) : ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
        if (ImGui::Button(editorText("ui.output_log_terminal", "  >_ Output Log  "), ImVec2(0, 22.0f * uiScale))) {
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
            static fs::path s_currentBrowsePath = "engine/assets";
            static fs::path s_selectedAssetPath;
            // Blender/BUI inspired file browser navigation bar
            std::error_code pathError;
            if (!fs::is_directory(s_currentBrowsePath, pathError) || pathError) {
                pathError.clear();
                if (fs::is_directory("assets", pathError) && !pathError) s_currentBrowsePath = "assets";
                else s_currentBrowsePath = ".";
            }
            if (!s_selectedAssetPath.empty() && s_selectedAssetPath.parent_path() != s_currentBrowsePath) {
                s_selectedAssetPath.clear();
            }

            // Top breadcrumbs & navigation strip
            if (ImGui::Button(editorText("ui.navigate_parent", " .. [Up] ")) && s_currentBrowsePath.has_parent_path()) {
                fs::path parent = s_currentBrowsePath.parent_path();
                if (!parent.empty() && fs::exists(parent)) {
                    s_currentBrowsePath = parent;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(editorText("ui.engine_assets", " engine/assets "))) s_currentBrowsePath = "engine/assets";
            ImGui::SameLine();
            if (fs::exists("assets")) {
                if (ImGui::Button(editorText("ui.assets_folder", " assets/ "))) s_currentBrowsePath = "assets";
                ImGui::SameLine();
            }
            if (fs::exists("scripts")) {
                if (ImGui::Button(editorText("ui.scripts_folder", " scripts/ "))) s_currentBrowsePath = "scripts";
                ImGui::SameLine();
            }
            static fs::path s_cachedBrowsePath;
            static std::vector<fs::directory_entry> s_cachedEntries;
            static std::string s_cachedBrowseError;
            static bool s_refreshAssets = true;
            if (s_cachedBrowsePath != s_currentBrowsePath) s_refreshAssets = true;
            if (ImGui::SmallButton(editorText("ui.refresh_assets", "Refresh"))) s_refreshAssets = true;
            ImGui::SameLine();

            // Filter chips
            static int s_assetFilterCategory = 0;
            static char s_assetSearch[128] = "";
            ImGui::SetNextItemWidth(std::min(260.0f * uiScale, ImGui::GetContentRegionAvail().x * 0.42f));
            ImGui::InputTextWithHint("##AssetSearch", editorText("ui.search_assets", "Search assets..."), s_assetSearch, sizeof(s_assetSearch));
            ImGui::SameLine();
            auto drawAssetChip = [&](const char* name, int cIdx) {
                const bool active = (s_assetFilterCategory == cIdx);
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.44f, 0.95f, 1.0f));
                else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
                if (ImGui::SmallButton(name)) s_assetFilterCategory = cIdx;
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, 3.0f * uiScale);
            };
            drawAssetChip(editorText("ui.asset_filter_all", "All"), 0);
            drawAssetChip(editorText("ui.asset_filter_textures_hdr", "Textures/HDR"), 1);
            drawAssetChip(editorText("ui.asset_filter_3d_models", "3D Models"), 2);
            drawAssetChip(editorText("ui.asset_filter_scripts", "JS Scripts"), 3);

            ImGui::TextDisabled(editorText("ui.asset_location", "Location: %s"), s_currentBrowsePath.string().c_str());
            ImGui::Separator();

            // Grid of files & folders
            // Reserve a compact metadata footer so selecting an asset never requires opening it.
            ImGui::BeginChild("AssetGridScroll", ImVec2(0, -58.0f * uiScale), false);
            const float itemWidth = 120.0f * uiScale;
            const float itemHeight = 65.0f * uiScale;
            const float panelWidth = ImGui::GetContentRegionAvail().x;
            const int columns = std::max(1, static_cast<int>(panelWidth / (itemWidth + 10.0f * uiScale)));

            int colIdx = 0;
            int visibleAssetCount = 0;
            std::error_code ec;
            if (s_refreshAssets) {
                s_cachedEntries.clear();
                s_cachedBrowseError.clear();
                try {
                    fs::directory_iterator it(s_currentBrowsePath, fs::directory_options::skip_permission_denied, ec);
                    const fs::directory_iterator end;
                    while (!ec && it != end) {
                        s_cachedEntries.push_back(*it);
                        it.increment(ec);
                    }
                    if (ec) s_cachedBrowseError = ec.message();
                } catch (const fs::filesystem_error& error) {
                    s_cachedBrowseError = error.what();
                }

                std::sort(s_cachedEntries.begin(), s_cachedEntries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
                    std::error_code aError, bError;
                    const bool aDirectory = a.is_directory(aError);
                    const bool bDirectory = b.is_directory(bError);
                    if (aDirectory != bDirectory) return aDirectory;
                    return a.path().filename().string() < b.path().filename().string();
                });
                s_cachedBrowsePath = s_currentBrowsePath;
                s_refreshAssets = false;
            }

            for (const auto& entry : s_cachedEntries) {
                const std::string filename = entry.path().filename().string();
                const std::string ext = entry.path().extension().string();
                std::error_code entryError;
                const bool isDirectory = entry.is_directory(entryError);
                if (entryError) continue;

                std::string searchName = filename;
                std::string searchQuery = s_assetSearch;
                std::transform(searchName.begin(), searchName.end(), searchName.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                std::transform(searchQuery.begin(), searchQuery.end(), searchQuery.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (!searchQuery.empty() && searchName.find(searchQuery) == std::string::npos) continue;

                // Category filtering
                if (!isDirectory) {
                    if (s_assetFilterCategory == 1 && ext != ".hdr" && ext != ".exr" && ext != ".png" && ext != ".jpg") continue;
                    if (s_assetFilterCategory == 2 && ext != ".gltf" && ext != ".glb" && ext != ".obj" && ext != ".bin") continue;
                    if (s_assetFilterCategory == 3 && ext != ".js" && ext != ".mjs" && ext != ".ts") continue;
                }

                ++visibleAssetCount;
                ImGui::PushID(filename.c_str());
                ImVec2 cardPos = ImGui::GetCursorScreenPos();

                VectorIconType cardIcon = VectorIconType::Cube;
                ImU32 iconColor = IM_COL32(100, 180, 245, 255);
                if (isDirectory) {
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
                    if (isDirectory) {
                        s_currentBrowsePath = entry.path();
                        s_selectedAssetPath.clear();
                    } else {
                        s_selectedAssetPath = entry.path();
                    }
                }

                // Only glTF assets are currently connected to the runtime mesh importer.
                std::string normalizedExt = ext;
                std::transform(normalizedExt.begin(), normalizedExt.end(), normalizedExt.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                const bool isImportableModel = !isDirectory && (normalizedExt == ".gltf" || normalizedExt == ".glb");
                if (isImportableModel && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    const std::string payloadPath = entry.path().string();
                    ImGui::SetDragDropPayload("LUCID_ASSET_PATH", payloadPath.c_str(), payloadPath.size() + 1);
                    ImGui::Text("%s", filename.c_str());
                    ImGui::TextDisabled("%s", editorText("ui.drop_model_into_viewport", "Drop into the 3D viewport to create an actor"));
                    ImGui::EndDragDropSource();
                }

                // Draw selection outline without changing the button's hit target.
                ImDrawList* dl = ImGui::GetWindowDrawList();
                if (!isDirectory && s_selectedAssetPath == entry.path()) {
                    dl->AddRect(cardPos, ImVec2(cardPos.x + itemWidth, cardPos.y + itemHeight),
                                IM_COL32(65, 165, 255, 255), 5.0f, 0, 2.0f);
                }
                drawEditorVectorIcon(dl, ImVec2(cardPos.x + itemWidth * 0.5f, cardPos.y + 20.0f * uiScale), 7.0f * uiScale, cardIcon, iconColor);

                std::string displayLabel = filename;
                if (displayLabel.length() > 14) {
                    displayLabel = displayLabel.substr(0, 12) + "..";
                }
                const float textW = ImGui::CalcTextSize(displayLabel.c_str()).x;
                dl->AddText(ImVec2(cardPos.x + std::max(4.0f, (itemWidth - textW) * 0.5f), cardPos.y + itemHeight - 22.0f * uiScale), IM_COL32(230, 235, 245, 255), displayLabel.c_str());

                if (ImGui::IsItemHovered()) {
                    if (isDirectory) {
                        ImGui::SetTooltip(editorText("ui.folder_tooltip", "Folder: %s\nClick to open"), filename.c_str());
                    } else {
                        const auto fileSize = entry.file_size(ec);
                        if (ec) ec.clear();
                        if (isImportableModel) {
                            ImGui::SetTooltip("%s\n%s", filename.c_str(),
                                editorText("ui.drag_model_tooltip", "Drag this glTF model into the 3D viewport."));
                        } else {
                            ImGui::SetTooltip(editorText("ui.file_tooltip", "File: %s\nSize: %zu B"), filename.c_str(), fileSize);
                        }
                    }
                }

                colIdx++;
                if (colIdx < columns) {
                    ImGui::SameLine(0.0f, 6.0f * uiScale);
                } else {
                    colIdx = 0;
                }
                ImGui::PopID();
            }

            if (!s_cachedBrowseError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "%s: %s", editorText("ui.asset_read_error", "Cannot read directory"), s_cachedBrowseError.c_str());
            if (visibleAssetCount == 0 && s_cachedBrowseError.empty()) ImGui::TextDisabled(editorText("ui.asset_no_results", "No assets match this search."));
            ImGui::EndChild();

            ImGui::Separator();
            if (!s_selectedAssetPath.empty()) {
                std::error_code selectedError;
                const bool selectedExists = fs::is_regular_file(s_selectedAssetPath, selectedError) && !selectedError;
                if (selectedExists) {
                    const std::string selectedName = s_selectedAssetPath.filename().string();
                    const std::string selectedExtension = s_selectedAssetPath.extension().string();
                    selectedError.clear();
                    const auto selectedSize = fs::file_size(s_selectedAssetPath, selectedError);
                    ImGui::Text("%s", selectedName.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", selectedExtension.empty() ? editorText("ui.asset_unknown_type", "Unknown type") : selectedExtension.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton(editorText("ui.copy_asset_path", "Copy Path"))) {
                        ImGui::SetClipboardText(s_selectedAssetPath.string().c_str());
                    }
                    ImGui::SameLine();
                    if (selectedError) {
                        ImGui::TextDisabled("%s", editorText("ui.asset_size_unavailable", "Size unavailable"));
                    } else {
                        ImGui::TextDisabled("%s: %llu B", editorText("ui.asset_size", "Size"), static_cast<unsigned long long>(selectedSize));
                    }
                    ImGui::TextDisabled("%s", s_selectedAssetPath.string().c_str());
                } else {
                    s_selectedAssetPath.clear();
                    ImGui::TextDisabled("%s", editorText("ui.asset_select_hint", "Select a file to inspect its metadata."));
                }
            } else {
                ImGui::TextDisabled("%s", editorText("ui.asset_select_hint", "Select a file to inspect its metadata."));
            }
        } else {
            static bool s_autoScroll = true;
            ImGui::Checkbox(editorText("ui.auto_scroll", "Auto-scroll"), &s_autoScroll);
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
        if (ImGui::Button(editorText("ui.content_drawer_status", " Content Drawer "))) { showContentDrawer = !showContentDrawer; if (showContentDrawer) showOutputLog = false; }
        ImGui::SameLine();
        if (ImGui::Button(editorText("ui.output_log_status", " >_ Output Log "))) { showOutputLog = !showOutputLog; if (showOutputLog) showContentDrawer = false; }
        ImGui::SameLine(screenW - 220.0f * uiScale);
        ImGui::TextColored(ImVec4(0.22f, 0.85f, 1.0f, 1.0f), "Vulkan 1.3: %.0f FPS (%ux%u)", frameRate, renderWidth, renderHeight);
        ImGui::End();
        ImGui::PopStyleVar();
    }
}

} // namespace engine::renderer
#endif
