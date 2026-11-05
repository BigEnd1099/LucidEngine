#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D srcTex;

layout(push_constant) uniform BloomUpPush {
    vec4 params; // x = srcTexelW, y = srcTexelH, z = filterRadius, w = mipWeight
} pcs;

void main() {
    vec2 d = pcs.params.xy * pcs.params.z;

    vec3 s = vec3(0.0);
    s += texture(srcTex, fragUV + vec2(-d.x, -d.y)).rgb * 1.0;
    s += texture(srcTex, fragUV + vec2( 0.0, -d.y)).rgb * 2.0;
    s += texture(srcTex, fragUV + vec2( d.x, -d.y)).rgb * 1.0;

    s += texture(srcTex, fragUV + vec2(-d.x,  0.0)).rgb * 2.0;
    s += texture(srcTex, fragUV).rgb                    * 4.0;
    s += texture(srcTex, fragUV + vec2( d.x,  0.0)).rgb * 2.0;

    s += texture(srcTex, fragUV + vec2(-d.x,  d.y)).rgb * 1.0;
    s += texture(srcTex, fragUV + vec2( 0.0,  d.y)).rgb * 2.0;
    s += texture(srcTex, fragUV + vec2( d.x,  d.y)).rgb * 1.0;

    outColor = vec4((s * (1.0 / 16.0)) * pcs.params.w, 1.0);
}
