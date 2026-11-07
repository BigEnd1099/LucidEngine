#pragma once
#include <cstdint>

namespace engine::renderer {

    enum class RtDebugMode : uint32_t {
        None = 0,
        RawIndirect = 1,
        HistoryLength = 2,
        GeomNormals = 3,
        LinearDepth = 4,
        AmbientOcclusionOnly = 5,
        ReflectionsOnly = 6,
        Variance = 7
    };

    struct RtSettings {
        bool enabled{true};
        bool shadowsEnabled{true};
        bool aoEnabled{true};
        bool reflectionsEnabled{true};
        bool giEnabled{true};
        bool giHalfRes{true};
        float giIntensity{1.0f};
        int giBounces{2};
        int giSamples{1};
        int reflectionSamples{1};
        int maxHistory{32};
        float spatialStrength{1.0f};
        bool denoiseEnabled{true};
        bool svgfEnabled{true};
        RtDebugMode debugMode{RtDebugMode::None};
    };

} // namespace engine::renderer
