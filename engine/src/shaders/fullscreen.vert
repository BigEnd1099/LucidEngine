#version 450

layout(location = 0) out vec2 fragUV;

void main() {
    fragUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    // z = 1.0 pozwala wykorzystać ten sam shader dla Skyboxa (depth == 1.0) oraz przebiegów bez testu głębi
    gl_Position = vec4(fragUV * 2.0 - 1.0, 1.0, 1.0);
}
