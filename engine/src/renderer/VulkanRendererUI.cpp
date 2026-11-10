#include "renderer/VulkanRenderer.hpp"
#include "renderer/EditorUiStyle.hpp"
#include "renderer/EditorLocalization.hpp"
#include "renderer/EditorUiControls.hpp"
#include "renderer/EditorProjectDialogs.hpp"
#include "renderer/EditorUiBottomPanels.hpp"
#include "renderer/EditorOutlinerPanel.hpp"
#include "renderer/EditorPreferencesPanel.hpp"
#include "renderer/EditorTopBarPanel.hpp"
#include "renderer/EditorViewportOverlay.hpp"
#include "renderer/EditorCameraNavigation.hpp"
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
#include <cctype>
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

    void VulkanRenderer::initImGui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        loadEditorUiFont(io);
        applyEditorUiTheme();

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


    static float distToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
        const float abx = b.x - a.x;
        const float aby = b.y - a.y;
        const float lenSq = abx * abx + aby * aby;
        if (lenSq < 1e-4f) return hypotf(p.x - a.x, p.y - a.y);
        const float t = std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / lenSq, 0.0f, 1.0f);
        return hypotf(p.x - (a.x + t * abx), p.y - (a.y + t * aby));
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
        ImGui::GetStyle().Alpha = 1.0f;

        auto requestResolution = [&](uint32_t w, uint32_t h) {
            g_pendingRenderWidth  = std::clamp(w, 64u, 7680u);
            g_pendingRenderHeight = std::clamp(h, 64u, 4320u);
        };

        if (m_standaloneGameMode) {
            // Forward gameplay controls in standalone mode
            if (engine) {
                engine->setKeyInput("KeyW", ImGui::IsKeyDown(ImGuiKey_W));
                engine->setKeyInput("KeyS", ImGui::IsKeyDown(ImGuiKey_S));
                engine->setKeyInput("KeyA", ImGui::IsKeyDown(ImGuiKey_A));
                engine->setKeyInput("KeyD", ImGui::IsKeyDown(ImGuiKey_D));
                engine->setKeyInput("Space", ImGui::IsKeyDown(ImGuiKey_Space));
                engine->setKeyInput("ShiftLeft", ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift));
                engine->setKeyInput("ShiftRight", ImGui::IsKeyDown(ImGuiKey_RightShift));
                engine->setKeyInput("KeyE", ImGui::IsKeyDown(ImGuiKey_E));
                engine->setKeyInput("KeyR", ImGui::IsKeyDown(ImGuiKey_R));
                engine->setKeyInput("KeyF", ImGui::IsKeyDown(ImGuiKey_F));
                engine->setKeyInput("KeyQ", ImGui::IsKeyDown(ImGuiKey_Q));
                engine->setKeyInput("ArrowUp", ImGui::IsKeyDown(ImGuiKey_UpArrow));
                engine->setKeyInput("ArrowDown", ImGui::IsKeyDown(ImGuiKey_DownArrow));
                engine->setKeyInput("ArrowLeft", ImGui::IsKeyDown(ImGuiKey_LeftArrow));
                engine->setKeyInput("ArrowRight", ImGui::IsKeyDown(ImGuiKey_RightArrow));

                // 'R' key restarts simulation in standalone game
                if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
                    engine->restartSimulation();
                }

                // 'V' key toggles FPP / TPP perspective in gameplay
                if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
                    std::string curMode = engine->getCameraMode();
                    bool toFpp = (curMode != "firstPerson");
                    engine->setCameraMode(toFpp ? "firstPerson" : "thirdPerson");
                    auto boomView = registry.get().view<ecs::CameraBoomComponent>();
                    for (auto bEnt : boomView) {
                        auto& b = boomView.get<ecs::CameraBoomComponent>(bEnt);
                        b.isFirstPerson = toFpp;
                        b.targetArmLength = toFpp ? 0.0f : 8.0f;
                        b.cameraLag = !toFpp;
                    }
                }

                // Mouse look when dragging RMB or LMB
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    float sens = engine->getPreferences().camMouseSensitivity;
                    float yDelta = ImGui::GetIO().MouseDelta.y * sens;
                    if (engine->getPreferences().invertY) yDelta = -yDelta;
                    float camYaw = engine->getCameraYaw() + ImGui::GetIO().MouseDelta.x * sens;
                    float camPitch = glm::clamp(engine->getCameraPitch() - yDelta, -88.0f, 88.0f);
                    engine->setCamera(engine->getCameraPosition(), camYaw, camPitch, engine->getCameraFov());
                }

                // Mouse wheel zoom
                if (std::abs(ImGui::GetIO().MouseWheel) > 0.01f) {
                    float mw = ImGui::GetIO().MouseWheel;
                    auto boomView = registry.get().view<ecs::CameraBoomComponent>();
                    for (auto bEnt : boomView) {
                        auto& b = boomView.get<ecs::CameraBoomComponent>(bEnt);
                        if (mw > 0.1f) {
                            if (!b.isFirstPerson) {
                                b.targetArmLength -= 1.5f;
                                if (b.targetArmLength <= 1.0f) {
                                    b.targetArmLength = 0.0f;
                                    b.isFirstPerson = true;
                                    b.cameraLag = false;
                                    engine->setCameraMode("firstPerson");
                                }
                            }
                        } else if (mw < -0.1f) {
                            if (b.isFirstPerson) {
                                b.targetArmLength = 4.0f;
                                b.isFirstPerson = false;
                                b.cameraLag = true;
                                engine->setCameraMode("thirdPerson");
                            } else {
                                b.targetArmLength = glm::clamp(b.targetArmLength + 1.5f, 2.0f, 40.0f);
                            }
                        }
                    }
                }
            }

            // Top-left HUD Card
            ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(360, 125), ImGuiCond_Always);
            ImGuiWindowFlags hudFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.12f, 0.85f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
            if (ImGui::Begin("GameHUD", nullptr, hudFlags)) {
                const int hp = engine ? engine->getHealth() : 100;
                const int score = engine ? engine->getScore() : 0;
                const std::string st = engine ? engine->getGameStatus() : "PLAYING";

                ImVec4 hpColor = (hp > 50) ? ImVec4(0.1f, 0.85f, 0.45f, 1.0f) : ((hp > 25) ? ImVec4(0.95f, 0.75f, 0.1f, 1.0f) : ImVec4(0.95f, 0.25f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, hpColor);
                ImGui::ProgressBar(std::clamp(hp / 100.0f, 0.0f, 1.0f), ImVec2(332, 22), ("HEALTH: " + std::to_string(hp) + " / 100").c_str());
                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "POWER ORBS COLLECTED: %d / 5", score);
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "OBJECTIVE: Collect all orbs and reach the Portal!");
                ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.7f, 1.0f), "Controls: [WASD] Move | [SPACE] Jump | [V] FPP/TPP | [R] Restart");
            }
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();

            // Center Victory / Defeat Modal Banner
            const std::string st = engine ? engine->getGameStatus() : "PLAYING";
            const int hp = engine ? engine->getHealth() : 100;
            const int score = engine ? engine->getScore() : 0;
            if (st == "VICTORY" || st == "WIN" || hp <= 0 || st == "GAME_OVER" || st == "DEFEAT") {
                bool isWin = (st == "VICTORY" || st == "WIN");
                ImGui::SetNextWindowPos(ImVec2(screenW * 0.5f - 210, screenH * 0.5f - 100), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(420, 200), ImGuiCond_Always);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, isWin ? ImVec4(0.05f, 0.18f, 0.10f, 0.95f) : ImVec4(0.20f, 0.06f, 0.06f, 0.95f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
                if (ImGui::Begin("GameOutcomeModal", nullptr, hudFlags)) {
                    if (isWin) {
                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "=== VICTORY! MISSION COMPLETE ===");
                        ImGui::Spacing();
                        ImGui::TextWrapped("You successfully gathered all 5 Power Orbs and activated the Portal!");
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "Final Score: %d Orbs  |  Health Remaining: %d%%", score, hp);
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "=== GAME OVER / DEFEAT ===");
                        ImGui::Spacing();
                        ImGui::TextWrapped("Your health reached zero from hazardous laser traps!");
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Score: %d Orbs collected before falling.", score);
                    }
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    if (ImGui::Button(" [R] Play Again / Respawn ", ImVec2(380, 36))) {
                        if (engine) engine->restartSimulation();
                    }
                }
                ImGui::End();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
            }
        } else {
            static bool s_immersivePlay = false;
            static bool s_showPlaceActors = true;
            static bool s_showOutliner = true;
            static bool s_showDetails = true;
            static bool s_showContentDrawer = false;
            static bool s_showOutputLog = false;
            static bool s_showScriptStudio = false;
            static bool s_showPreferencesWindow = false;

            static bool s_prefsLoaded = false;
            if (!s_prefsLoaded && engine) {
                s_prefsLoaded = true;
                const auto& pref = engine->getPreferences();
                s_showPlaceActors   = pref.showPlaceActors;
                s_showOutliner      = pref.showOutliner;
                s_showDetails       = pref.showDetails;
                s_showContentDrawer = pref.showContentDrawer;
                s_showOutputLog     = pref.showOutputLog;
                s_showScriptStudio  = pref.showScriptStudio;
                m_uiScale           = pref.uiScale;
            }

            if (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_F11)) {
                s_immersivePlay = !s_immersivePlay;
            }

            static float s_userLeftPanelW = 260.0f;
            static float s_userRightPanelW = 310.0f;
            static float s_userOutlinerH = 240.0f;
            static float s_userDrawerH = 220.0f;
            static bool s_splittersInit = false;
            static bool s_splitterWasActive = false;
            static ImGuiID s_activeSplitterId = 0;
            static bool s_anySplitterHovered = false;
            static bool s_wasSplitterHoveredLastFrame = false;

            if (!s_splittersInit) {
                s_splittersInit = true;
                if (engine && engine->getPreferences().userLeftPanelW > 50.0f) {
                    const auto& pref = engine->getPreferences();
                    s_userLeftPanelW  = pref.userLeftPanelW;
                    s_userRightPanelW = pref.userRightPanelW;
                    s_userOutlinerH   = pref.userOutlinerH;
                    s_userDrawerH     = pref.userDrawerH;
                } else {
                    // Start with a balanced layout that scales with the actual editor viewport.
                    // Keep enough width for the scene viewport while giving Details room for
                    // longer rendering controls (RT, GI, denoiser and environment settings).
                    s_userLeftPanelW  = std::clamp(screenW * 0.16f, 210.0f * s_uiScale, 300.0f * s_uiScale);
                    s_userRightPanelW = std::clamp(screenW * 0.29f, 340.0f * s_uiScale, 540.0f * s_uiScale);
                    s_userOutlinerH   = std::clamp(screenH * 0.30f, 220.0f * s_uiScale, 360.0f * s_uiScale);
                    s_userDrawerH     = std::clamp(screenH * 0.24f, 180.0f * s_uiScale, 300.0f * s_uiScale);
                }
            }

            const float topTitleBarH  = s_immersivePlay ? 0.0f : (28.0f * s_uiScale);
            const float topLevelTabsH = s_immersivePlay ? 0.0f : (26.0f * s_uiScale);
            const float topToolbarH   = 36.0f * s_uiScale;
            const float topTotalH     = topTitleBarH + topLevelTabsH + topToolbarH;

            // Keep a usable viewport on narrow windows instead of forcing fixed minimum side panels.
            const float minViewportW = std::min(180.0f * s_uiScale, screenW * 0.35f);
            const float minPanelW = std::min(130.0f * s_uiScale,
                std::max(24.0f * s_uiScale, (screenW - minViewportW) * 0.5f));
            const float maxPanelW = std::max(minPanelW, (screenW - minViewportW) * 0.5f);
            s_userLeftPanelW  = std::clamp(s_userLeftPanelW, minPanelW, maxPanelW);
            s_userRightPanelW = std::clamp(s_userRightPanelW, minPanelW, maxPanelW);

            const float bottomStatusH = s_immersivePlay ? 0.0f : (24.0f * s_uiScale);
            const float minContentH = std::min(120.0f * s_uiScale, screenH * 0.25f);
            const float maxDrawerH = std::max(0.0f,
                std::min(screenH * 0.70f, screenH - topTotalH - bottomStatusH - minContentH));
            s_userDrawerH = std::clamp(s_userDrawerH, std::min(80.0f * s_uiScale, maxDrawerH), maxDrawerH);

            const float drawerH = (!s_immersivePlay && (s_showContentDrawer || s_showOutputLog)) ? s_userDrawerH : 0.0f;
            const float bottomTotalH = bottomStatusH + drawerH;

            const float contentH = std::max(minContentH, screenH - topTotalH - bottomTotalH);
            const float leftPanelW = (!s_immersivePlay && s_showPlaceActors) ? s_userLeftPanelW : 0.0f;
            const float rightPanelW = (!s_immersivePlay && (s_showOutliner || s_showDetails)) ? s_userRightPanelW : 0.0f;
            const float centerViewportW = std::max(minViewportW, screenW - leftPanelW - rightPanelW);

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

                if (hovered && s_activeSplitterId == 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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
            static EditorGizmoMode s_gizmoMode = EditorGizmoMode::TRANSLATE;
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
                if (ok) {
                    engine->getPreferences().lastProjectPath = targetPath;
                    if (engine->getPreferences().autoSavePreferences) engine->saveUserPreferences();
                }
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
                engine->requestLoadProject(targetPath);
                selectedEnt = entt::null;
                s_currentProjectPath = targetPath;
                engine->getPreferences().lastProjectPath = targetPath;
                if (engine->getPreferences().autoSavePreferences) engine->saveUserPreferences();
                snprintf(s_engineNotification, sizeof(s_engineNotification),
                         "Wczytywanie projektu: %s", targetPath.c_str());
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


            static bool s_camPrefsInit = false;
            if (!s_camPrefsInit && engine) {
                s_camPrefsInit = true;
                const auto& pref = engine->getPreferences();
                s_camFlySpeed     = pref.camFlySpeed;
                s_cameraSpeed     = pref.cameraSpeed;
                s_snapEnabled     = pref.snapEnabled;
                s_snapStep        = pref.snapStep;
                s_gizmoSpaceLocal = pref.gizmoSpaceLocal;
                if (!pref.lastProjectPath.empty()) {
                    s_currentProjectPath = pref.lastProjectPath;
                }
            }

            if (selectedEnt != entt::null && !registry.get().valid(selectedEnt)) {
                selectedEnt = entt::null;
                s_draggingGizmoAxis = 0;
                if (engine) engine->setSelectedEntity(entt::null);
            }
            if (engine && engine->getSelectedEntity() != entt::null && selectedEnt == entt::null) {
                selectedEnt = engine->getSelectedEntity();
            }

            EditorTopBarUiState topBarState{
                s_immersivePlay, s_showPreferencesWindow, s_showPlaceActors,
                s_showOutliner, s_showDetails, s_showContentDrawer,
                s_showOutputLog, s_showScriptStudio, s_uiScale,
                m_resolutionScale, m_antiAliasingMode,
                s_userLeftPanelW, s_userRightPanelW, s_userOutlinerH, s_userDrawerH,
                s_engineNotification, s_engineNotificationTimer,
                engine && engine->isSimulationSessionActive(),
                [&](bool saveAs) { saveCurrentGame(saveAs); },
                [&](const std::string& path) { loadCurrentGame(path); }
            };
            drawEditorTopBarPanel(engine, topBarState, screenW, topTitleBarH,
                                  topLevelTabsH, topToolbarH, selectedEnt);

            drawEditorPlaceActorsPanel(registry, engine, s_immersivePlay, s_showPlaceActors,
                                       s_showDetails, s_uiScale, topTotalH, contentH,
                                       leftPanelW, camPos, front, selectedEnt);



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

            if (!m_viewportDescriptor && m_viewportSampler && m_colorImageView) {
                m_viewportDescriptor = ImGui_ImplVulkan_AddTexture(
                    m_viewportSampler, m_colorImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            const bool hasViewportImage = m_viewportDescriptor != VK_NULL_HANDLE &&
                viewportPanelSize.x > 0 && viewportPanelSize.y > 0;
            if (hasViewportImage) {
                ImGui::Image((ImTextureID)m_viewportDescriptor, viewportPanelSize);
            }

            const ImVec2 vMin = ImGui::GetItemRectMin();
            const ImVec2 vSize = ImGui::GetItemRectSize();
            const bool isHovered = hasViewportImage && ImGui::IsItemHovered();
            const bool isWindowFocused = ImGui::IsWindowFocused();

            // Content Drawer -> Viewport: import glTF/GLB files as new scene actors.
            bool isAssetDropHovered = false;
            if (hasViewportImage && !(engine && engine->isSimulationSessionActive()) &&
                assetManager && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                        "LUCID_ASSET_PATH", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                    isAssetDropHovered = payload->Preview || payload->Delivery;
                    if (payload->Delivery && payload->Data && payload->DataSize > 1) {
                        const char* rawPath = static_cast<const char*>(payload->Data);
                        const char* pathEnd = static_cast<const char*>(std::memchr(rawPath, '\0', static_cast<size_t>(payload->DataSize)));
                        if (pathEnd) {
                            const std::filesystem::path droppedPath(std::string(rawPath, pathEnd));
                            std::string extension = droppedPath.extension().string();
                            std::transform(extension.begin(), extension.end(), extension.begin(),
                                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                            std::error_code dropError;
                            const bool supportedModel = extension == ".glb" || extension == ".gltf";
                            const bool fileExists = std::filesystem::is_regular_file(droppedPath, dropError) && !dropError;
                            if (supportedModel && fileExists) {
                                const uint32_t firstMeshId = assetManager->loadGLTF(droppedPath.string());
                                const auto& instances = assetManager->getLastImportedInstances();
                                if (assetManager->getMesh(firstMeshId) && !instances.empty()) {
                                    const std::string modelName = droppedPath.stem().string().empty()
                                        ? std::string("ImportedModel") : droppedPath.stem().string();
                                    const glm::vec3 placementOrigin = camPos + front * 5.0f;
                                    entt::entity firstCreated = entt::null;
                                    size_t createdCount = 0;
                                    for (const auto& instance : instances) {
                                        if (!assetManager->getMesh(instance.meshId)) continue;
                                        const entt::entity newEntity = registry.createEntity();
                                        const std::string nodeName = instance.name.empty() || instance.name == "Mesh"
                                            ? modelName + (instances.size() > 1 ? "_" + std::to_string(createdCount + 1) : "")
                                            : modelName + "/" + instance.name;
                                        registry.get().emplace<ecs::TagComponent>(newEntity, ecs::TagComponent{nodeName});
                                        ecs::TransformComponent transform{};
                                        transform.position = placementOrigin + instance.translation;
                                        transform.rotation = glm::degrees(glm::eulerAngles(instance.rotation));
                                        transform.scale = instance.scale;
                                        transform.orientation = instance.rotation;
                                        registry.get().emplace<ecs::TransformComponent>(newEntity, transform);
                                        registry.get().emplace<ecs::MeshComponent>(newEntity, ecs::MeshComponent{instance.meshId});
                                        if (firstCreated == entt::null) firstCreated = newEntity;
                                        ++createdCount;
                                    }
                                    if (firstCreated != entt::null) {
                                        selectedEnt = firstCreated;
                                        if (engine) engine->setSelectedEntity(firstCreated);
                                        core::Logger::info("Imported " + std::to_string(createdCount) +
                                            " glTF mesh instance(s) into viewport: " + droppedPath.string());
                                    } else {
                                        core::Logger::error("Viewport drop produced no valid mesh instances: " + droppedPath.string());
                                    }
                                } else {
                                    core::Logger::error("Viewport drop failed to import model: " + droppedPath.string());
                                }
                            } else if (!supportedModel) {
                                core::Logger::warn("Viewport drop supports .glb and .gltf models; import failed: " + droppedPath.string());
                            } else {
                                core::Logger::error("Viewport drop file is no longer available: " + droppedPath.string());
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            const ImVec2 mousePos = ImGui::GetIO().MousePos;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (isAssetDropHovered) {
                drawList->AddRectFilled(vMin, ImVec2(vMin.x + vSize.x, vMin.y + vSize.y),
                    IM_COL32(20, 115, 210, 42));
                drawList->AddRect(vMin, ImVec2(vMin.x + vSize.x, vMin.y + vSize.y),
                    IM_COL32(75, 175, 255, 230), 0.0f, 0, 3.0f);
                const char* dropHint = editorText("ui.drop_model_into_viewport", "Drop model to create actor");
                const ImVec2 hintSize = ImGui::CalcTextSize(dropHint);
                drawList->AddText(ImVec2(vMin.x + (vSize.x - hintSize.x) * 0.5f,
                                         vMin.y + (vSize.y - hintSize.y) * 0.5f),
                                  IM_COL32(235, 245, 255, 255), dropHint);
            }

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

            const bool isSimulationSessionActive = (engine != nullptr && engine->isSimulationSessionActive());
            const bool gizmoEnabled = !isSimulationSessionActive;
            if (!gizmoEnabled) s_draggingGizmoAxis = 0;

            if ((isHovered || isWindowFocused) && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
                const bool ctrlDown = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
                const bool shiftDown = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
                if (!isSimulationSessionActive && ctrlDown && shiftDown && ImGui::IsKeyPressed(ImGuiKey_S)) {
                    saveCurrentGame(true);
                } else if (!isSimulationSessionActive && ctrlDown && ImGui::IsKeyPressed(ImGuiKey_S)) {
                    saveCurrentGame(false);
                }
                if (!isSimulationSessionActive && ctrlDown && ImGui::IsKeyPressed(ImGuiKey_O)) {
                    loadCurrentGame("");
                }
                if (ctrlDown && ImGui::IsKeyPressed(ImGuiKey_Comma)) {
                    s_showPreferencesWindow = !s_showPreferencesWindow;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_F4)) {
                    s_showScriptStudio = !s_showScriptStudio;
                }
                if (ctrlDown && ImGui::IsKeyPressed(ImGuiKey_Space)) {
                    s_showContentDrawer = !s_showContentDrawer;
                    if (s_showContentDrawer) s_showOutputLog = false;
                }
                if (!isSimulationSessionActive && !s_isRmbNavigating) {
                    if (ImGui::IsKeyPressed(ImGuiKey_W) || ImGui::IsKeyPressed(ImGuiKey_G)) s_gizmoMode = EditorGizmoMode::TRANSLATE;
                    if (ImGui::IsKeyPressed(ImGuiKey_E) || (!ctrlDown && ImGui::IsKeyPressed(ImGuiKey_R))) s_gizmoMode = EditorGizmoMode::ROTATE;
                    if (ImGui::IsKeyPressed(ImGuiKey_S) && !ctrlDown) s_gizmoMode = EditorGizmoMode::SCALE;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_F) && selectedEnt != entt::null && registry.get().valid(selectedEnt) && registry.get().all_of<ecs::TransformComponent>(selectedEnt)) {
                    const auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                    const float frameDist = std::max(5.0f, glm::length(t.scale) * 3.5f);
                    if (engine) engine->setCamera(t.position - front * frameDist, camYaw, camPitch, camFov);
                }
                if (!isSimulationSessionActive && ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedEnt != entt::null && registry.get().valid(selectedEnt)) {
                    if (engine) engine->destroyEntity(static_cast<uint32_t>(selectedEnt));
                    else registry.get().destroy(selectedEnt);
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

            if (gizmoEnabled && hasSelectedTransform) {
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

            if (gizmoEnabled && !blockViewportClicks && isHovered && !altDown && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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

            if (gizmoEnabled && s_draggingGizmoAxis > 0 && hasSelectedTransform) {
                auto& t = registry.get().get<ecs::TransformComponent>(selectedEnt);
                if (s_gizmoMode == EditorGizmoMode::TRANSLATE) {
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
                } else if (s_gizmoMode == EditorGizmoMode::ROTATE) {
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
                } else if (s_gizmoMode == EditorGizmoMode::SCALE) {
                    float scaleFactor = hypotf(mousePos.x - s_dragPivotScreen.x, mousePos.y - s_dragPivotScreen.y) / s_dragInitialDist;
                    if (s_snapEnabled) scaleFactor = std::max(0.1f, std::round(scaleFactor / 0.25f) * 0.25f);
                    if (s_draggingGizmoAxis == 1) t.scale.x = std::max(0.01f, s_dragInitialScale.x * scaleFactor);
                    else if (s_draggingGizmoAxis == 2) t.scale.y = std::max(0.01f, s_dragInitialScale.y * scaleFactor);
                    else if (s_draggingGizmoAxis == 3) t.scale.z = std::max(0.01f, s_dragInitialScale.z * scaleFactor);
                    else t.scale = glm::max(glm::vec3(0.01f), s_dragInitialScale * scaleFactor);
                    if (engine) engine->syncEntityTransformPhysics(selectedEnt);
                }
            }

            EditorCameraNavigationState cameraNavigationState{
                s_isRmbNavigating, s_isMmbPanning, s_isAltOrbiting, isHovered,
                s_camFlySpeed, s_cameraSpeed, hasSelectedTransform,
                selectedEnt, camPos, front, right, up, camYaw, camPitch, camFov
            };
            drawEditorCameraNavigation(engine, registry, cameraNavigationState);

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
            if (gizmoEnabled && hasSelectedTransform) {
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

            EditorViewportOverlayState viewportOverlayState{
                s_immersivePlay, s_showPlaceActors, s_showOutliner, s_showDetails,
                s_snapEnabled, s_cameraSpeed, s_uiScale, s_gizmoMode, gizmoEnabled
            };
            drawEditorViewportOverlay(viewportOverlayState, glm::vec2(viewportPanelSize.x, viewportPanelSize.y));

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            if (engine && !engine->isPaused()) {
                const int hp = engine->getHealth();
                const int score = engine->getScore();
                const std::string st = engine->getGameStatus();

                if (score > 0 || hp < 100 || st == "VICTORY" || st == "WIN" || st == "GAME_OVER" || st == "DEFEAT") {
                    ImGui::SetNextWindowPos(ImVec2(vMin.x + 20, vMin.y + 40), ImGuiCond_Always);
                    ImGui::SetNextWindowSize(ImVec2(340, 110), ImGuiCond_Always);
                    ImGuiWindowFlags vpHudFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
                    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.12f, 0.85f));
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
                    if (ImGui::Begin("##ViewportGameHUD", nullptr, vpHudFlags)) {
                        ImVec4 hpColor = (hp > 50) ? ImVec4(0.1f, 0.85f, 0.45f, 1.0f) : ((hp > 25) ? ImVec4(0.95f, 0.75f, 0.1f, 1.0f) : ImVec4(0.95f, 0.25f, 0.2f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, hpColor);
                        ImGui::ProgressBar(std::clamp(hp / 100.0f, 0.0f, 1.0f), ImVec2(316, 20), ("HEALTH: " + std::to_string(hp) + " / 100").c_str());
                        ImGui::PopStyleColor();
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "POWER ORBS: %d / 5", score);
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.22f, 0.74f, 0.97f, 1.0f), " | STATUS: %s", st.c_str());
                        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.7f, 1.0f), "[WASD] Move  [SPACE] Jump  [V] FPP/TPP  [R] Reset");
                    }
                    ImGui::End();
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor();

                    if (st == "VICTORY" || st == "WIN" || hp <= 0 || st == "GAME_OVER" || st == "DEFEAT") {
                        bool isWin = (st == "VICTORY" || st == "WIN");
                        ImGui::SetNextWindowPos(ImVec2(vMin.x + (vSize.x - 380) * 0.5f, vMin.y + (vSize.y - 180) * 0.5f), ImGuiCond_Always);
                        ImGui::SetNextWindowSize(ImVec2(380, 180), ImGuiCond_Always);
                        ImGui::PushStyleColor(ImGuiCol_WindowBg, isWin ? ImVec4(0.05f, 0.18f, 0.10f, 0.95f) : ImVec4(0.20f, 0.06f, 0.06f, 0.95f));
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
                        if (ImGui::Begin("##ViewportGameModal", nullptr, vpHudFlags)) {
                            if (isWin) {
                                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "=== VICTORY! MISSION COMPLETE ===");
                                ImGui::Spacing();
                                ImGui::TextWrapped("All Power Orbs collected & Portal reached!");
                                ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "Final Score: %d Orbs  |  Health: %d%%", score, hp);
                            } else {
                                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "=== GAME OVER / DEFEAT ===");
                                ImGui::Spacing();
                                ImGui::TextWrapped("Destroyed by hazardous laser trap!");
                                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Orbs collected: %d", score);
                            }
                            ImGui::Spacing();
                            if (ImGui::Button(" [R] Play Again / Respawn ", ImVec2(348, 32))) {
                                engine->restartSimulation();
                            }
                        }
                        ImGui::End();
                        ImGui::PopStyleVar(2);
                        ImGui::PopStyleColor();
                    }
                }
            }

            drawEditorOutlinerPanel(registry, engine, selectedEnt, s_immersivePlay,
                                     s_showOutliner, s_showDetails, s_uiScale,
                                     leftPanelW, centerViewportW, rightPanelW, topTotalH, outlinerH,
                                     camPos, front, camYaw, camPitch, camFov);


            drawEditorDetailsPanel(registry, engine, selectedEnt, s_immersivePlay,
                                   s_showDetails, s_showOutliner, s_uiScale,
                                   leftPanelW, centerViewportW, topTotalH, outlinerH,
                                   rightPanelW, detailsH, camPos, front, camYaw, camPitch,
                                   camFov, glm::vec2(viewportPanelSize.x, viewportPanelSize.y));


            drawEditorBottomPanels(s_immersivePlay, s_showContentDrawer, s_showOutputLog,
                                   screenW, screenH, bottomTotalH, drawerH, bottomStatusH,
                                   s_uiScale, ImGui::GetIO().Framerate, m_width, m_height);


            EditorPreferencesUiState preferencesUiState{
                s_showPreferencesWindow, s_showScriptStudio,
                s_showPlaceActors, s_showOutliner, s_showDetails,
                s_showContentDrawer, s_showOutputLog,
                s_uiScale, s_userLeftPanelW, s_userRightPanelW,
                s_userOutlinerH, s_userDrawerH,
                s_camFlySpeed, s_cameraSpeed, s_snapEnabled, s_snapStep,
                s_gizmoSpaceLocal, s_currentProjectPath,
                s_engineNotification, s_engineNotificationTimer
            };
            drawEditorPreferencesPanel(engine, preferencesUiState, screenW, screenH);

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

            // Persist panel dimensions once, when the user finishes a splitter drag.
            // Saving every frame while dragging would cause unnecessary disk I/O.
            const bool splitterIsActive = (s_activeSplitterId != 0);
            if (s_splitterWasActive && !splitterIsActive && engine) {
                auto& pref = engine->getPreferences();
                pref.userLeftPanelW = s_userLeftPanelW;
                pref.userRightPanelW = s_userRightPanelW;
                pref.userOutlinerH = s_userOutlinerH;
                pref.userDrawerH = s_userDrawerH;
                engine->saveUserPreferences();
            }
            s_splitterWasActive = splitterIsActive;

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
