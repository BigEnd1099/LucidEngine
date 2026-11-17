#pragma once
#include <cstdint>
#include <functional>

namespace engine::core {

    template<typename Tag>
    struct ResourceHandle {
        uint32_t id{0xFFFFFFFF};
        uint32_t generation{0};

        bool is_valid() const {
            return id != 0xFFFFFFFF;
        }

        static ResourceHandle<Tag> invalid() {
            return ResourceHandle<Tag>{0xFFFFFFFF, 0};
        }

        bool operator==(const ResourceHandle<Tag>& other) const {
            return id == other.id && generation == other.generation;
        }

        bool operator!=(const ResourceHandle<Tag>& other) const {
            return !(*this == other);
        }
    };

    struct MeshAssetTag {};
    struct TextureAssetTag {};
    struct MaterialAssetTag {};
    struct AudioAssetTag {};
    struct ShaderAssetTag {};

    using MeshHandle     = ResourceHandle<MeshAssetTag>;
    using TextureHandle  = ResourceHandle<TextureAssetTag>;
    using MaterialHandle = ResourceHandle<MaterialAssetTag>;
    using AudioHandle    = ResourceHandle<AudioAssetTag>;
    using ShaderHandle   = ResourceHandle<ShaderAssetTag>;

} // namespace engine::core

namespace std {
    template<typename Tag>
    struct hash<engine::core::ResourceHandle<Tag>> {
        size_t operator()(const engine::core::ResourceHandle<Tag>& handle) const noexcept {
            return (static_cast<size_t>(handle.id) << 32) ^ static_cast<size_t>(handle.generation);
        }
    };
}
