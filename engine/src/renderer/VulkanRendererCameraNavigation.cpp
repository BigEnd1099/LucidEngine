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
            if (state.isRmbNavigating && engine) {
                float sens = engine->getPreferences().camMouseSensitivity;
                float yDelta = ImGui::GetIO().MouseDelta.y * sens;
                if (engine->getPreferences().invertY) yDelta = -yDelta;
                state.cameraYaw += ImGui::GetIO().MouseDelta.x * sens;
                state.cameraPitch = glm::clamp(state.cameraPitch - yDelta, -89.0f, 89.0f);
                const glm::vec3 cf = glm::normalize(glm::vec3(cosf(glm::radians(state.cameraYaw)) * cosf(glm::radians(state.cameraPitch)), sinf(glm::radians(state.cameraPitch)), sinf(glm::radians(state.cameraYaw)) * cosf(glm::radians(state.cameraPitch))));
                const glm::vec3 cr = glm::normalize(glm::cross(cf, glm::vec3(0.0f, 1.0f, 0.0f)));
                float speed = s_camFlySpeed * ImGui::GetIO().DeltaTime * (s_cameraSpeed / 4.0f);
                if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) speed *= 3.0f;
                if (ImGui::IsKeyDown(ImGuiKey_W)) state.cameraPosition += cf * speed;
                if (ImGui::IsKeyDown(ImGuiKey_S)) state.cameraPosition -= cf * speed;
                if (ImGui::IsKeyDown(ImGuiKey_A)) state.cameraPosition -= cr * speed;
                if (ImGui::IsKeyDown(ImGuiKey_D)) state.cameraPosition += cr * speed;
                if (ImGui::IsKeyDown(ImGuiKey_E)) state.cameraPosition.y += speed;
                if (ImGui::IsKeyDown(ImGuiKey_Q)) state.cameraPosition.y -= speed;
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
            if (state.isHovered && std::abs(ImGui::GetIO().MouseWheel) > 0.01f && engine) {
                engine->setCamera(state.cameraPosition + state.cameraFront * (ImGui::GetIO().MouseWheel * 3.0f), state.cameraYaw, state.cameraPitch, state.cameraFov);
            }

}
}
#endif
