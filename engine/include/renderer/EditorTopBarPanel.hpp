#pragma once
#include <functional>
#include <string>
namespace engine::renderer {
struct EditorTopBarUiState {
    bool& immersivePlay;
    bool& showPreferencesWindow;
    bool& showPlaceActors;
    bool& showOutliner;
    bool& showDetails;
    bool& showContentDrawer;
    bool& showOutputLog;
    bool& showScriptStudio;
    float& uiScale;
    float& resolutionScale;
    int& antiAliasingMode;
    float& userLeftPanelW;
    float& userRightPanelW;
    float& userOutlinerH;
    float& userDrawerH;
    char* engineNotification;
    float& engineNotificationTimer;
    bool simulationSessionActive;
    std::function<void(bool)> saveProject;
    std::function<void(const std::string&)> loadProject;
};
}
