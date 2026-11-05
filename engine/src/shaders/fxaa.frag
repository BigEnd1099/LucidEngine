#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D ldrTex; // rgb = sRGB, a = luma

layout(push_constant) uniform FxaaPush {
    vec4 params; // x = invWidth, y = invHeight, z = subpixQuality, w = edgeThreshold
} pcs;

const float EDGE_STEPS[12] = float[](1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

void main() {
    vec2 rcpFrame = pcs.params.xy;
    vec4 rgbyM = texture(ldrTex, fragUV);
    float lumaM = rgbyM.a;

    float lumaN = textureOffset(ldrTex, fragUV, ivec2( 0, -1)).a;
    float lumaS = textureOffset(ldrTex, fragUV, ivec2( 0,  1)).a;
    float lumaW = textureOffset(ldrTex, fragUV, ivec2(-1,  0)).a;
    float lumaE = textureOffset(ldrTex, fragUV, ivec2( 1,  0)).a;

    float rangeMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float rangeMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float range = rangeMax - rangeMin;

    // Wczesne wyjście dla obszarów o niskim kontraście
    if (range < max(0.0312, rangeMax * pcs.params.w)) {
        outColor = vec4(rgbyM.rgb, 1.0);
        return;
    }

    float lumaNW = textureOffset(ldrTex, fragUV, ivec2(-1, -1)).a;
    float lumaNE = textureOffset(ldrTex, fragUV, ivec2( 1, -1)).a;
    float lumaSW = textureOffset(ldrTex, fragUV, ivec2(-1,  1)).a;
    float lumaSE = textureOffset(ldrTex, fragUV, ivec2( 1,  1)).a;

    float lumaNS = lumaN + lumaS;
    float lumaWE = lumaW + lumaE;
    float lumaNWSW = lumaNW + lumaSW;
    float lumaNESE = lumaNE + lumaSE;
    float lumaNWNE = lumaNW + lumaNE;
    float lumaSWSE = lumaSW + lumaSE;

    // Filtr subpikselowy 3x3
    float subpixAvg = (2.0 * (lumaNS + lumaWE) + lumaNWSW + lumaNESE) * (1.0 / 12.0);
    float subpixFactor = clamp(abs(subpixAvg - lumaM) / range, 0.0, 1.0);
    float subpixBlend = smoothstep(0.0, 1.0, subpixFactor);
    subpixBlend = subpixBlend * subpixBlend * pcs.params.z;

    // Wybór orientacji krawędzi (pozioma vs pionowa)
    float edgeHorz = abs(-2.0 * lumaW + lumaNWSW) + abs(-2.0 * lumaM + lumaNS) * 2.0 + abs(-2.0 * lumaE + lumaNESE);
    float edgeVert = abs(-2.0 * lumaN + lumaNWNE) + abs(-2.0 * lumaM + lumaWE) * 2.0 + abs(-2.0 * lumaS + lumaSWSE);
    bool isHorizontal = (edgeHorz >= edgeVert);

    float luma1 = isHorizontal ? lumaN : lumaW;
    float luma2 = isHorizontal ? lumaS : lumaE;
    float grad1 = abs(luma1 - lumaM);
    float grad2 = abs(luma2 - lumaM);
    bool is1Steepest = (grad1 >= grad2);

    float stepLength = isHorizontal ? rcpFrame.y : rcpFrame.x;
    float lumaLocalAvg = 0.0;
    if (is1Steepest) {
        stepLength = -stepLength;
        lumaLocalAvg = 0.5 * (luma1 + lumaM);
    } else {
        lumaLocalAvg = 0.5 * (luma2 + lumaM);
    }

    float gradientScaled = 0.25 * max(grad1, grad2);
    vec2 currentUV = fragUV;
    if (isHorizontal) currentUV.y += stepLength * 0.5;
    else              currentUV.x += stepLength * 0.5;

    // 12-krokowy spacer wzdłuż krawędzi (FXAA 3.11 Quality)
    vec2 edgeOffset = isHorizontal ? vec2(rcpFrame.x, 0.0) : vec2(0.0, rcpFrame.y);
    vec2 uv1 = currentUV - edgeOffset;
    vec2 uv2 = currentUV + edgeOffset;

    float lumaEnd1 = texture(ldrTex, uv1).a - lumaLocalAvg;
    float lumaEnd2 = texture(ldrTex, uv2).a - lumaLocalAvg;
    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;

    if (!reached1) uv1 -= edgeOffset;
    if (!reached2) uv2 += edgeOffset;

    for (int i = 2; i < 12 && !(reached1 && reached2); ++i) {
        if (!reached1) {
            lumaEnd1 = texture(ldrTex, uv1).a - lumaLocalAvg;
            reached1 = abs(lumaEnd1) >= gradientScaled;
        }
        if (!reached2) {
            lumaEnd2 = texture(ldrTex, uv2).a - lumaLocalAvg;
            reached2 = abs(lumaEnd2) >= gradientScaled;
        }
        if (!reached1) uv1 -= edgeOffset * EDGE_STEPS[i];
        if (!reached2) uv2 += edgeOffset * EDGE_STEPS[i];
    }

    float dist1 = isHorizontal ? (fragUV.x - uv1.x) : (fragUV.y - uv1.y);
    float dist2 = isHorizontal ? (uv2.x - fragUV.x) : (uv2.y - fragUV.y);
    float minDist = min(dist1, dist2);
    float spanLength = max(dist1 + dist2, 1e-5);

    bool isDir1 = (dist1 < dist2);
    float lumaEndNearest = isDir1 ? lumaEnd1 : lumaEnd2;
    bool correctVariation = (lumaEndNearest < 0.0) != ((lumaM - lumaLocalAvg) < 0.0);

    float pixelOffset = correctVariation ? (0.5 - minDist / spanLength) : 0.0;
    float finalOffset = max(pixelOffset, subpixBlend);

    vec2 finalUV = fragUV;
    if (isHorizontal) finalUV.y += finalOffset * stepLength;
    else              finalUV.x += finalOffset * stepLength;

    outColor = vec4(texture(ldrTex, finalUV).rgb, 1.0);
}
