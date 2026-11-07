#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

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

layout(set = 0, binding = 3) uniform samplerCube envCubemap;

void main() {
    vec2 ndc = fragUV * 2.0 - 1.0;
    vec4 worldTarget = ubo.invViewProj * vec4(ndc, 1.0, 1.0);
    vec3 rayDir = normalize(worldTarget.xyz / worldTarget.w - ubo.cameraPos.xyz);

    vec3 skyHdr = textureLod(envCubemap, rayDir, 0.0).rgb * max(ubo.ambientColor.w, 0.0);

    // Analityczna tarcza słoneczna oraz aureola Mie zsynchronizowana z kierunkiem słońca
    if (ubo.sunDirection.w > 0.05) {
        vec3 sunDir = normalize(ubo.sunDirection.xyz);
        float cosTheta = dot(rayDir, sunDir);
        float sunDisk = smoothstep(0.99965, 0.99990, cosTheta);
        float sunCorona = pow(max(cosTheta, 0.0), 256.0) * 0.35;
        skyHdr += ubo.sunColor.rgb * ubo.sunDirection.w * (sunDisk * 8.0 + sunCorona);
    }

    outColor = vec4(max(skyHdr, vec3(0.0)), 1.0);
}
