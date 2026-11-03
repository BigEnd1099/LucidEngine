#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform KawaseConstants {
    vec4 params; // xy = texelSize, z = offset, w = unused
} pcs;

void main() {
    vec2 ts = pcs.params.xy;
    float offset = pcs.params.z;

    vec2 o = ts * (offset + 0.5);
    vec3 sum = texture(uTexture, inUV + vec2(-o.x, -o.y)).rgb;
    sum += texture(uTexture, inUV + vec2( o.x, -o.y)).rgb;
    sum += texture(uTexture, inUV + vec2(-o.x,  o.y)).rgb;
    sum += texture(uTexture, inUV + vec2( o.x,  o.y)).rgb;

    outColor = vec4(sum * 0.25, 1.0);
}
