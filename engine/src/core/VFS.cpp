#include "core/VFS.hpp"
#include <fstream>
#include <algorithm>
#include <cstring>

namespace engine::core {

    VirtualFileSystem& VirtualFileSystem::get() {
        static VirtualFileSystem instance;
        return instance;
    }

    VirtualFileSystem::VirtualFileSystem() : m_passthrough(true) {
    }

    uint64_t VirtualFileSystem::hash_path(const std::string& path) {
        // FNV-1a 64-bit
        uint64_t hash = 14695981039346656037ULL;
        for (char c : path) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    std::string VirtualFileSystem::normalize_path(const std::string& path) {
        std::string result = path;
        std::replace(result.begin(), result.end(), '\\', '/');
        // convert to lowercase
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (!result.empty() && result[0] == '/') {
            result.erase(0, 1);
        }
        return result;
    }

    bool VirtualFileSystem::mount(const std::string& pakFilePath, const std::string& mountPoint) {
        (void)mountPoint;
        std::lock_guard<std::mutex> lock(m_mutex);

        std::ifstream file(pakFilePath, std::ios::binary);
        if (!file.is_open()) return false;

        LucidPakHeader header{};
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (file.gcount() < static_cast<std::streamsize>(sizeof(header))) return false;
        if (header.magic != 0x4B504B4C) return false;

        std::vector<LucidPakEntryOnDisk> diskEntries(header.entryCount);
        file.read(reinterpret_cast<char*>(diskEntries.data()), header.entryCount * sizeof(LucidPakEntryOnDisk));
        if (file.gcount() < static_cast<std::streamsize>(header.entryCount * sizeof(LucidPakEntryOnDisk))) {
            return false;
        }

        for (const auto& de : diskEntries) {
            std::string entryPath(de.path);
            std::string norm = normalize_path(entryPath);
            uint64_t h = hash_path(norm);

            VfsEntry entry{};
            entry.virtualPath = norm;
            entry.offset = de.offset;
            entry.compressedSize = de.compressedSize;
            entry.uncompressedSize = de.uncompressedSize;
            entry.sourcePakFile = pakFilePath;

            m_entries[h] = entry;
        }

        m_mountedPaks.push_back(pakFilePath);
        return true;
    }

    void VirtualFileSystem::unmount(const std::string& mountPoint) {
        (void)mountPoint;
        clear();
    }

    bool VirtualFileSystem::exists(const std::string& virtualPath) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string norm = normalize_path(virtualPath);
        uint64_t h = hash_path(norm);
        if (m_entries.find(h) != m_entries.end()) {
            return true;
        }
        if (m_passthrough) {
            std::ifstream test(virtualPath);
            return test.good();
        }
        return false;
    }

    std::optional<std::vector<uint8_t>> VirtualFileSystem::read_binary(const std::string& virtualPath) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string norm = normalize_path(virtualPath);
        uint64_t h = hash_path(norm);

        auto it = m_entries.find(h);
        if (it != m_entries.end()) {
            const auto& entry = it->second;
            std::ifstream file(entry.sourcePakFile, std::ios::binary);
            if (!file.is_open()) return std::nullopt;

            file.seekg(entry.offset, std::ios::beg);
            std::vector<uint8_t> buffer(entry.compressedSize);
            file.read(reinterpret_cast<char*>(buffer.data()), entry.compressedSize);
            if (file.gcount() != static_cast<std::streamsize>(entry.compressedSize)) {
                return std::nullopt;
            }
            return buffer;
        }

        // Native filesystem passthrough
        if (m_passthrough) {
            std::ifstream file(virtualPath, std::ios::binary | std::ios::ate);
            if (file.is_open()) {
                auto size = file.tellg();
                if (size >= 0) {
                    file.seekg(0, std::ios::beg);
                    std::vector<uint8_t> buffer(static_cast<size_t>(size));
                    file.read(reinterpret_cast<char*>(buffer.data()), size);
                    return buffer;
                }
            }
        }

        return std::nullopt;
    }

    std::optional<std::string> VirtualFileSystem::read_text(const std::string& virtualPath) const {
        auto bin = read_binary(virtualPath);
        if (!bin) return std::nullopt;
        return std::string(bin->begin(), bin->end());
    }

    bool VirtualFileSystem::create_pak(const std::string& pakFilePath,
                                       const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
        std::ofstream out(pakFilePath, std::ios::binary);
        if (!out.is_open()) return false;

        LucidPakHeader header{};
        header.magic = 0x4B504B4C;
        header.version = 2;
        header.flags = 0;
        header.entryCount = static_cast<uint32_t>(files.size());

        // Header placeholder
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));

        // Compute offsets and write entries placeholder
        std::vector<LucidPakEntryOnDisk> entries(files.size());
        uint64_t dataOffset = sizeof(LucidPakHeader) + files.size() * sizeof(LucidPakEntryOnDisk);

        for (size_t i = 0; i < files.size(); ++i) {
            std::string norm = normalize_path(files[i].first);
            entries[i].pathHash = hash_path(norm);
            entries[i].offset = dataOffset;
            entries[i].compressedSize = files[i].second.size();
            entries[i].uncompressedSize = files[i].second.size();
            size_t copyLen = std::min(norm.size(), sizeof(entries[i].path) - 1);
            std::memcpy(entries[i].path, norm.data(), copyLen);
            entries[i].path[copyLen] = '\0';

            dataOffset += files[i].second.size();
        }

        out.write(reinterpret_cast<const char*>(entries.data()), entries.size() * sizeof(LucidPakEntryOnDisk));

        // Write file payloads
        for (const auto& file : files) {
            if (!file.second.empty()) {
                out.write(reinterpret_cast<const char*>(file.second.data()), file.second.size());
            }
        }

        return true;
    }

    std::vector<std::string> VirtualFileSystem::list_mounted_files() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> result;
        result.reserve(m_entries.size());
        for (const auto& [hash, entry] : m_entries) {
            result.push_back(entry.virtualPath);
        }
        return result;
    }

    void VirtualFileSystem::clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.clear();
        m_mountedPaks.clear();
    }

} // namespace engine::core
