#include "core/EditorPreferences.hpp"
#include "core/Logger.hpp"
#include <fstream>
#include <algorithm>

namespace engine::core {

    std::string EditorPreferences::getDefaultConfigPath() {
        return "Saved/Config/EditorPreferences.json";
    }

    nlohmann::json EditorPreferences::toJson() const {
        return nlohmann::json{
            {"language", language},
            {"ui", {
                {"uiScale", uiScale},
                {"showPlaceActors", showPlaceActors},
                {"showOutliner", showOutliner},
                {"showDetails", showDetails},
                {"showContentDrawer", showContentDrawer},
                {"showOutputLog", showOutputLog},
                {"showScriptStudio", showScriptStudio},
                {"userLeftPanelW", userLeftPanelW},
                {"userRightPanelW", userRightPanelW},
                {"userOutlinerH", userOutlinerH},
                {"userDrawerH", userDrawerH}
            }},
            {"viewport", {
                {"camFlySpeed", camFlySpeed},
                {"cameraSpeed", cameraSpeed},
                {"camMouseSensitivity", camMouseSensitivity},
                {"invertY", invertY},
                {"snapEnabled", snapEnabled},
                {"snapStep", snapStep},
                {"gizmoSpaceLocal", gizmoSpaceLocal}
            }},
            {"graphics", {
                {"targetFps", targetFps},
                {"vsync", vsync},
                {"antiAliasingMode", antiAliasingMode},
                {"resolutionScale", resolutionScale},
                {"autoViewportResolution", autoViewportResolution},
                {"shadowDistance", shadowDistance},
                {"bloomEnabled", bloomEnabled},
                {"bloomIntensity", bloomIntensity},
                {"bloomThreshold", bloomThreshold},
                {"gtaoEnabled", gtaoEnabled},
                {"gtaoRadius", gtaoRadius},
                {"gtaoIntensity", gtaoIntensity},
                {"rayTracingEnabled", rayTracingEnabled},
                {"exposure", exposure}
            }},
            {"audio", {
                {"masterVolume", masterVolume},
                {"sfxVolume", sfxVolume}
            }},
            {"general", {
                {"lastProjectPath", lastProjectPath},
                {"loadLastProjectOnStartup", loadLastProjectOnStartup},
                {"autoSavePreferences", autoSavePreferences}
            }}
        };
    }

    void EditorPreferences::fromJson(const nlohmann::json& j) {
        if (!j.is_object()) return;

        language = j.value("language", language);
        if (language.empty() || language.size() > 16 ||
            !std::all_of(language.begin(), language.end(), [](unsigned char ch) {
                return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
            })) {
            language = "en";
        }

        // Structured parsing
        if (j.contains("ui") && j["ui"].is_object()) {
            const auto& u = j["ui"];
            uiScale = u.value("uiScale", uiScale);
            showPlaceActors = u.value("showPlaceActors", showPlaceActors);
            showOutliner = u.value("showOutliner", showOutliner);
            showDetails = u.value("showDetails", showDetails);
            showContentDrawer = u.value("showContentDrawer", showContentDrawer);
            showOutputLog = u.value("showOutputLog", showOutputLog);
            showScriptStudio = u.value("showScriptStudio", showScriptStudio);
            userLeftPanelW = u.value("userLeftPanelW", userLeftPanelW);
            userRightPanelW = u.value("userRightPanelW", userRightPanelW);
            userOutlinerH = u.value("userOutlinerH", userOutlinerH);
            userDrawerH = u.value("userDrawerH", userDrawerH);
        } else {
            uiScale = j.value("uiScale", uiScale);
            showPlaceActors = j.value("showPlaceActors", showPlaceActors);
            showOutliner = j.value("showOutliner", showOutliner);
            showDetails = j.value("showDetails", showDetails);
            showContentDrawer = j.value("showContentDrawer", showContentDrawer);
            showOutputLog = j.value("showOutputLog", showOutputLog);
            showScriptStudio = j.value("showScriptStudio", showScriptStudio);
        }

        if (j.contains("viewport") && j["viewport"].is_object()) {
            const auto& v = j["viewport"];
            camFlySpeed = v.value("camFlySpeed", camFlySpeed);
            cameraSpeed = v.value("cameraSpeed", cameraSpeed);
            camMouseSensitivity = v.value("camMouseSensitivity", camMouseSensitivity);
            invertY = v.value("invertY", invertY);
            snapEnabled = v.value("snapEnabled", snapEnabled);
            snapStep = v.value("snapStep", snapStep);
            gizmoSpaceLocal = v.value("gizmoSpaceLocal", gizmoSpaceLocal);
        }

        if (j.contains("graphics") && j["graphics"].is_object()) {
            const auto& g = j["graphics"];
            targetFps = g.value("targetFps", targetFps);
            vsync = g.value("vsync", vsync);
            antiAliasingMode = g.value("antiAliasingMode", antiAliasingMode);
            resolutionScale = g.value("resolutionScale", resolutionScale);
            autoViewportResolution = g.value("autoViewportResolution", autoViewportResolution);
            shadowDistance = g.value("shadowDistance", shadowDistance);
            bloomEnabled = g.value("bloomEnabled", bloomEnabled);
            bloomIntensity = g.value("bloomIntensity", bloomIntensity);
            bloomThreshold = g.value("bloomThreshold", bloomThreshold);
            gtaoEnabled = g.value("gtaoEnabled", gtaoEnabled);
            gtaoRadius = g.value("gtaoRadius", gtaoRadius);
            gtaoIntensity = g.value("gtaoIntensity", gtaoIntensity);
            rayTracingEnabled = g.value("rayTracingEnabled", rayTracingEnabled);
            exposure = g.value("exposure", exposure);
        }

        if (j.contains("audio") && j["audio"].is_object()) {
            const auto& a = j["audio"];
            masterVolume = a.value("masterVolume", masterVolume);
            sfxVolume = a.value("sfxVolume", sfxVolume);
        }

        if (j.contains("general") && j["general"].is_object()) {
            const auto& gen = j["general"];
            lastProjectPath = gen.value("lastProjectPath", lastProjectPath);
            loadLastProjectOnStartup = gen.value("loadLastProjectOnStartup", loadLastProjectOnStartup);
            autoSavePreferences = gen.value("autoSavePreferences", autoSavePreferences);
        }

        // Clamp values to safe ranges
        uiScale = std::clamp(uiScale, 0.5f, 2.5f);
        camFlySpeed = std::clamp(camFlySpeed, 1.0f, 200.0f);
        cameraSpeed = std::clamp(cameraSpeed, 1, 8);
        camMouseSensitivity = std::clamp(camMouseSensitivity, 0.01f, 2.0f);
        snapStep = std::max(0.01f, snapStep);
        resolutionScale = std::clamp(resolutionScale, 0.25f, 2.5f);
        antiAliasingMode = std::clamp(antiAliasingMode, 0, 3);
        shadowDistance = std::clamp(shadowDistance, 10.0f, 500.0f);
        exposure = std::clamp(exposure, 0.1f, 10.0f);
        masterVolume = std::clamp(masterVolume, 0.0f, 1.0f);
        sfxVolume = std::clamp(sfxVolume, 0.0f, 1.0f);
    }

    bool EditorPreferences::saveToFile(const std::string& path) const {
        std::string targetPath = path.empty() ? getDefaultConfigPath() : path;
        try {
            std::filesystem::path p(targetPath);
            if (p.has_parent_path()) {
                std::filesystem::create_directories(p.parent_path());
            }
            std::ofstream file(p);
            if (!file.is_open()) {
                core::Logger::error("Failed to open preferences file for writing: " + targetPath);
                return false;
            }
            file << toJson().dump(4);
            file.close();
            core::Logger::info("Saved editor preferences to: " + targetPath);
            return true;
        } catch (const std::exception& e) {
            core::Logger::error(std::string("Exception saving preferences: ") + e.what());
            return false;
        }
    }

    bool EditorPreferences::loadFromFile(const std::string& path) {
        std::string targetPath = path.empty() ? getDefaultConfigPath() : path;
        try {
            if (!std::filesystem::exists(targetPath)) {
                // Check fallback path
                if (std::filesystem::exists("Config/EditorPreferences.json")) {
                    targetPath = "Config/EditorPreferences.json";
                } else {
                    return false; // No config file yet, use defaults
                }
            }
            std::ifstream file(targetPath);
            if (!file.is_open()) return false;
            nlohmann::json j = nlohmann::json::parse(file);
            file.close();
            fromJson(j);
            core::Logger::info("Loaded editor preferences from: " + targetPath);
            return true;
        } catch (const std::exception& e) {
            core::Logger::error(std::string("Failed to load preferences from ") + targetPath + ": " + e.what());
            return false;
        }
    }

} // namespace engine::core
