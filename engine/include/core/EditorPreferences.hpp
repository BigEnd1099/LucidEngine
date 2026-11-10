#pragma once
#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string_view>

namespace engine::core {

    struct EditorPreferences {\n        // UI language: "en" (default) or "pl".\n        std::string language{"en"};
        // --- UI Layout & Display ---
        float uiScale{1.0f};
        bool showPlaceActors{true};
        bool showOutliner{true};
        bool showDetails{true};
        bool showContentDrawer{false};
        bool showOutputLog{false};
        bool showScriptStudio{false};

        float userLeftPanelW{260.0f};
        float userRightPanelW{310.0f};
        float userOutlinerH{240.0f};
        float userDrawerH{220.0f};

        // --- Viewport & Camera Navigation ---
        float camFlySpeed{25.0f};
        int cameraSpeed{4};               // 1..8
        float camMouseSensitivity{0.20f};
        bool invertY{false};
        bool snapEnabled{false};
        float snapStep{0.5f};
        bool gizmoSpaceLocal{false};

        // --- Rendering & Graphics ---
        int targetFps{0};                 // 0 = unlimited, 30, 60, 120, 240
        bool vsync{true};
        int antiAliasingMode{1};          // 0=Off, 1=Fast, 2=Quality, 3=Ultra
        float resolutionScale{1.0f};
        bool autoViewportResolution{true};
        float shadowDistance{100.0f};
        bool bloomEnabled{true};
        float bloomIntensity{0.65f};
        float bloomThreshold{1.0f};
        bool gtaoEnabled{true};
        float gtaoRadius{0.75f};
        float gtaoIntensity{1.15f};
        bool rayTracingEnabled{false};
        float exposure{1.0f};

        // --- Audio ---
        float masterVolume{1.0f};
        float sfxVolume{1.0f};

        // --- General & Projects ---
        std::string lastProjectPath{"projects/MyGame"};
        bool loadLastProjectOnStartup{true};
        bool autoSavePreferences{true};

        // Serialization
        nlohmann::json toJson() const;
        void fromJson(const nlohmann::json& j);

        bool saveToFile(const std::string& path = "") const;
        bool loadFromFile(const std::string& path = "");

        static std::string getDefaultConfigPath();
    };

} // namespace engine::core
