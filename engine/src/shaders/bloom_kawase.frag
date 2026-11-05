#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D srcTex;

layout(push_constant) uniform KawasePush {
    vec4 params;
} pcs;

void main() {
    vec2 off = pcs.params.xy * pcs.params.z;
    vec3 sum = texture(srcTex, fragUV).rgb * 4.0;
    sum += texture(srcTex, fragUV + vec2(-off.x, -off.y)).rgb;
    sum += texture(srcTex, fragUV + vec2( off.x, -off.y)).rgb;
    sum += texture(srcTex, fragUV + vec2(-off.x,  off.y)).rgb;
    sum += texture(srcTex, fragUV + vec2( off.x,  off.y)).rgb;
    outColor = vec4(sum * 0.125, 1.0);
}
