#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in float inSize;

layout(push_constant) uniform ParticlePcs {
    mat4 vp;
} pcs;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = pcs.vp * vec4(inPos, 1.0);
    gl_PointSize = clamp(inSize * (1000.0 / max(gl_Position.w, 0.1)), 2.0, 80.0);
    fragColor = inColor;
}
