#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D srcTex;

layout(push_constant) uniform BloomDownPush {
    vec4 params; // x = srcTexelW, y = srcTexelH, z = threshold, w = mipLevel (0 = prefilter + Karis)
} pcs;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float karisWeight(vec3 c) {
    return 1.0 / (1.0 + luminance(c));
}

vec3 softThreshold(vec3 color, float threshold) {
    float br = max(color.r, max(color.g, color.b));
    float knee = threshold * 0.5 + 1e-4;
    float soft = clamp(br - threshold + knee, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee);
    float contrib = max(soft, br - threshold) / max(br, 1e-4);
    return color * max(contrib, 0.0);
}

void main() {
    vec2 t = pcs.params.xy;

    // Siatka 13-tap Jimenez (Call of Duty: Advanced Warfare / UE5)
    vec3 a = texture(srcTex, fragUV + vec2(-2.0, -2.0) * t).rgb;
    vec3 b = texture(srcTex, fragUV + vec2( 0.0, -2.0) * t).rgb;
    vec3 c = texture(srcTex, fragUV + vec2( 2.0, -2.0) * t).rgb;

    vec3 d = texture(srcTex, fragUV + vec2(-1.0, -1.0) * t).rgb;
    vec3 e = texture(srcTex, fragUV).rgb;
    vec3 f = texture(srcTex, fragUV + vec2( 1.0, -1.0) * t).rgb;

    vec3 g = texture(srcTex, fragUV + vec2(-2.0,  0.0) * t).rgb;
    vec3 h = texture(srcTex, fragUV + vec2( 2.0,  0.0) * t).rgb;

    vec3 i = texture(srcTex, fragUV + vec2(-1.0,  1.0) * t).rgb;
    vec3 j = texture(srcTex, fragUV + vec2( 1.0,  1.0) * t).rgb;

    vec3 k = texture(srcTex, fragUV + vec2(-2.0,  2.0) * t).rgb;
    vec3 l = texture(srcTex, fragUV + vec2( 0.0,  2.0) * t).rgb;
    vec3 m = texture(srcTex, fragUV + vec2( 2.0,  2.0) * t).rgb;

    vec3 result;
    if (pcs.params.w < 0.5) {
        // Pierwszy stopień piramidy: progowanie miękkim kolanem + 5-grupowa średnia Karisa tłumiąca subpikselowe piki
        float th = pcs.params.z;
        vec3 g0 = softThreshold((d + f + i + j) * 0.25, th);
        vec3 g1 = softThreshold((a + b + g + e) * 0.25, th);
        vec3 g2 = softThreshold((b + c + e + h) * 0.25, th);
        vec3 g3 = softThreshold((g + e + k + l) * 0.25, th);
        vec3 g4 = softThreshold((e + h + l + m) * 0.25, th);

        float w0 = karisWeight(g0) * 0.5;
        float w1 = karisWeight(g1) * 0.125;
        float w2 = karisWeight(g2) * 0.125;
        float w3 = karisWeight(g3) * 0.125;
        float w4 = karisWeight(g4) * 0.125;

        result = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / max(w0 + w1 + w2 + w3 + w4, 1e-5);
    } else {
        result = e * 0.125;
        result += (a + c + k + m) * 0.03125;
        result += (b + g + h + l) * 0.0625;
        result += (d + f + i + j) * 0.125;
    }

    outColor = vec4(max(result, vec3(0.0)), 1.0);
}
