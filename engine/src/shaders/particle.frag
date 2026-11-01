#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 coord = gl_PointCoord - vec2(0.5);
    float distSq = dot(coord, coord);
    if (distSq > 0.25) discard;
    float alpha = (1.0 - sqrt(distSq) * 2.0) * fragColor.a;
    outColor = vec4(fragColor.rgb * alpha, alpha);
}
