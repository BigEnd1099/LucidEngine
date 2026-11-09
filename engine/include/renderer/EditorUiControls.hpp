#pragma once
#ifdef USE_GLFW
#include <glm/vec3.hpp>
#include "imgui.h"
namespace engine::renderer {
bool drawEditorVec3Control(const char* label, glm::vec3& values,
                           float resetValue = 0.0f, float columnWidth = 70.0f);
}
#endif
