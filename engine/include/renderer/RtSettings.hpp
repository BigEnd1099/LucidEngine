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
        Variance = 7,
        RawSpecularCaustics = 8,
        CausticHitDensity = 9,
        TemporalIndirect = 10
    };

    struct RtSettings {
        bool enabled{true};
        bool shadowsEnabled{true};
        bool aoEnabled{true};
        bool reflectionsEnabled{true};
        bool giEnabled{true};
        bool giHalfRes{true};
        bool giCheckerboard{true};
        float giIntensity{1.0f};
        int giBounces{2};
        int reflectionBounces{2};
        bool reflectionInfiniteBounces{false};
        float reflectionMaxRoughness{0.60f};
        float reflectionMinThroughput{0.06f};
        int giSamples{1};
        int reflectionSamples{1};
        int maxHistory{32};
        float spatialStrength{1.0f};
        bool denoiseEnabled{true};
        bool giTemporalFilterEnabled{true};
        bool giHistoryClampEnabled{true};
        bool giTemporalFireflyFilterEnabled{true};
        bool giTraceFireflyClampEnabled{true};
        bool giAtrousPass1Enabled{true};
        bool giAtrousPass2Enabled{true};
        bool giAtrousPass3Enabled{true};
        bool giAtrousPass4Enabled{true};
        bool svgfEnabled{true};
        bool reflectionTemporalFilterEnabled{true};
        bool reflectionOutlierFilterEnabled{true};
        bool reflectionSpatialFilterEnabled{true};
        RtDebugMode debugMode{RtDebugMode::None};

        // 4 RT Reflection Optimization Variants:
        bool reflectionCheckerboard{true};      // Variant 1: Roughness-adaptive checkerboard (roughness > 0.25)
        bool reflectionStochasticLights{true};  // Variant 2: Stochastic 1-light direct illumination on secondary bounces
        bool reflectionPrimaryStochasticLights{false}; // Optional PR #49 optimization for the primary reflection hit
        bool reflectionLodBias{true};           // Variant 3: Mip LOD bias increasing with bounce depth
        bool reflectionHalfRes{false};          // Variant 4: Global half-resolution reflections with bilateral filter
        bool reflectionShowLightBulbs{true};    // Show light sources in specular reflections (spotlights directional cone, point/area lights, default true)
        bool reflectionDenoiseMirrors{false};   // Optional planar bilateral spatial filter for mirrors (roughness < 0.035, default false)
    };

} // namespace engine::renderer
