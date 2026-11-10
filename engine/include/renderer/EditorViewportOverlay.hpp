#pragma once
namespace engine::renderer {
enum class EditorGizmoMode { TRANSLATE = 0, ROTATE = 1, SCALE = 2 };
struct EditorViewportOverlayState {
    bool& immersivePlay;
    bool& showPlaceActors;
    bool& showOutliner;
    bool& showDetails;
    bool& snapEnabled;
    int& cameraSpeed;
    float& uiScale;
    EditorGizmoMode& gizmoMode;
    bool gizmoEnabled;
};
}
