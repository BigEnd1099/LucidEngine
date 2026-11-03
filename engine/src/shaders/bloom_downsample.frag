#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uHdrTexture;

layout(push_constant) uniform BloomDownConstants {
    vec4 params; // x = threshold, y = knee, z = texelSizeX, w = texelSizeY
} pcs;

void main() {
    vec2 ts = pcs.params.zw;
    float threshold = pcs.params.x > 0.0 ? pcs.params.x : 1.0;

    // 13-tap anti-flicker box filter (Call of Duty / Unreal Engine 5 method)
    vec3 a = texture(uHdrTexture, inUV + vec2(-2.0, -2.0) * ts).rgb;
    vec3 b = texture(uHdrTexture, inUV + vec2( 0.0, -2.0) * ts).rgb;
    vec3 c = texture(uHdrTexture, inUV + vec2( 2.0, -2.0) * ts).rgb;
    vec3 d = texture(uHdrTexture, inUV + vec2(-2.0,  0.0) * ts).rgb;
    vec3 e = texture(uHdrTexture, inUV).rgb;
    vec3 f = texture(uHdrTexture, inUV + vec2( 2.0,  0.0) * ts).rgb;
    vec3 g = texture(uHdrTexture, inUV + vec2(-2.0,  2.0) * ts).rgb;
    vec3 h = texture(uHdrTexture, inUV + vec2( 0.0,  2.0) * ts).rgb;
    vec3 i = texture(uHdrTexture, inUV + vec2( 2.0,  2.0) * ts).rgb;

    vec3 j = texture(uHdrTexture, inUV + vec2(-1.0, -1.0) * ts).rgb;
    vec3 k = texture(uHdrTexture, inUV + vec2( 1.0, -1.0) * ts).rgb;
    vec3 l = texture(uHdrTexture, inUV + vec2(-1.0,  1.0) * ts).rgb;
    vec3 m = texture(uHdrTexture, inUV + vec2( 1.0,  1.0) * ts).rgb;

    vec3 color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;

    // Soft knee thresholding
    float brightness = max(max(color.r, color.g), color.b);
    float soft = brightness - threshold + 0.5;
    soft = clamp(soft, 0.0, 1.0);
    soft = soft * soft * 0.5;
    float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4);
    outColor = vec4(color * max(0.0, contribution), 1.0);
}
