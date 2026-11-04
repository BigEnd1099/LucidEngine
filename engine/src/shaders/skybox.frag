#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 3) uniform sampler2D environmentMap;

layout(push_constant) uniform SkyboxPushConstants {
    mat4 invViewProj;
    vec4 cameraPosAndParams; // xyz = cameraPos, w = skyIntensity
} pcs;

vec2 SampleSphericalMap(vec3 v) {
    vec3 dir = normalize(v);
    vec2 uv = vec2(atan(dir.z, dir.x), asin(clamp(dir.y, -1.0, 1.0)));
    uv *= vec2(0.15915494, -0.31830989); // 1 / (2*PI), -1 / PI
    uv += 0.5;
    return uv;
}

void main() {
    // Unproject pixel from normalized device coordinates to world space ray direction
    vec4 clip = vec4(inUV.x * 2.0 - 1.0, inUV.y * 2.0 - 1.0, 1.0, 1.0);
    vec4 worldPos = pcs.invViewProj * clip;
    if (abs(worldPos.w) < 1e-6) discard;

    vec3 rayDir = normalize(worldPos.xyz / worldPos.w - pcs.cameraPosAndParams.xyz);
    vec2 skyUv = SampleSphericalMap(rayDir);

    vec3 skyColor = texture(environmentMap, skyUv).rgb;
    if (any(isnan(skyColor)) || any(isinf(skyColor))) {
        skyColor = vec3(0.0);
    }
    skyColor = max(skyColor, vec3(0.0));

    float intensity = pcs.cameraPosAndParams.w > 0.0 ? pcs.cameraPosAndParams.w : 1.0;
    outColor = vec4(skyColor * intensity, 1.0);
}
