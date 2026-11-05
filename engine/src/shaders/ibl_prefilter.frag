#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube envCubemap;

layout(push_constant) uniform IblPush {
    int faceIndex;
    float roughness;
    float envResolution;
    float pad;
} pcs;

const float PI = 3.14159265359;

vec3 cubeFaceDirection(int face, vec2 uv) {
    float s = uv.x * 2.0 - 1.0;
    float t = uv.y * 2.0 - 1.0;
    if (face == 0) return normalize(vec3( 1.0, -t, -s));
    if (face == 1) return normalize(vec3(-1.0, -t,  s));
    if (face == 2) return normalize(vec3(  s,  1.0,  t));
    if (face == 3) return normalize(vec3(  s, -1.0, -t));
    if (face == 4) return normalize(vec3(  s, -t,  1.0));
    return normalize(vec3( -s, -t, -1.0));
}

float radicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), radicalInverseVdC(i));
}

vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-6);
}

void main() {
    vec3 N = cubeFaceDirection(pcs.faceIndex, fragUV);
    vec3 R = N;
    vec3 V = R;

    const uint SAMPLE_COUNT = 256u;
    vec3 prefilteredColor = vec3(0.0);
    float totalWeight = 0.0;
    float res = max(pcs.envResolution, 64.0);
    float saTexel = 4.0 * PI / (6.0 * res * res);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(Xi, N, pcs.roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float D = distributionGGX(NdotH, pcs.roughness);
            float pdf = (D * NdotH / max(4.0 * HdotV, 1e-4)) + 1e-4;
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 1e-4);
            float mipLevel = (pcs.roughness == 0.0) ? 0.0 : max(0.0, 0.5 * log2(saSample / saTexel));

            prefilteredColor += textureLod(envCubemap, L, mipLevel).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefilteredColor /= max(totalWeight, 1e-4);
    outColor = vec4(prefilteredColor, 1.0);
}
