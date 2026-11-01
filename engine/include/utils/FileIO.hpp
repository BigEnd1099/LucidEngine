#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include <filesystem>

namespace engine::utils {
    inline std::vector<char> readFile(const std::string& filename) {
        std::vector<std::string> searchPaths = {
            filename,
            "engine/build/Release/" + filename,
            "engine/build/Debug/" + filename,
            "engine/build/" + filename,
            "engine/src/shaders/" + filename,
            "build/Release/" + filename,
            "build/Debug/" + filename,
            "build/" + filename,
            "Release/" + filename,
            "Debug/" + filename,
            "../" + filename,
            "../../" + filename,
            "../engine/build/Release/" + filename,
            "../../engine/build/Release/" + filename
        };

        for (const auto& path : searchPaths) {
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            if (file.is_open()) {
                size_t fileSize = (size_t) file.tellg();
                std::vector<char> buffer(fileSize);
                file.seekg(0);
                file.read(buffer.data(), fileSize);
                file.close();
                return buffer;
            }
        }

        throw std::runtime_error("failed to open file in any known directory: " + filename);
    }
}
