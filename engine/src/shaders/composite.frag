#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uHdrScene;
layout(set = 0, binding = 1) uniform sampler2D uBloom;
layout(set = 0, binding = 2) uniform sampler2D uAoTexture;

layout(push_constant) uniform CompositeConstants {
    vec4 params;   // x = bloomIntensity, y = exposure, z = aaMode (0: Off, 1: FXAA Fast, 2: FXAA Quality, 3: Ultra), w = bloomEnabled (1.0 or 0.0)
    vec4 aoParams; // x = aoEnabled (1.0 or 0.0), y = aoPower, z = texelSizeX, w = texelSizeY
} pcs;

// [UE5 TonemapCommon.ush: ACESFilm] ACES Filmic Tone Mapping Curve
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Samples HDR scene, applies Bloom & GTAO, and performs ACES Tonemap + Gamma
vec3 SampleToneMappedPixel(vec2 uv) {
    vec3 sceneColor = texture(uHdrScene, uv).rgb;
    
    // GTAO Contact Shadow
    if (pcs.aoParams.x > 0.5) {
        float ao = texture(uAoTexture, uv).r;
        float aoFactor = clamp(1.0 - ao * 0.75, 0.0, 1.0);
        sceneColor *= aoFactor;
    }
    
    // Bloom Dual-Kawase Glow
    if (pcs.params.w > 0.5) {
        float bloomIntensity = pcs.params.x > 0.0 ? pcs.params.x : 0.85;
        sceneColor += texture(uBloom, uv).rgb * bloomIntensity;
    }
    
    // Exposure
    sceneColor *= (pcs.params.y > 0.0 ? pcs.params.y : 1.0);
    
    // ACES Filmic Tone Mapping
    vec3 ldr = ACESFilm(sceneColor);
    
    // Gamma 2.2 Correction
    return pow(ldr, vec3(1.0 / 2.2));
}

float RGBToLuma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main() {
    float aaMode = pcs.params.z;
    vec2 ts = pcs.aoParams.zw;

    vec3 colorCenter = SampleToneMappedPixel(inUV);

    // If Anti-Aliasing is disabled (Mode 0), output direct tone-mapped pixel
    if (aaMode < 0.5 || ts.x <= 0.0 || ts.y <= 0.0) {
        outColor = vec4(colorCenter, 1.0);
        return;
    }

    // === NVIDIA FXAA 3.11 (Full-Screen Fast Approximate Anti-Aliasing) ===
    float lumaM = RGBToLuma(colorCenter);
    float lumaU = RGBToLuma(SampleToneMappedPixel(inUV + vec2(0.0, -ts.y)));
    float lumaD = RGBToLuma(SampleToneMappedPixel(inUV + vec2(0.0,  ts.y)));
    float lumaL = RGBToLuma(SampleToneMappedPixel(inUV + vec2(-ts.x, 0.0)));
    float lumaR = RGBToLuma(SampleToneMappedPixel(inUV + vec2( ts.x, 0.0)));

    float lumaMin = min(lumaM, min(min(lumaU, lumaD), min(lumaL, lumaR)));
    float lumaMax = max(lumaM, max(max(lumaU, lumaD), max(lumaL, lumaR)));
    float lumaRange = lumaMax - lumaMin;

    // Edge contrast threshold: if delta is below threshold, skip AA to preserve sharp textures
    float edgeThresholdMin = 0.0312;
    float edgeThresholdMax = 0.125;
    float edgeThreshold = max(edgeThresholdMin, lumaMax * edgeThresholdMax);

    if (lumaRange < edgeThreshold) {
        outColor = vec4(colorCenter, 1.0);
        return;
    }

    // 4 Corner diagonals for subpixel filter and gradient direction
    float lumaUL = RGBToLuma(SampleToneMappedPixel(inUV + vec2(-ts.x, -ts.y)));
    float lumaUR = RGBToLuma(SampleToneMappedPixel(inUV + vec2( ts.x, -ts.y)));
    float lumaDL = RGBToLuma(SampleToneMappedPixel(inUV + vec2(-ts.x,  ts.y)));
    float lumaDR = RGBToLuma(SampleToneMappedPixel(inUV + vec2( ts.x,  ts.y)));

    float lumaLPlusR = lumaL + lumaR;
    float lumaUPlusD = lumaU + lumaD;
    float lumaCorners = lumaUL + lumaUR + lumaDL + lumaDR;

    // Local edge orientation (horizontal vs vertical)
    float edgeH = abs(-2.0 * lumaL + lumaUL + lumaDL) + abs(-2.0 * lumaM + lumaU + lumaD) * 2.0 + abs(-2.0 * lumaR + lumaUR + lumaDR);
    float edgeV = abs(-2.0 * lumaU + lumaUL + lumaUR) + abs(-2.0 * lumaM + lumaL + lumaR) * 2.0 + abs(-2.0 * lumaD + lumaDL + lumaDR);
    bool isHorizontal = (edgeH >= edgeV);

    // Subpixel antialiasing filter
    float subpixelLuma = (2.0 * (lumaUPlusD + lumaLPlusR) + lumaCorners) / 12.0;
    float subpixelDiff = abs(subpixelLuma - lumaM);
    float subpixelBlend = clamp(subpixelDiff / lumaRange, 0.0, 1.0);
    subpixelBlend = smoothstep(0.0, 1.0, subpixelBlend);
    float subpixelFactor = (aaMode > 1.5) ? 0.75 : 0.50;
    float subpixelOffset = subpixelBlend * subpixelBlend * subpixelFactor;

    // Edge step normal
    float luma1 = isHorizontal ? lumaU : lumaL;
    float luma2 = isHorizontal ? lumaD : lumaR;
    float stepSign = (abs(luma1 - lumaM) >= abs(luma2 - lumaM)) ? -1.0 : 1.0;

    vec2 blendDir = isHorizontal ? vec2(0.0, ts.y * stepSign * (0.5 + subpixelOffset * 0.5))
                                 : vec2(ts.x * stepSign * (0.5 + subpixelOffset * 0.5), 0.0);

    vec3 blendedColor = SampleToneMappedPixel(inUV + blendDir);
    outColor = vec4(mix(colorCenter, blendedColor, 0.85), 1.0);
}
