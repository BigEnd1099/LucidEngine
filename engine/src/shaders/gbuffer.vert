#version 450

invariant gl_Position;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragTangent;
layout(location = 4) out vec4 currClipPos;
layout(location = 5) out vec4 prevClipPos;

layout(set = 0, binding = 1) uniform SceneUbo {
    mat4 viewProj;
    mat4 view;
    mat4 invViewProj;
} ubo;

layout(std430, set = 0, binding = 18) readonly buffer MotionBuffer {
    mat4 prevViewProj;
    mat4 prevModels[];
} motion;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 normalCol0;      // w = metallic
    vec4 normalCol1;      // w = roughness
    vec4 normalCol2;      // w = emissiveIntensity
    vec4 baseColorFactor; // rgb = baseColor, a = aoOverride
} pcs;

void main() {
    vec4 curWorldPos = pcs.model * vec4(inPosition, 1.0);
    fragWorldPos = curWorldPos.xyz;

    mat3 normalMat = mat3(pcs.normalCol0.xyz, pcs.normalCol1.xyz, pcs.normalCol2.xyz);
    vec3 N = normalize(normalMat * inNormal);
    fragNormal = N;
    fragTexCoord = inTexCoord;

    if (dot(inTangent.xyz, inTangent.xyz) > 1e-4) {
        vec3 T = normalize(mat3(pcs.model) * inTangent.xyz);
        T = normalize(T - dot(T, N) * N);
        fragTangent = vec4(T, inTangent.w != 0.0 ? inTangent.w : 1.0);
    } else {
        fragTangent = vec4(0.0, 0.0, 0.0, 0.0);
    }

    gl_Position = ubo.viewProj * curWorldPos;
    currClipPos = gl_Position;

    mat4 prevModel = motion.prevModels[gl_InstanceIndex];
    vec4 prevWorldPos = prevModel * vec4(inPosition, 1.0);
    prevClipPos = motion.prevViewProj * prevWorldPos;
}
