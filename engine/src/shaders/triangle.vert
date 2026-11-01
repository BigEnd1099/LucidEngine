#version 450

layout(location = 0) out vec3 fragColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
    vec4 normalRow0;
    vec4 normalRow1;
    vec4 normalRow2;
} pcs;

const vec3 positions[36] = vec3[36](
    // Front
    vec3(-0.5, -0.5,  0.5), vec3( 0.5, -0.5,  0.5), vec3( 0.5,  0.5,  0.5),
    vec3( 0.5,  0.5,  0.5), vec3(-0.5,  0.5,  0.5), vec3(-0.5, -0.5,  0.5),
    // Back
    vec3(-0.5, -0.5, -0.5), vec3(-0.5,  0.5, -0.5), vec3( 0.5,  0.5, -0.5),
    vec3( 0.5,  0.5, -0.5), vec3( 0.5, -0.5, -0.5), vec3(-0.5, -0.5, -0.5),
    // Top
    vec3(-0.5,  0.5, -0.5), vec3(-0.5,  0.5,  0.5), vec3( 0.5,  0.5,  0.5),
    vec3( 0.5,  0.5,  0.5), vec3( 0.5,  0.5, -0.5), vec3(-0.5,  0.5, -0.5),
    // Bottom
    vec3(-0.5, -0.5, -0.5), vec3( 0.5, -0.5, -0.5), vec3( 0.5, -0.5,  0.5),
    vec3( 0.5, -0.5,  0.5), vec3(-0.5, -0.5,  0.5), vec3(-0.5, -0.5, -0.5),
    // Right
    vec3( 0.5, -0.5, -0.5), vec3( 0.5,  0.5, -0.5), vec3( 0.5,  0.5,  0.5),
    vec3( 0.5,  0.5,  0.5), vec3( 0.5, -0.5,  0.5), vec3( 0.5, -0.5, -0.5),
    // Left
    vec3(-0.5, -0.5, -0.5), vec3(-0.5, -0.5,  0.5), vec3(-0.5,  0.5,  0.5),
    vec3(-0.5,  0.5,  0.5), vec3(-0.5,  0.5, -0.5), vec3(-0.5, -0.5, -0.5)
);

const vec3 normals[36] = vec3[36](
    vec3(0,0,1),vec3(0,0,1),vec3(0,0,1),vec3(0,0,1),vec3(0,0,1),vec3(0,0,1),
    vec3(0,0,-1),vec3(0,0,-1),vec3(0,0,-1),vec3(0,0,-1),vec3(0,0,-1),vec3(0,0,-1),
    vec3(0,1,0),vec3(0,1,0),vec3(0,1,0),vec3(0,1,0),vec3(0,1,0),vec3(0,1,0),
    vec3(0,-1,0),vec3(0,-1,0),vec3(0,-1,0),vec3(0,-1,0),vec3(0,-1,0),vec3(0,-1,0),
    vec3(1,0,0),vec3(1,0,0),vec3(1,0,0),vec3(1,0,0),vec3(1,0,0),vec3(1,0,0),
    vec3(-1,0,0),vec3(-1,0,0),vec3(-1,0,0),vec3(-1,0,0),vec3(-1,0,0),vec3(-1,0,0)
);

void main() {
    gl_Position = pcs.mvp * vec4(positions[gl_VertexIndex], 1.0);
    
    // Simple directional lighting
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    mat3 normalMat = mat3(pcs.normalRow0.xyz, pcs.normalRow1.xyz, pcs.normalRow2.xyz);
    vec3 normalWorld = normalize(normalMat * normals[gl_VertexIndex]);
    float diff = max(dot(normalWorld, lightDir), 0.2); // ambient 0.2
    
    fragColor = pcs.color.rgb * diff;
}
