#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D srcTex;

layout(push_constant) uniform BloomUpPush {
    vec4 params; // x = srcTexelW, y = srcTexelH, z = filterRadius, w = mipWeight
} pcs;

const float MAX_HDR_VALUE = 65504.0;

vec3 sampleClamped(vec2 uv, vec2 uvMin, vec2 uvMax) {
    vec3 c = texture(srcTex, clamp(uv, uvMin, uvMax)).rgb;
    if (any(isnan(c)) || any(isinf(c))) return vec3(0.0);
    return clamp(c, vec3(0.0), vec3(MAX_HDR_VALUE));
}

void main() {
    // Bazujemy na rzeczywistym rozmiarze aktualnie bindowanej mipmapy źródłowej
    vec2 texSize = vec2(max(textureSize(srcTex, 0), ivec2(1)));
    vec2 texel = 1.0 / texSize;
    vec2 uvMin = texel * 0.5;
    vec2 uvMax = vec2(1.0) - texel * 0.5;

    vec2 d = texel * max(pcs.params.z, 0.0);

    vec3 s = vec3(0.0);
    s += sampleClamped(fragUV + vec2(-d.x, -d.y), uvMin, uvMax) * 1.0;
    s += sampleClamped(fragUV + vec2( 0.0, -d.y), uvMin, uvMax) * 2.0;
    s += sampleClamped(fragUV + vec2( d.x, -d.y), uvMin, uvMax) * 1.0;

    s += sampleClamped(fragUV + vec2(-d.x,  0.0), uvMin, uvMax) * 2.0;
    s += sampleClamped(fragUV,                    uvMin, uvMax) * 4.0;
    s += sampleClamped(fragUV + vec2( d.x,  0.0), uvMin, uvMax) * 2.0;

    s += sampleClamped(fragUV + vec2(-d.x,  d.y), uvMin, uvMax) * 1.0;
    s += sampleClamped(fragUV + vec2( 0.0,  d.y), uvMin, uvMax) * 2.0;
    s += sampleClamped(fragUV + vec2( d.x,  d.y), uvMin, uvMax) * 1.0;

    vec3 outHdr = (s * (1.0 / 16.0)) * max(pcs.params.w, 0.0);
    outColor = vec4(clamp(outHdr, vec3(0.0), vec3(MAX_HDR_VALUE)), 1.0);
}