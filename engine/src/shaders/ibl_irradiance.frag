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

void main() {
    vec3 N = cubeFaceDirection(pcs.faceIndex, fragUV);
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = cross(N, right);

    vec3 irradiance = vec3(0.0);
    float sampleCount = 0.0;
    const float sampleDelta = 0.035;

    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        float cosPhi = cos(phi);
        float sinPhi = sin(phi);
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            float sinTheta = sin(theta);
            float cosTheta = cos(theta);
            vec3 tangentSample = vec3(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;
            // Próbkowanie wyższego mipu (2.0) eliminuje szum od ekstremalnie małych źródeł słońca
            irradiance += textureLod(envCubemap, sampleVec, 2.0).rgb * cosTheta * sinTheta;
            sampleCount += 1.0;
        }
    }

    irradiance = PI * irradiance / max(sampleCount, 1.0);
    outColor = vec4(irradiance, 1.0);
}
