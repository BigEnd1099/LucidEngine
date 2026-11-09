#pragma once

#ifdef USE_GLFW
#include "imgui.h"

namespace engine::renderer {

enum class VectorIconType {
        Cube, Character, Pawn, Light, SkyLight, PlayerStart, TriggerBox, TriggerSphere,
        Star, Clock, Shapes, Sparkles, Folder, WarningTri, Eye, Translate, Rotate, Scale,
        Globe, Camera, Speaker, Wand, Pointer, Mountain, Leaf, Brush, Gear,
        Play, Pause, Stop, SaveDisk
    };

void applyEditorUiTheme();
void loadEditorUiFont(ImGuiIO& io);
void drawEditorVectorIcon(ImDrawList* drawList, ImVec2 center, float radius, VectorIconType type, ImU32 color);

} // namespace engine::renderer
#endif
