#include "renderer/VulkanRenderer.hpp"
#include "renderer/EditorCameraNavigation.hpp"
#include "core/Engine.hpp"
#ifdef USE_GLFW
#include "imgui.h"
#include <algorithm>
#include <cmath>
namespace engine::renderer {
void VulkanRenderer::drawEditorCameraNavigation(core::Engine* engine,
                                                ecs::Registry& registry,
                                                EditorCameraNavigationState& state) {
            // Forward gameplay controls (WASD, Space, Shift, V) during simulation
            if (engine && !engine->isPaused()) {
                if (state.isHovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
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
                            if (toFpp) {
                                b.targetArmLength = 0.0f;
                                b.cameraLag = false;
                            } else {
                                b.targetArmLength = 8.0f;
                                b.cameraLag = true;
                            }
                        }
                    }

                    // Mouse wheel zoom in / out during gameplay
                    if (std::abs(ImGui::GetIO().MouseWheel) > 0.01f) {
                        float mw = ImGui::GetIO().MouseWheel;
                        std::string curMode = engine->getCameraMode();
                        auto boomView = registry.get().view<ecs::CameraBoomComponent>();
                        for (auto bEnt : boomView) {
                            auto& b = boomView.get<ecs::CameraBoomComponent>(bEnt);
                            if (mw > 0.1f) {
                                // Zoom in towards player
                                if (curMode == "thirdPerson") {
                                    b.targetArmLength -= 1.5f;
                                    if (b.targetArmLength <= 1.0f) {
                                        b.targetArmLength = 0.0f;
                                        b.isFirstPerson = true;
                                        b.cameraLag = false;
                                        engine->setCameraMode("firstPerson");
                                    }
                                }
                            } else if (mw < -0.1f) {
                                // Zoom out away from player
                                if (curMode == "firstPerson" || b.isFirstPerson) {
                                    b.targetArmLength = 4.0f;
                                    b.isFirstPerson = false;
                                    b.cameraLag = true;
                                    engine->setCameraMode("thirdPerson");
                                } else if (curMode == "thirdPerson") {
                                    b.targetArmLength = glm::clamp(b.targetArmLength + 1.5f, 2.0f, 40.0f);
                                }
                            }
                        }
                    }
                }
            }

            // In edit mode (paused), editor camera fly is ALWAYS enabled
            const bool isEditorCam = !engine || engine->isPaused() || (engine->getCameraMode() == "editor");

            if (state.isRmbNavigating && engine && isEditorCam) {
                float sens = engine->getPreferences().camMouseSensitivity;
                float yDelta = ImGui::GetIO().MouseDelta.y * sens;
                if (engine->getPreferences().invertY) yDelta = -yDelta;
                state.cameraYaw += ImGui::GetIO().MouseDelta.x * sens;
                state.cameraPitch = glm::clamp(state.cameraPitch - yDelta, -89.0f, 89.0f);
                const glm::vec3 cf = glm::normalize(glm::vec3(cosf(glm::radians(state.cameraYaw)) * cosf(glm::radians(state.cameraPitch)), sinf(glm::radians(state.cameraPitch)), sinf(glm::radians(state.cameraYaw)) * cosf(glm::radians(state.cameraPitch))));
                const glm::vec3 cr = glm::normalize(glm::cross(cf, glm::vec3(0.0f, 1.0f, 0.0f)));
                float speed = state.cameraFlySpeed * ImGui::GetIO().DeltaTime * (state.cameraSpeed / 4.0f);
                if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) speed *= 3.0f;
                if (ImGui::IsKeyDown(ImGuiKey_W)) state.cameraPosition += cf * speed;
                if (ImGui::IsKeyDown(ImGuiKey_S)) state.cameraPosition -= cf * speed;
                if (ImGui::IsKeyDown(ImGuiKey_A)) state.cameraPosition -= cr * speed;
                if (ImGui::IsKeyDown(ImGuiKey_D)) state.cameraPosition += cr * speed;
                if (ImGui::IsKeyDown(ImGuiKey_E)) state.cameraPosition.y += speed;
                if (ImGui::IsKeyDown(ImGuiKey_Q)) state.cameraPosition.y -= speed;
                engine->setCamera(state.cameraPosition, state.cameraYaw, state.cameraPitch, state.cameraFov);
            } else if (state.isRmbNavigating && engine && !isEditorCam) {
                // Orbit/look in 3rd person / 1st person
                float sens = engine->getPreferences().camMouseSensitivity;
                float yDelta = ImGui::GetIO().MouseDelta.y * sens;
                if (engine->getPreferences().invertY) yDelta = -yDelta;
                state.cameraYaw += ImGui::GetIO().MouseDelta.x * sens;
                state.cameraPitch = glm::clamp(state.cameraPitch - yDelta, -88.0f, 88.0f);
                engine->setCamera(state.cameraPosition, state.cameraYaw, state.cameraPitch, state.cameraFov);
            }
            if (state.isMmbPanning && engine) {
                state.cameraPosition -= (state.cameraRight * ImGui::GetIO().MouseDelta.x - state.cameraUp * ImGui::GetIO().MouseDelta.y) * 0.04f;
                engine->setCamera(state.cameraPosition, state.cameraYaw, state.cameraPitch, state.cameraFov);
            }
            if (state.isAltOrbiting && engine) {
                const glm::vec3 pivot = state.hasSelectedTransform ? registry.get().get<ecs::TransformComponent>(state.selectedEntity).position : (state.cameraPosition + state.cameraFront * 10.0f);
                const float orbitDist = std::max(0.5f, glm::length(state.cameraPosition - pivot));
                state.cameraYaw += ImGui::GetIO().MouseDelta.x * 0.25f;
                state.cameraPitch = glm::clamp(state.cameraPitch - ImGui::GetIO().MouseDelta.y * 0.25f, -89.0f, 89.0f);
                const glm::vec3 of = glm::normalize(glm::vec3(cosf(glm::radians(state.cameraYaw)) * cosf(glm::radians(state.cameraPitch)), sinf(glm::radians(state.cameraPitch)), sinf(glm::radians(state.cameraYaw)) * cosf(glm::radians(state.cameraPitch))));
                engine->setCamera(pivot - of * orbitDist, state.cameraYaw, state.cameraPitch, state.cameraFov);
            }
            if (isEditorCam && state.isHovered && std::abs(ImGui::GetIO().MouseWheel) > 0.01f && engine) {
                state.cameraPosition += state.cameraFront * (ImGui::GetIO().MouseWheel * 3.0f);
                engine->setCamera(state.cameraPosition, state.cameraYaw, state.cameraPitch, state.cameraFov);
            }

}
}
#endif
