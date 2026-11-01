#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
    mat4 lightSpaceMatrix;
    vec4 baseColorFactor;
    vec4 cameraPos;        // xyz = camera position, w = time
    vec4 materialParams;   // x = metallic, y = roughness, z = isGround, w = emissive
} pcs;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) out vec4 fragLightSpacePos;

void main() {
    gl_Position = pcs.mvp * vec4(inPosition, 1.0);
    
    // Normal matrix to handle non-uniform scale correctly
    mat3 normalMatrix = transpose(inverse(mat3(pcs.model)));
    fragNormal = normalize(normalMatrix * inNormal);
    
    vec4 worldPos4 = pcs.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos4.xyz;
    fragLightSpacePos = pcs.lightSpaceMatrix * worldPos4;
    
    fragTexCoord = inTexCoord;
    fragColor = pcs.baseColorFactor.rgb;
}
