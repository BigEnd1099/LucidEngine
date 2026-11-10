#pragma once

#include <string_view>

namespace engine::renderer {

// English is the canonical language and the safe fallback for unknown locale IDs.
enum class EditorLanguage { English, Polish };

inline EditorLanguage& activeEditorLanguage() {
    static EditorLanguage language = EditorLanguage::English;
    return language;
}

inline void setEditorLanguage(std::string_view languageId) {
    activeEditorLanguage() = (languageId == "pl") ? EditorLanguage::Polish : EditorLanguage::English;
}

inline const char* editorText(const char* english, const char* polish) {
    return activeEditorLanguage() == EditorLanguage::Polish ? polish : english;
}

} // namespace engine::renderer
