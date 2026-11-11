#version 460
layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D u_SceneColor;
layout(set = 0, binding = 1) uniform sampler2D u_Depth;
layout(set = 0, binding = 2) uniform sampler2DArray u_ShadowMap;

layout(push_constant) uniform PushConstants {
    mat4 invViewProj;
    vec4 cascadeSplits;
    uint debugMode; // 0 = Cascade colors, 1 = Raw shadow depth, 2 = Bias acne heatmap
    uint padding[3];
} pc;

void main() {
    vec3 sceneColor = texture(u_SceneColor, v_TexCoord).rgb;
    float depth = texture(u_Depth, v_TexCoord).r;

    if (depth >= 1.0) {
        outColor = vec4(sceneColor, 1.0);
        return;
    }

    // Reconstruct view-space depth
    vec4 clip = vec4(v_TexCoord * 2.0 - 1.0, depth, 1.0);
    vec4 worldH = pc.invViewProj * clip;
    vec3 worldPos = worldH.xyz / worldH.w;

    float viewDepth = abs(worldH.w);

    // Determine cascade index based on view depth
    int cascadeIndex = 0;
    if (viewDepth > pc.cascadeSplits[0]) cascadeIndex = 1;
    if (viewDepth > pc.cascadeSplits[1]) cascadeIndex = 2;
    if (viewDepth > pc.cascadeSplits[2]) cascadeIndex = 3;

    vec3 cascadeTint;
    switch (cascadeIndex) {
        case 0: cascadeTint = vec3(1.0, 0.2, 0.2); break; // Red: Near cascade
        case 1: cascadeTint = vec3(0.2, 1.0, 0.2); break; // Green: Mid-near
        case 2: cascadeTint = vec3(0.2, 0.4, 1.0); break; // Blue: Mid-far
        case 3: cascadeTint = vec3(1.0, 1.0, 0.2); break; // Yellow: Far cascade
        default: cascadeTint = vec3(0.8); break;
    }

    if (pc.debugMode == 0) {
        // Cascade overlay
        vec3 finalColor = mix(sceneColor, cascadeTint, 0.45);
        outColor = vec4(finalColor, 1.0);
    } else if (pc.debugMode == 1) {
        // Raw shadow map visualization for current cascade
        float shadowVal = texture(u_ShadowMap, vec3(v_TexCoord, float(cascadeIndex))).r;
        outColor = vec4(vec3(shadowVal), 1.0);
    } else {
        // Acne / slope bias diagnostic
        float acneDiagnostic = fract(viewDepth * 10.0);
        outColor = vec4(mix(cascadeTint, vec3(acneDiagnostic), 0.5), 1.0);
    }
}
