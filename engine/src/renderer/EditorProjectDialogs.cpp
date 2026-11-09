#include "renderer/EditorProjectDialogs.hpp"

#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#endif

namespace engine::renderer {

#ifdef _WIN32
static std::string openProjectNativeDialog() {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Lucid / Unreal Project (*.uproject;*.json)\0*.uproject;*.json;project.json;game.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) {
        std::filesystem::path p(filename);
        return p.string();
    }
    return "";
}

static std::string saveProjectNativeDialog() {
    char filename[MAX_PATH] = "MyProject.uproject";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Unreal Project Descriptor (*.uproject)\0*.uproject\0Game Package (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameA(&ofn)) {
        std::filesystem::path p(filename);
        if (!p.has_extension()) {
            p += ".uproject";
        }
        return p.string();
    }
    return "";
}
#else
static std::string openProjectNativeDialog() { return ""; }
static std::string saveProjectNativeDialog() { return ""; }
#endi
#else
std::string openProjectNativeDialog() { return {}; }
std::string saveProjectNativeDialog() { return {}; }
#endif

} // namespace engine::renderer
