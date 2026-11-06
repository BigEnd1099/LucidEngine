#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrSceneTex;
layout(set = 0, binding = 1) uniform sampler2D bloomTex;

layout(push_constant) uniform CompositePush {
    vec4 params;  // x = bloomIntensity, y = exposure, z = bloomEnabled, w = frameIndex
    vec4 params2; // x = useFxaa (1.0 = true, 0.0 = false)
} pcs;

const float MAX_HDR_VALUE = 65504.0;

// Macierze przestrzeni barw ACES (Stephen Hill RRT + ODT Fit: sRGB -> AP1 -> RRT+ODT -> sRGB)
const mat3 ACES_INPUT_MAT = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777
);

const mat3 ACES_OUTPUT_MAT = mat3(
     1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602
);

vec3 sanitizeHDR(vec3 c) {
    if (any(isnan(c)) || any(isinf(c))) return vec3(0.0);
    return clamp(c, vec3(0.0), vec3(MAX_HDR_VALUE));
}

vec3 rrtAndOdtFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 acesFitted(vec3 color) {
    color = max(ACES_INPUT_MAT * color, vec3(0.0));
    color = rrtAndOdtFit(color);
    color = ACES_OUTPUT_MAT * color;
    return clamp(color, 0.0, 1.0);
}

// Dokładna krzywa elektro-optyczna IEC 61966-2-1 sRGB
vec3 linearToSRGB(vec3 x) {
    bvec3 cutoff = lessThanEqual(x, vec3(0.0031308));
    vec3 lower = x * 12.92;
    vec3 higher = 1.055 * pow(max(x, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(higher, lower, cutoff);
}

float ignDither(vec2 pos, float frame) {
    return fract(52.9829189 * fract(dot(pos + mod(frame, 64.0) * vec2(5.588238, 5.588238), vec2(0.06711056, 0.00583715)))) - 0.5;
}

void main() {
    vec2 sceneTexel = 1.0 / vec2(max(textureSize(hdrSceneTex, 0), ivec2(1)));
    vec2 sceneUV = clamp(fragUV, sceneTexel * 0.5, vec2(1.0) - sceneTexel * 0.5);
    vec3 hdr = sanitizeHDR(texture(hdrSceneTex, sceneUV).rgb);

    if (pcs.params.z > 0.5) {
        vec2 bloomTexel = 1.0 / vec2(max(textureSize(bloomTex, 0), ivec2(1)));
        vec2 bloomUV = clamp(fragUV, bloomTexel * 0.5, vec2(1.0) - bloomTexel * 0.5);
        vec3 bloom = sanitizeHDR(texture(bloomTex, bloomUV).rgb);
        hdr += bloom * max(pcs.params.x, 0.0);
    }

    hdr *= max(pcs.params.y, 0.01);
    vec3 mapped = acesFitted(hdr);
    vec3 srgb = linearToSRGB(mapped);

    // Dithering 8-bit zapobiegający pasmowaniu (banding) na ciemnym niebie i mgle
    srgb = clamp(srgb + ignDither(gl_FragCoord.xy, pcs.params.w) / 255.0, 0.0, 1.0);

    float luma = dot(srgb, vec3(0.299, 0.587, 0.114));
    float outAlpha = (pcs.params2.x > 0.5) ? luma : 1.0;
    outColor = vec4(srgb, outAlpha);
}