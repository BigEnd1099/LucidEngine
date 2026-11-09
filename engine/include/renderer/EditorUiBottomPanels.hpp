#pragma once
#ifdef USE_GLFW
namespace engine::renderer {
void drawEditorBottomPanels(bool immersivePlay,
                            bool& showContentDrawer,
                            bool& showOutputLog,
                            float screenW,
                            float screenH,
                            float bottomTotalH,
                            float drawerH,
                            float bottomStatusH,
                            float uiScale,
                            float frameRate,
                            unsigned int renderWidth,
                            unsigned int renderHeight);
}
#endif
