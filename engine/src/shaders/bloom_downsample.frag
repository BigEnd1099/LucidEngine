#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D srcTex;

layout(push_constant) uniform BloomDownPush {
    vec4 params; // x = srcTexelW, y = srcTexelH, z = threshold, w = mipLevel (0 = prefilter + Karis)
} pcs;

const float MAX_HDR_VALUE = 65504.0;

float luminance(vec3 c) {
    return max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 0.0);
}

float karisWeight(vec3 c) {
    return 1.0 / (1.0 + luminance(c));
}

vec3 sanitizeSample(vec3 c) {
    if (any(isnan(c)) || any(isinf(c))) return vec3(0.0);
    return clamp(c, vec3(0.0), vec3(MAX_HDR_VALUE));
}

// Bezpieczne próbkowanie z przycięciem do środków skrajnych tekseli źródłowej mipmapy.
// Eliminuje duchy z zawijania (REPEAT/MIRROR) oraz poziome pasy przy krawędzi ekranu.
vec3 sampleClamped(vec2 uv, vec2 uvMin, vec2 uvMax) {
    return sanitizeSample(texture(srcTex, clamp(uv, uvMin, uvMax)).rgb);
}

vec3 softThreshold(vec3 color, float threshold) {
    color = sanitizeSample(color);
    float br = max(color.r, max(color.g, color.b));
    if (br <= 1e-5) return vec3(0.0);

    float th = max(threshold, 0.0);
    float knee = th * 0.5 + 1e-4;
    float soft = clamp(br - th + knee, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee);
    float contrib = max(soft, br - th) / max(br, 1e-4);
    return color * max(contrib, 0.0);
}

void main() {
    // Wyznaczenie rzeczywistego rozmiaru teksela bezpośrednio z powiązanego widoku mipmapy
    vec2 texSize = vec2(max(textureSize(srcTex, 0), ivec2(1)));
    vec2 t = 1.0 / texSize;
    vec2 uvMin = t * 0.5;
    vec2 uvMax = vec2(1.0) - t * 0.5;

    // Siatka 13-tap Jimenez (Call of Duty: Advanced Warfare / UE5)
    vec3 a = sampleClamped(fragUV + vec2(-2.0, -2.0) * t, uvMin, uvMax);
    vec3 b = sampleClamped(fragUV + vec2( 0.0, -2.0) * t, uvMin, uvMax);
    vec3 c = sampleClamped(fragUV + vec2( 2.0, -2.0) * t, uvMin, uvMax);

    vec3 d = sampleClamped(fragUV + vec2(-1.0, -1.0) * t, uvMin, uvMax);
    vec3 e = sampleClamped(fragUV,                        uvMin, uvMax);
    vec3 f = sampleClamped(fragUV + vec2( 1.0, -1.0) * t, uvMin, uvMax);

    vec3 g = sampleClamped(fragUV + vec2(-2.0,  0.0) * t, uvMin, uvMax);
    vec3 h = sampleClamped(fragUV + vec2( 2.0,  0.0) * t, uvMin, uvMax);

    vec3 i = sampleClamped(fragUV + vec2(-1.0,  1.0) * t, uvMin, uvMax);
    vec3 j = sampleClamped(fragUV + vec2( 1.0,  1.0) * t, uvMin, uvMax);

    vec3 k = sampleClamped(fragUV + vec2(-2.0,  2.0) * t, uvMin, uvMax);
    vec3 l = sampleClamped(fragUV + vec2( 0.0,  2.0) * t, uvMin, uvMax);
    vec3 m = sampleClamped(fragUV + vec2( 2.0,  2.0) * t, uvMin, uvMax);

    vec3 result;
    if (pcs.params.w < 0.5) {
        // Pierwszy stopień piramidy: progowanie miękkim kolanem + 5-grupowa średnia Karisa
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

    outColor = vec4(sanitizeSample(result), 1.0);
}