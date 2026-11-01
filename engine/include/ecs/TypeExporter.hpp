#pragma once
#include <string>

namespace engine::ecs {
    class TypeExporter {
    public:
        static std::string generateTypeScriptDefinitions();
        static void exportToTypeScript(const std::string& filepath);
    };
}
