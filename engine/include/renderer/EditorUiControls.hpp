#pragma once
#ifdef USE_GLFW
#include <glm/vec3.hpp>
#include "imgui.h"

namespace engine::renderer {

bool drawEditorVec3Control(const char* label, glm::vec3& values,
                           float resetValue = 0.0f, float columnWidth = 70.0f);

bool drawEditorSliderFloat(const char* label, float* v, float v_min, float v_max, float v_default,
                           const char* format = "%.2f");

bool drawEditorDragFloat(const char* label, float* v, float v_speed, float v_min, float v_max, float v_default,
                         const char* format = "%.2f");

} // namespace engine::renderer
#endif
