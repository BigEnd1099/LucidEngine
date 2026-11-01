#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

layout(push_constant) uniform ShadowPushConstants {
    mat4 lightSpaceMvp;
} pcs;

void main() {
    gl_Position = pcs.lightSpaceMvp * vec4(inPosition, 1.0);
}
