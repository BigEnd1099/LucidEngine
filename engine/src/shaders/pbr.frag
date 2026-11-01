#version 450

// ============================================================================
// Lucid Engine - High-Fidelity PBR Shader adapted from Unreal Engine 5.8
// Cook-Torrance GGX / Smith / Schlick BRDF + UE5 Grid + ACES Tone Mapping
// ============================================================================

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
    vec4 baseColorFactor;
    vec4 cameraPos;        // xyz = camera position in world space, w = time
    vec4 materialParams;   // x = metallic, y = roughness, z = isGround, w = emissive
} pcs;

const float PI = 3.14159265358979323846;

// [UE5 BRDF.ush: D_GGX] Normal Distribution Function
float D_GGX(float a2, float NoH) {
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d);
}

// [UE5 BRDF.ush: Vis_SmithJointApprox] Masking-Shadowing Function
float Vis_SmithJointApprox(float a2, float NoV, float NoL) {
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / max(Vis_SmithV + Vis_SmithL, 1e-5);
}

// [UE5 BRDF.ush: F_Schlick] Fresnel Approximation
vec3 F_Schlick(vec3 F0, float VoH) {
    float Fc = pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
    return F0 + (vec3(1.0) - F0) * Fc;
}

// [UE5 BRDF.ush: Diffuse_Burley] Energy-Conserving Disney/Burley Diffuse
vec3 Diffuse_Burley(vec3 diffColor, float Roughness, float NoV, float NoL, float VoH) {
    float FD90 = 0.5 + 2.0 * VoH * VoH * Roughness;
    float FdV = 1.0 + (FD90 - 1.0) * pow(clamp(1.0 - NoV, 0.0, 1.0), 5.0);
    float FdL = 1.0 + (FD90 - 1.0) * pow(clamp(1.0 - NoL, 0.0, 1.0), 5.0);
    return diffColor * ((1.0 / PI) * FdV * FdL);
}

// [UE5 TonemapCommon.ush: ACESFilm] ACES Filmic Tone Mapping Curve
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 N = normalize(fragNormal);
    // True Camera View Vector
    vec3 V = normalize(pcs.cameraPos.xyz - fragWorldPos);

    vec3 baseColor = fragColor;
    float metallic = clamp(pcs.materialParams.x, 0.0, 1.0);
    float roughness = clamp(pcs.materialParams.y, 0.04, 1.0);
    bool isGround = (pcs.materialParams.z > 0.5) || (N.y > 0.8 && abs(fragWorldPos.y) < 0.2);

    // Viewport background horizon color (matches clean UE5 Editor tone)
    vec3 horizonColor = vec3(0.06, 0.07, 0.09);

    if (isGround) {
        vec2 coord = fragWorldPos.xz;
        vec2 dcoord = fwidth(coord);
        
        // 1. Minor Grid (1 unit)
        vec2 grid = abs(fract(coord - 0.5) - 0.5) / max(dcoord, vec2(1e-4));
        float line = min(grid.x, grid.y);
        float lineAlpha = 1.0 - min(line, 1.0);

        // 2. Major Grid (5 units)
        vec2 majorCoord = coord * 0.2;
        vec2 dmajorCoord = fwidth(majorCoord);
        vec2 majorGrid = abs(fract(majorCoord - 0.5) - 0.5) / max(dmajorCoord, vec2(1e-4));
        float majorLine = min(majorGrid.x, majorGrid.y);
        float majorLineAlpha = 1.0 - min(majorLine, 1.0);

        // 3. Central coordinate axes (Red = X-axis, Cyan = Z-axis)
        float axisZ = 1.0 - min(abs(coord.x) / max(dcoord.x, 1e-4), 1.0); // Z-axis at X=0
        float axisX = 1.0 - min(abs(coord.y) / max(dcoord.y, 1e-4), 1.0); // X-axis at Z=0

        vec3 groundSurface = vec3(0.04, 0.045, 0.06);
        vec3 subGridColor   = vec3(0.09, 0.12, 0.17);
        vec3 majorGridColor = vec3(0.14, 0.22, 0.32);
        vec3 axisCyan       = vec3(0.0, 0.85, 1.0);
        vec3 axisRed        = vec3(1.0, 0.25, 0.35);

        baseColor = mix(groundSurface, subGridColor, lineAlpha * 0.45);
        baseColor = mix(baseColor, majorGridColor, majorLineAlpha * 0.75);
        baseColor = mix(baseColor, axisCyan, axisZ * 0.95);
        baseColor = mix(baseColor, axisRed, axisX * 0.95);

        // Distance fog fade into viewport horizon
        float dist = length(fragWorldPos.xz - pcs.cameraPos.xz);
        float fog = clamp((dist - 25.0) / 100.0, 0.0, 1.0);
        baseColor = mix(baseColor, horizonColor, fog);

        roughness = 0.35;
        metallic = 0.1;
    }

    float a = roughness * roughness;
    float a2 = a * a;

    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    vec3 diffColor = baseColor * (1.0 - metallic);

    // --- 1. Primary Directional Sun Light (Unreal Engine 5 Sun Sky) ---
    vec3 L = normalize(vec3(0.55, 0.85, 0.40));
    vec3 H = normalize(V + L);

    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float NoV = clamp(abs(dot(N, V)) + 1e-5, 0.0, 1.0);
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);

    float D = D_GGX(a2, NoH);
    float Vis = Vis_SmithJointApprox(a2, NoV, NoL);
    vec3 F = F_Schlick(F0, VoH);
    vec3 specular = D * Vis * F;
    vec3 diffuse = Diffuse_Burley(diffColor, roughness, NoV, NoL, VoH);

    vec3 sunColor = vec3(1.0, 0.97, 0.92) * 3.4;
    vec3 directLighting = (diffuse + specular) * sunColor * NoL;

    // --- 2. Secondary Fill Light (Soft cool reflection) ---
    vec3 fillDir = normalize(vec3(-0.6, 0.4, -0.5));
    float fillNoL = clamp(dot(N, fillDir), 0.0, 1.0);
    vec3 fillH = normalize(V + fillDir);
    float fillVoH = clamp(dot(V, fillH), 0.0, 1.0);
    vec3 fillLight = Diffuse_Burley(diffColor, roughness, NoV, fillNoL, fillVoH) * vec3(0.2, 0.35, 0.55) * fillNoL * 1.5;

    // --- 3. Dual-Hemisphere Ambient (Rayleigh Sky Top + Ground Bounce) ---
    vec3 skyRadiance = vec3(0.15, 0.22, 0.35);
    vec3 groundRadiance = vec3(0.08, 0.07, 0.06);
    float skyWeight = 0.5 + 0.5 * N.y;
    vec3 ambientLighting = diffColor * mix(groundRadiance, skyRadiance, skyWeight);

    // --- 4. Emissive Component ---
    vec3 emissive = baseColor * pcs.materialParams.w * 2.5;

    // Total High Dynamic Range Radiance
    vec3 hdrColor = directLighting + fillLight + ambientLighting + emissive;

    // ACES Filmic Tone Mapping & Gamma Correction
    vec3 ldrColor = ACESFilm(hdrColor);
    ldrColor = pow(ldrColor, vec3(1.0 / 2.2));

    outColor = vec4(ldrColor, 1.0);
}
