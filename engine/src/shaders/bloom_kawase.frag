#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D srcTex;

layout(push_constant) uniform KawasePush {
    vec4 params;
} pcs;

const float MAX_HDR_VALUE = 65504.0;

vec3 sampleClamped(vec2 uv, vec2 uvMin, vec2 uvMax) {
    vec3 c = texture(srcTex, clamp(uv, uvMin, uvMax)).rgb;
    if (any(isnan(c)) || any(isinf(c))) return vec3(0.0);
    return clamp(c, vec3(0.0), vec3(MAX_HDR_VALUE));
}

void main() {
    vec2 texSize = vec2(max(textureSize(srcTex, 0), ivec2(1)));
    vec2 texel = 1.0 / texSize;
    vec2 uvMin = texel * 0.5;
    vec2 uvMax = vec2(1.0) - texel * 0.5;

    vec2 off = texel * max(pcs.params.z, 0.0);

    vec3 sum = sampleClamped(fragUV, uvMin, uvMax) * 4.0;
    sum += sampleClamped(fragUV + vec2(-off.x, -off.y), uvMin, uvMax);
    sum += sampleClamped(fragUV + vec2( off.x, -off.y), uvMin, uvMax);
    sum += sampleClamped(fragUV + vec2(-off.x,  off.y), uvMin, uvMax);
    sum += sampleClamped(fragUV + vec2( off.x,  off.y), uvMin, uvMax);

    outColor = vec4(sum * 0.125, 1.0);
}