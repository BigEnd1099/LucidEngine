#pragma once
#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>
namespace engine::renderer {
struct EditorCameraNavigationState {
    bool& isRmbNavigating;
    bool& isMmbPanning;
    bool& isAltOrbiting;
    bool isHovered;
    float& cameraFlySpeed;
    int& cameraSpeed;
    bool hasSelectedTransform;
    entt::entity selectedEntity;
    glm::vec3& cameraPosition;
    glm::vec3& cameraFront;
    glm::vec3& cameraRight;
    glm::vec3& cameraUp;
    float& cameraYaw;
    float& cameraPitch;
    float cameraFov;
};
}
