#pragma once

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace engine::renderer {

// Runtime-loaded editor translations. English is the canonical fallback.
inline std::string& activeEditorLocale() {
    static std::string locale = "en";
    return locale;
}

inline std::map<std::string, std::string>& activeEditorTranslations() {
    static std::map<std::string, std::string> translations;
    return translations;
}

inline bool loadEditorLanguage(std::string_view locale) {
    namespace fs = std::filesystem;
    const std::string localeId(locale);
    if (localeId.empty() || localeId.size() > 16) return false;
    for (unsigned char ch : localeId) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) return false;
    }
    const fs::path candidates[] = {
        fs::path("language") / (localeId + ".json"),
        fs::path("engine") / "language" / (localeId + ".json"),
        fs::path("../language") / (localeId + ".json"),
        fs::path("../engine") / "language" / (localeId + ".json")
    };

    for (const auto& path : candidates) {
        try {
            if (!fs::exists(path)) continue;
            std::ifstream file(path);
            if (!file.is_open()) continue;

            const auto document = nlohmann::json::parse(file);
            if (!document.is_object() || !document.contains("translations") ||
                !document["translations"].is_object()) continue;

            std::map<std::string, std::string> loaded;
            for (auto it = document["translations"].begin(); it != document["translations"].end(); ++it) {
                if (it.value().is_string()) loaded.emplace(it.key(), it.value().get<std::string>());
            }

            activeEditorTranslations() = std::move(loaded);
            activeEditorLocale() = localeId;
            return true;
        } catch (...) {
            // Try the next known location; callers retain the previous locale on failure.
        }
    }
    return false;
}

inline bool setEditorLanguage(std::string_view locale) {
    if (loadEditorLanguage(locale)) return true;

    if (locale != "en" && loadEditorLanguage("en")) return false;

    // Never leave the previous locale active if even the English catalog is unavailable.
    activeEditorTranslations().clear();
    activeEditorLocale() = "en";
    return false;
}

// Prefer stable IDs so translation wording can evolve independently from source code.
inline const char* editorText(const char* key, const char* english, const char* legacyPolish) {
    const auto& translations = activeEditorTranslations();
    const auto it = translations.find(key ? key : "");
    if (it != translations.end()) return it->second.c_str();

    if (activeEditorLocale() == "pl" && legacyPolish) return legacyPolish;
    return english ? english : "";
}

// Compatibility overload for panels that have not yet migrated to stable IDs.
inline const char* editorText(const char* english, const char* legacyPolish = nullptr) {
    return editorText(english, english, legacyPolish);
}

} // namespace engine::renderer
