#version 450
layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;

layout(location = 0) out vec4 outAccum;
layout(location = 1) out float outReveal;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColor;
    vec4 params; // x = metallic, y = roughness
} pc;

void main() {
    vec4 color = pc.baseColor;
    float alpha = clamp(color.a, 0.0, 1.0);
    if (alpha < 0.005) discard;

    // Linear distance from camera in view space (in meters) directly from gl_FragCoord.w = 1.0 / w_clip = 1.0 / z_view
    float viewZ = 1.0 / max(gl_FragCoord.w, 1e-5);

    // Exact McGuire & Bavoil weight function
    float distWeight = 0.03 / (1e-5 + pow(viewZ / 200.0, 4.0));
    float weight = alpha * clamp(distWeight, 1e-2, 3e3);

    outAccum = vec4(color.rgb * alpha * weight, alpha * weight);
    outReveal = alpha;
}
