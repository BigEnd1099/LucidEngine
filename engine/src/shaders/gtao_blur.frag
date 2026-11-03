#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uAoTexture;
layout(set = 0, binding = 1) uniform sampler2D uDepthTexture;

layout(push_constant) uniform BlurConstants {
    vec4 texelSize; // xy = texelSize, zw = unused
} pc;

void main() {
    float centerDepth = texture(uDepthTexture, inUV).r;
    float centerAO = texture(uAoTexture, inUV).r;

    if (centerDepth >= 0.9999) {
        outColor = vec4(0.0);
        return;
    }

    vec2 ts = pc.texelSize.xy;

    // 3x3 Gaussian Bilateral Kernel (8 neighbors + center)
    float totalAO = centerAO * 0.25;
    float totalWeight = 0.25;

    const vec2 offsets[8] = vec2[](
        vec2(-1.0, -1.0), vec2( 0.0, -1.0), vec2( 1.0, -1.0),
        vec2(-1.0,  0.0),                   vec2( 1.0,  0.0),
        vec2(-1.0,  1.0), vec2( 0.0,  1.0), vec2( 1.0,  1.0)
    );

    const float spatialWeights[8] = float[](
        0.0625, 0.125, 0.0625,
        0.125,         0.125,
        0.0625, 0.125, 0.0625
    );

    // Dynamic depth tolerance that accommodates perspective depth slopes across polygons
    float depthTolerance = max(centerDepth * 0.008, 0.0004);

    for (int i = 0; i < 8; ++i) {
        vec2 uv = inUV + offsets[i] * ts * 1.5;
        float d = texture(uDepthTexture, uv).r;
        float ao = texture(uAoTexture, uv).r;

        float depthDiff = abs(centerDepth - d);
        float depthWeight = exp(-(depthDiff * depthDiff) / (2.0 * depthTolerance * depthTolerance));

        float w = spatialWeights[i] * depthWeight;
        totalAO += ao * w;
        totalWeight += w;
    }

    outColor = vec4(totalAO / max(totalWeight, 1e-4), 0.0, 0.0, 1.0);
}
