#include "core/Engine.hpp"
#include "network/DualTransportContracts.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

void logCrash(const std::string& msg, bool showBox = false) {
    std::ofstream logFile("crash.log", std::ios::app);
    if (logFile.is_open()) {
        logFile << msg << "\n";
        logFile.close();
    }
#ifdef _WIN32
    if (showBox) {
        MessageBoxA(NULL, msg.c_str(), "Engine Crash / Boot Error", MB_ICONERROR | MB_OK);
    }
#endif
}

#ifdef _WIN32
LONG WINAPI GlobalUnhandledExceptionFilter(EXCEPTION_POINTERS* ep) {
    std::ofstream f("crash.log", std::ios::app);
    if (f.is_open()) {
        f << "SEH FATAL CRASH: code 0x" << std::hex << ep->ExceptionRecord->ExceptionCode
          << " at address 0x" << ep->ExceptionRecord->ExceptionAddress << "\n";
        f.close();
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    SetUnhandledExceptionFilter(GlobalUnhandledExceptionFilter);
#endif

    std::set_terminate([]() {
        std::ofstream f("crash.log", std::ios::app);
        if (f.is_open()) {
            f << "std::terminate called!\n";
            f.close();
        }
        std::abort();
    });
    bool headless = false;
    bool standaloneGame = false;
    bool enableRemote = true;
    bool launchStudio = false;
    bool noStudio = false;
    bool vulkanWindow = false;
    std::string projectPath = "";
    std::string playPreset = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless") {
            headless = true;
            noStudio = true;
        } else if (arg == "--studio" || arg == "--web-ui") {
            launchStudio = true;
        } else if (arg == "--no-studio" || arg == "--no-ui") {
            noStudio = true;
        } else if (arg == "--no-remote" || arg == "--local-only") {
            enableRemote = false;
        } else if (arg == "--vulkan-window" || arg == "--window") {
            vulkanWindow = true;
        } else if (arg == "--game" || arg == "--standalone") {
            standaloneGame = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                projectPath = argv[++i];
            }
        } else if (arg == "--play" && i + 1 < argc) {
            playPreset = argv[++i];
            standaloneGame = true;
        }
    }

    if (!standaloneGame && std::filesystem::exists("game.json")) {
        standaloneGame = true;
        projectPath = "game.json";
    }

    // Default: In desktop editor mode, run headless backend (offscreen Vulkan GPU streaming)
    // and launch the unified 1:1 Studio UI Shell (so user has EXACTLY ONE unified window on desktop).
    // The GLFW window is only opened if specifically requested via --vulkan-window or --game.
    if (!standaloneGame && !vulkanWindow) {
        if (!noStudio && !headless) {
            launchStudio = true;
        }
        headless = true; // Offscreen Vulkan GPU streaming; UI shell embeds viewport directly
    }

    // Clear previous log
    std::ofstream clearLog("crash.log");
    if(clearLog.is_open()) {
        clearLog << "--- BOOT LOG ---\n";
        clearLog.close();
    }

    try {
        logCrash("STEP 1: Creating engine instance...");
        engine::core::Engine eng(9000, headless, enableRemote);
        
        if (standaloneGame) {
            eng.setStandaloneGameMode(true);
            if (!projectPath.empty()) {
                eng.loadProjectFromDisk(projectPath);
            } else if (!playPreset.empty()) {
                eng.resetScene(playPreset);
            }
            eng.playSimulation();
        }

        logCrash("STEP 2: Exporting type definitions...");
        eng.exportTypeDefinitions();

        if (launchStudio) {
#ifdef _WIN32
            // Use ShellExecuteA so handles (such as listening sockets) are NEVER inherited by the UI process
            if (std::filesystem::exists("node_modules\\electron\\dist\\electron.exe")) {
                ShellExecuteA(NULL, "open", "node_modules\\electron\\dist\\electron.exe", ".", NULL, SW_SHOWNORMAL);
            } else if (std::filesystem::exists("C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe")) {
                ShellExecuteA(NULL, "open", "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe", "--app=http://127.0.0.1:9000 --window-size=1920,1080", NULL, SW_SHOWNORMAL);
            } else if (std::filesystem::exists("C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe")) {
                ShellExecuteA(NULL, "open", "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe", "--app=http://127.0.0.1:9000 --window-size=1920,1080", NULL, SW_SHOWNORMAL);
            } else {
                ShellExecuteA(NULL, "open", "http://127.0.0.1:9000", NULL, NULL, SW_SHOWNORMAL);
            }
#endif
        }
        
        logCrash("STEP 3: Running engine...");
        eng.run();

        logCrash("STEP 4: Engine finished gracefully. isRunning: " + std::to_string(eng.isRunning()));
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        logCrash(std::string("Fatal Error: ") + e.what(), !headless);
        return 1;
    } catch (...) {
        std::cerr << "Unknown Fatal Error!" << std::endl;
        logCrash("Unknown Fatal Error!", !headless);
        return 1;
    }
    return 0;
}
