#pragma once
#include <string>
namespace engine::renderer {
struct EditorPreferencesUiState {
    bool& showPreferencesWindow;
    bool& showScriptStudio;
    bool& showPlaceActors;
    bool& showOutliner;
    bool& showDetails;
    bool& showContentDrawer;
    bool& showOutputLog;
    float& uiScale;
    float& userLeftPanelW;
    float& userRightPanelW;
    float& userOutlinerH;
    float& userDrawerH;
    float& camFlySpeed;
    int& cameraSpeed;
    bool& snapEnabled;
    float& snapStep;
    bool& gizmoSpaceLocal;
    std::string& currentProjectPath;
    char* engineNotification;
    float& engineNotificationTimer;
};
} // namespace engine::renderer
