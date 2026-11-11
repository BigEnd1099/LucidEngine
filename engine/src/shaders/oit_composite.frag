#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrSceneTex;
layout(set = 0, binding = 1) uniform sampler2D oitAccumTex;
layout(set = 0, binding = 2) uniform sampler2D oitRevealTex;

void main() {
    vec3 opaque = texture(hdrSceneTex, fragUV).rgb;
    vec4 accum = texture(oitAccumTex, fragUV);
    float reveal = texture(oitRevealTex, fragUV).r;

    // If no transparent surfaces covered this fragment, preserve opaque color exactly
    if (reveal >= 0.9999) {
        outColor = vec4(opaque, 1.0);
        return;
    }

    // Weighted Blended OIT (McGuire & Bavoil)
    vec3 transColor = accum.rgb / max(accum.a, 1e-5);
    vec3 result = opaque * reveal + transColor;
    outColor = vec4(result, 1.0);
}
