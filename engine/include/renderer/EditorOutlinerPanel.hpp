#pragma once
#ifdef USE_GLFW
#include "ecs/Registry.hpp"
#include <glm/glm.hpp>

namespace engine::core { class Engine; }

namespace engine::renderer {
void drawEditorOutlinerPanel(ecs::Registry& registry,
                             core::Engine* engine,
                             entt::entity& selectedEntity,
                             bool immersivePlay,
                             bool& showOutliner,
                             bool& showDetails,
                             float uiScale,
                             float leftPanelW,
                             float centerViewportW,
                             float topTotalH,
                             float outlinerH,
                             const glm::vec3& cameraPosition,
                             const glm::vec3& cameraForward,
                             float cameraYaw,
                             float cameraPitch,
                             float cameraFov);
}
#endif
