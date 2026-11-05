#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out float outAO;

layout(set = 0, binding = 0) uniform sampler2D aoTex;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(push_constant) uniform BlurPush {
    vec4 params; // x = invWidth, y = invHeight, z = zNear, w = zFar
} pcs;

float linearizeDepth(float d) {
    float zNear = pcs.params.z;
    float zFar = pcs.params.w;
    return (zNear * zFar) / max(zFar - d * (zFar - zNear), 1e-6);
}

void main() {
    float rawCenter = texture(depthTex, fragUV).r;
    if (rawCenter >= 0.9999) {
        outAO = 1.0;
        return;
    }

    float centerZ = linearizeDepth(rawCenter);
    float depthSigma = max(0.05, centerZ * 0.035);
    const float spatialWeights[3] = float[](1.0, 0.85, 0.55);

    float sumAO = 0.0;
    float sumWeight = 0.0;

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 sampleUV = fragUV + vec2(float(x), float(y)) * pcs.params.xy;
            float sAO = texture(aoTex, sampleUV).r;
            float sZ = linearizeDepth(texture(depthTex, sampleUV).r);

            float dz = abs(sZ - centerZ) / depthSigma;
            float wDepth = exp(-dz * dz);
            float wSpatial = spatialWeights[abs(x)] * spatialWeights[abs(y)];
            float w = wDepth * wSpatial;

            sumAO += sAO * w;
            sumWeight += w;
        }
    }

    outAO = sumAO / max(sumWeight, 1e-5);
}
