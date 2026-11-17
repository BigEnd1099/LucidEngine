#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include <mutex>
#include <cstdint>

namespace engine::core {

    #pragma pack(push, 1)
    struct LucidPakHeader {
        uint32_t magic{0x4B504B4C}; // "LKPK"
        uint32_t version{2};
        uint32_t flags{0};          // bit 0: encrypted
        uint32_t entryCount{0};
        uint8_t  nonce[24]{0};
    };

    struct LucidPakEntryOnDisk {
        uint64_t pathHash{0};
        uint64_t offset{0};
        uint64_t compressedSize{0};
        uint64_t uncompressedSize{0};
        char     path[128]{0};
    };
    #pragma pack(pop)

    struct VfsEntry {
        std::string virtualPath;
        uint64_t offset{0};
        uint64_t compressedSize{0};
        uint64_t uncompressedSize{0};
        std::string sourcePakFile;
    };

    class VirtualFileSystem {
    public:
        static VirtualFileSystem& get();

        VirtualFileSystem();
        ~VirtualFileSystem() = default;

        // Mount a .lucidpak archive
        bool mount(const std::string& pakFilePath, const std::string& mountPoint = "/");
        void unmount(const std::string& mountPoint = "/");

        // Set whether to fallback to native disk if not in pak
        void set_passthrough(bool enabled) { m_passthrough = enabled; }
        bool is_passthrough() const { return m_passthrough; }

        // Query & Reading
        bool exists(const std::string& virtualPath) const;
        std::optional<std::vector<uint8_t>> read_binary(const std::string& virtualPath) const;
        std::optional<std::string> read_text(const std::string& virtualPath) const;

        // Tooling: Build a .lucidpak file
        static bool create_pak(const std::string& pakFilePath,
                               const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files);

        std::vector<std::string> list_mounted_files() const;
        void clear();

    private:
        static uint64_t hash_path(const std::string& path);
        static std::string normalize_path(const std::string& path);

        mutable std::mutex m_mutex;
        std::unordered_map<uint64_t, VfsEntry> m_entries;
        std::vector<std::string> m_mountedPaks;
        bool m_passthrough{true};
    };

} // namespace engine::core
