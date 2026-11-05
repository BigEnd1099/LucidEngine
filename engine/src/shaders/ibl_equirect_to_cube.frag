#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D equirectMap;

layout(push_constant) uniform IblPush {
    int faceIndex;
    float roughness;
    float envResolution;
    float pad;
} pcs;

const vec2 INV_ATAN = vec2(0.15915494309, 0.31830988618);

// Zwraca kierunek 3D dla danej ściany cubemapy zgodnie ze specyfikacją Vulkan 1.3 (rozdz. 16.5.3)
vec3 cubeFaceDirection(int face, vec2 uv) {
    float s = uv.x * 2.0 - 1.0;
    float t = uv.y * 2.0 - 1.0;
    if (face == 0) return normalize(vec3( 1.0, -t, -s)); // +X
    if (face == 1) return normalize(vec3(-1.0, -t,  s)); // -X
    if (face == 2) return normalize(vec3(  s,  1.0,  t)); // +Y
    if (face == 3) return normalize(vec3(  s, -1.0, -t)); // -Y
    if (face == 4) return normalize(vec3(  s, -t,  1.0)); // +Z
    return normalize(vec3( -s, -t, -1.0));                // -Z
}

void main() {
    vec3 dir = cubeFaceDirection(pcs.faceIndex, fragUV);
    vec2 eqUV = vec2(atan(dir.z, dir.x) * INV_ATAN.x + 0.5, 0.5 - asin(clamp(dir.y, -1.0, 1.0)) * INV_ATAN.y);
    // textureLod(..., 0.0) eliminuje artefakty szwu na granicy atan(-pi/+pi)
    vec3 hdr = textureLod(equirectMap, eqUV, 0.0).rgb;
    hdr = min(hdr, vec3(64.0));
    outColor = vec4(max(hdr, vec3(0.0)), 1.0);
}
