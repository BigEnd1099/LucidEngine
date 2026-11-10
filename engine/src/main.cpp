#include "core/Engine.hpp"
#include "network/DualTransportContracts.hpp"
#include <iostream>
#include <algorithm>
#include <fstream>
#include <string>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
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
    bool benchmarkMode = false;
    std::string projectPath = "";
    std::string playPreset = "";
    int wsPort = 9000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--port" && i + 1 < argc) {
            wsPort = std::clamp(std::stoi(argv[++i]), 1, 65535);
        } else if (arg == "--benchmark") {
            benchmarkMode = true;
        } else if (arg == "--studio" || arg == "--web-ui") {
            launchStudio = true;
        } else if (arg == "--no-remote" || arg == "--local-only") {
            enableRemote = false;
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

    std::string exeName = "";
    if (argc > 0 && argv[0]) {
        std::filesystem::path p(argv[0]);
        exeName = p.stem().string();
        for (char& c : exeName) c = static_cast<char>(std::tolower(c));
    }
    const bool isEngineEditor = (exeName == "engine" || exeName == "lucidengine" || exeName.empty());

    // Only auto-launch standalone mode if this is an exported game executable (NOT the engine editor itself)
    if (!standaloneGame && !isEngineEditor && std::filesystem::exists("game.json")) {
        standaloneGame = true;
        projectPath = "game.json";
    }

    // Clear previous log
    std::ofstream clearLog("crash.log");
    if(clearLog.is_open()) {
        clearLog << "--- BOOT LOG ---\n";
        clearLog.close();
    }

    try {
        logCrash("STEP 1: Creating engine instance...");
        engine::core::Engine eng(wsPort, headless, enableRemote, benchmarkMode);
        
        if (standaloneGame) {
            eng.setStandaloneGameMode(true);
            if (!projectPath.empty()) {
                eng.loadCurrentProject(projectPath);
            } else if (!playPreset.empty()) {
                eng.resetScene(playPreset);
            }
            eng.playSimulation();
        }

        logCrash("STEP 2: Exporting type definitions...");
        eng.exportTypeDefinitions();

        if (launchStudio) {
#ifdef _WIN32
            const char* chromePath = "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
            const char* edgePath = "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
            if (std::filesystem::exists(chromePath)) {
                std::system("start \"\" \"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe\" --app=http://127.0.0.1:9000 --window-size=1920,1080");
            } else if (std::filesystem::exists(edgePath)) {
                std::system("start \"\" \"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe\" --app=http://127.0.0.1:9000 --window-size=1920,1080");
            } else {
                std::system("start http://127.0.0.1:9000");
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
