#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragTangent;

struct OccluderData {
    vec4 centerRadius;
    vec4 halfExtents;
};

layout(set = 0, binding = 1) uniform SceneUbo {
    mat4 viewProj;
    mat4 view;
    mat4 invViewProj;
    mat4 cascadeLightSpace[4];
    vec4 cascadeSplits;
    vec4 cascadeRadii;
    vec4 cameraPos;
    vec4 sunDirection;
    vec4 sunPosition;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 fogParams;
    vec4 fogColor;
    vec4 screenParams;
    vec4 lightsPos[8];
    vec4 lightsCol[8];
    vec4 lightsDir[8];
    vec4 lightsParams[8];
    vec4 pointShadowParams;
    vec4 bounceParams;
    vec4 bounceColor;
    int numLights;
    int numOccluders;
    int frameIndex;
    int skyboxMode;
    OccluderData occluders[32];
} ubo;

// 128-bajtowy blok PushConstants (zgodny ze 100% kart AMD/NVIDIA/Intel)
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 normalCol0;      // xyz = kolumna 0 macierzy normalnych, w = metallic
    vec4 normalCol1;      // xyz = kolumna 1 macierzy normalnych, w = roughness
    vec4 normalCol2;      // xyz = kolumna 2 macierzy normalnych, w = emissiveIntensity
    vec4 baseColorFactor; // rgb = baseColor, a = aoOverride
} pcs;

void main() {
    vec4 worldPos = pcs.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    mat3 normalMat = mat3(pcs.normalCol0.xyz, pcs.normalCol1.xyz, pcs.normalCol2.xyz);
    vec3 N = normalize(normalMat * inNormal);
    fragNormal = N;
    fragTexCoord = inTexCoord;

    if (dot(inTangent.xyz, inTangent.xyz) > 1e-4) {
        vec3 T = normalize(mat3(pcs.model) * inTangent.xyz);
        T = normalize(T - dot(T, N) * N);
        fragTangent = vec4(T, inTangent.w != 0.0 ? inTangent.w : 1.0);
    } else {
        fragTangent = vec4(0.0, 0.0, 0.0, 0.0);
    }

    gl_Position = ubo.viewProj * worldPos;
}
