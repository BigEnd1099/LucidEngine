#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 p = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(p, p);
    if (r2 > 1.0) discard;

    // Gładki profil Gaussa z gorącym rdzeniem dla realistycznego efektu Bloom
    float softMask = exp(-3.5 * r2) * (1.0 - r2);
    float coreBoost = 1.0 + 1.25 * exp(-12.0 * r2);
    outColor = vec4(fragColor.rgb * coreBoost, fragColor.a * softMask);
}
