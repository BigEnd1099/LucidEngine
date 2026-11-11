#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragTangent;
layout(location = 4) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

struct OccluderData {
    vec4 centerRadius;
    vec4 halfExtents;
};

layout(set = 0, binding = 0) uniform sampler2DArray shadowMap;
layout(set = 0, binding = 1) uniform SceneUbo {
    mat4 viewProj;
    mat4 view;
    mat4 invViewProj;
    mat4 cascadeLightSpace[4];
    vec4 cascadeSplits;
    vec4 cascadeRadii;
    vec4 cameraPos;
    vec4 sunDirection;
    vec4 sunPosition;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 fogParams;
    vec4 fogColor;
    vec4 screenParams;
    vec4 lightsPos[8];
    vec4 lightsCol[8];
    vec4 lightsDir[8];
    vec4 lightsParams[8];
    vec4 pointShadowParams;
    vec4 bounceParams; // x = enabled, y = intensity, z = groundAlbedo, w = softShadows (1.0 = ON, 0.0 = OFF)
    vec4 bounceColor;
    vec4 rtParams; // x = RT active, y = RT shadows, z = RT AO
    int numLights;
    int numOccluders;
    int frameIndex;
    int skyboxMode;
    OccluderData occluders[32];
} ubo;
layout(set = 0, binding = 2) uniform samplerCube pointShadowMap;
layout(set = 0, binding = 4) uniform samplerCube irradianceMap;
layout(set = 0, binding = 5) uniform samplerCube prefilterMap;
layout(set = 0, binding = 6) uniform sampler2D brdfLut;
layout(set = 0, binding = 7) uniform sampler2D ssaoMap;
layout(set = 0, binding = 8) uniform sampler2D albedoMap;
layout(set = 0, binding = 9) uniform sampler2D normalMap;
layout(set = 0, binding = 10) uniform sampler2D ormMap;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 normalCol0;      // w = metallic
    vec4 normalCol1;      // w = roughness
    vec4 normalCol2;      // w = emissiveIntensity
    vec4 baseColorFactor; // rgb = baseColor, a = aoOverride
} pcs;

const float PI = 3.14159265359;
const float MAX_REFLECTION_LOD = 4.0;
const float MAX_HDR_VALUE = 65504.0;

vec3 sanitizeHDR(vec3 c) {
    if (any(isnan(c)) || any(isinf(c))) return vec3(0.0);
    return clamp(c, vec3(0.0), vec3(MAX_HDR_VALUE));
}

float interleavedGradientNoise(vec2 screenPos) {
    return fract(52.9829189 * fract(dot(screenPos, vec2(0.06711056, 0.00583715))));
}

vec2 vogelDiskSample(int sampleIdx, int samplesCount, float phi) {
    float goldenAngle = 2.39996323;
    float r = sqrt((float(sampleIdx) + 0.5) / float(samplesCount));
    float theta = float(sampleIdx) * goldenAngle + phi;
    return vec2(cos(theta), sin(theta)) * r;
}

vec3 computeNormal(vec3 N) {
    vec3 tangentNormal = texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0;
    if (dot(tangentNormal.xy, tangentNormal.xy) < 1e-3) {
        return N;
    }
    if (abs(fragTangent.w) > 0.5) {
        vec3 T = normalize(fragTangent.xyz - dot(fragTangent.xyz, N) * N);
        vec3 B = cross(N, T) * fragTangent.w;
        return normalize(mat3(T, B, N) * tangentNormal);
    }
    vec3 dp1 = dFdx(fragWorldPos);
    vec3 dp2 = dFdy(fragWorldPos);
    vec2 duv1 = dFdx(fragTexCoord);
    vec2 duv2 = dFdy(fragTexCoord);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invMax = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-8);
    return normalize(mat3(T * invMax, B * invMax, N) * tangentNormal);
}

// Heitz 2014 Height-Correlated Smith GGX
float distributionGGX(float NdotH, float alpha) {
    float a = max(alpha, 1e-5);
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-12);
}

float visibilitySmithGGXCorrelated(float NdotV, float NdotL, float alpha) {
    float a = max(alpha, 1e-5);
    float a2 = a * a;
    float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-7);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Jimenez 2016: Kolorowa wielokrotna okluzja otoczenia (zachowuje nasycenie we wnękach)
vec3 gtaoMultiBounce(float visibility, vec3 albedo) {
    vec3 a =  2.0404 * albedo - 0.3324;
    vec3 b = -4.7951 * albedo + 0.6417;
    vec3 c =  2.7552 * albedo + 0.6903;
    return max(vec3(visibility), ((visibility * a + b) * visibility + c) * visibility);
}

// Lagarde 2014: Fizyczna okluzja odbić zwierciadlanych
float computeSpecularAO(float NdotV, float ao, float roughness) {
    return clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

float sampleCascadeShadow(int cascadeIdx, vec3 worldPos, vec3 geomN, vec3 L, float phi, bool softShadows) {
    float cRadius = max(ubo.cascadeRadii[cascadeIdx], 1.0);
    float texelWorldSize = (2.0 * cRadius) / float(textureSize(shadowMap, 0).x);
    float NdotL = max(dot(geomN, L), 0.0);
    float sinTheta = sqrt(max(0.0, 1.0 - NdotL * NdotL));
    vec3 offsetPos = worldPos + geomN * (texelWorldSize * (1.2 + 2.0 * sinTheta));

    vec4 lsPos = ubo.cascadeLightSpace[cascadeIdx] * vec4(offsetPos, 1.0);
    vec3 proj = lsPos.xyz / lsPos.w;
    proj.xy = proj.xy * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.z < 0.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) {
        return 1.0;
    }

    float depthBias = 0.00025 + 0.0006 * sinTheta;

    // Tryb ostrych cieni ([ ] Soft Shadows odznaczone)
    if (!softShadows) {
        float closestDepth = texture(shadowMap, vec3(proj.xy, float(cascadeIdx))).r;
        return (proj.z - depthBias <= closestDepth) ? 1.0 : 0.0;
    }

    // Tryb miękkich cieni PCSS ([x] Soft Shadows zaznaczone)
    float searchRadius = clamp(0.06 / (2.0 * cRadius), 0.0015, 0.008);
    float avgBlockerDepth = 0.0;
    int numBlockers = 0;
    int blockerSamples = (cascadeIdx == 0) ? 8 : (cascadeIdx == 1 ? 4 : 2);
    for (int i = 0; i < blockerSamples; ++i) {
        vec2 offset = vogelDiskSample(i, blockerSamples, phi) * searchRadius;
        float d = texture(shadowMap, vec3(proj.xy + offset, float(cascadeIdx))).r;
        if (d < proj.z - depthBias) {
            avgBlockerDepth += d;
            numBlockers++;
        }
    }

    if (numBlockers == 0) return 1.0;

    avgBlockerDepth /= float(numBlockers);
    float blockerDist = max(proj.z - avgBlockerDepth, 0.0);

    float minFilterRadius = 1.0 / float(textureSize(shadowMap, 0).x);
    const float penumbraScale = 0.06;
    float uvFilterRadius = clamp(minFilterRadius + blockerDist * penumbraScale, minFilterRadius, 0.0065);

    float visibility = 0.0;
    int pcfSamples = (cascadeIdx == 0) ? 16 : (cascadeIdx == 1 ? 8 : 4);
    for (int i = 0; i < pcfSamples; ++i) {
        vec2 offset = vogelDiskSample(i, pcfSamples, phi) * uvFilterRadius;
        float closestDepth = texture(shadowMap, vec3(proj.xy + offset, float(cascadeIdx))).r;
        visibility += (proj.z - depthBias <= closestDepth) ? 1.0 : 0.0;
    }
    return visibility / float(pcfSamples);
}

float computeDirectionalShadow(vec3 worldPos, vec3 geomN, vec3 L, float viewDepth, float ign, bool softShadows) {
    if (viewDepth >= ubo.cascadeSplits.w) return 1.0;

    int cascadeIdx = 3;
    if (viewDepth < ubo.cascadeSplits.x) cascadeIdx = 0;
    else if (viewDepth < ubo.cascadeSplits.y) cascadeIdx = 1;
    else if (viewDepth < ubo.cascadeSplits.z) cascadeIdx = 2;

    float phi = ign * 2.0 * PI;
    float shadow = sampleCascadeShadow(cascadeIdx, worldPos, geomN, L, phi, softShadows);

    if (softShadows && cascadeIdx < 3) {
        float splitEnd = ubo.cascadeSplits[cascadeIdx];
        float splitStart = (cascadeIdx == 0) ? 0.0 : ubo.cascadeSplits[cascadeIdx - 1];
        float blendBand = (splitEnd - splitStart) * 0.15;
        float blendFactor = clamp((viewDepth - (splitEnd - blendBand)) / max(blendBand, 0.01), 0.0, 1.0);
        if (blendFactor > 0.001) {
            float nextShadow = sampleCascadeShadow(cascadeIdx + 1, worldPos, geomN, L, phi, true);
            shadow = mix(shadow, nextShadow, blendFactor);
        }
    }

    float distFade = 1.0 - smoothstep(ubo.cascadeSplits.w * 0.88, ubo.cascadeSplits.w, viewDepth);
    return mix(1.0, shadow, distFade);
}

float samplePointLightShadow(vec3 toLight, float currentDist, vec3 geomN, float ign, bool softShadows) {
    float nearPlane = max(ubo.pointShadowParams.y, 0.01);
    float farPlane = max(ubo.pointShadowParams.z, 1.0);
    if (currentDist >= farPlane) return 1.0;

    vec3 L = normalize(toLight);
    float NdotL = max(dot(geomN, L), 0.0);
    vec3 dir = -toLight + geomN * (0.015 * (1.0 - NdotL));
    float bias = max(0.03 * (1.0 - NdotL), ubo.pointShadowParams.w);

    // Tryb ostrych cieni punktowych/reflektorowych
    if (!softShadows) {
        float sampledDepth = texture(pointShadowMap, dir).r;
        float linearDepth = (nearPlane * farPlane) / max(farPlane - sampledDepth * (farPlane - nearPlane), 1e-4);
        float z_sample = max(abs(dir.x), max(abs(dir.y), abs(dir.z)));
        return (z_sample - bias <= linearDepth) ? 1.0 : 0.0;
    }

    // Tryb miękkich cieni z bazą Frisvada i dyskiem Vogla
    vec3 ndir = normalize(dir);
    vec3 right, bitan;
    if (ndir.z < -0.9999) {
        right = vec3(0.0, -1.0, 0.0);
        bitan = vec3(-1.0, 0.0, 0.0);
    } else {
        float a = 1.0 / (1.0 + ndir.z);
        float b = -ndir.x * ndir.y * a;
        right = vec3(1.0 - ndir.x * ndir.x * a, b, -ndir.x);
        bitan = vec3(b, 1.0 - ndir.y * ndir.y * a, -ndir.y);
    }

    float diskRadius = (0.015 + 0.02 * (currentDist / farPlane));
    float phi = ign * 2.0 * PI;

    float vis = 0.0;
    const int SAMPLES = 12;
    for (int i = 0; i < SAMPLES; ++i) {
        vec2 o = vogelDiskSample(i, SAMPLES, phi) * diskRadius;
        vec3 sampleDir = dir + right * o.x + bitan * o.y;
        float sampledDepth = texture(pointShadowMap, sampleDir).r;
        float linearDepth = (nearPlane * farPlane) / max(farPlane - sampledDepth * (farPlane - nearPlane), 1e-4);
        float z_sample = max(abs(sampleDir.x), max(abs(sampleDir.y), abs(sampleDir.z)));
        vis += (z_sample - bias <= linearDepth) ? 1.0 : 0.0;
    }
    return vis / float(SAMPLES);
}

vec3 evaluateBRDF(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, vec3 F0, float alpha, float roughness, vec3 energyComp, bool softShadows) {
    float rawNdotL = dot(N, L);
    float wrap = softShadows ? (0.14 * (1.0 - metallic) * roughness) : 0.0;
    float wrappedNdotL = clamp((rawNdotL + wrap) / ((1.0 + wrap) * (1.0 + wrap)), 0.0, 1.0);
    if (wrappedNdotL <= 0.0) return vec3(0.0);

    float NdotL = max(rawNdotL, 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float D = distributionGGX(NdotH, alpha);
    float Vis = visibilitySmithGGXCorrelated(NdotV, max(NdotL, 1e-4), alpha);
    vec3 F = fresnelSchlick(VdotH, F0);

    // Odbicie bezpośredniego światła od 100% metalu jest 4x mocniejsze
    float directMetalBoost = mix(1.0, 4.0, metallic);
    vec3 specular = (D * Vis) * F * energyComp * NdotL * directMetalBoost;
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * (albedo / PI) * wrappedNdotL;
    return (diffuse + specular) * radiance;
}

void main() {
    bool softShadows = (ubo.bounceParams.w > 0.5);

    vec3 geomN = normalize(fragNormal);
    vec3 N = computeNormal(geomN);
    vec3 V = normalize(ubo.cameraPos.xyz - fragWorldPos);
    float NdotV = max(dot(N, V), 1e-4);

    vec4 texSample = texture(albedoMap, fragTexCoord);
    vec3 texLinear = texSample.rgb;
    vec3 albedo = clamp(pcs.baseColorFactor.rgb * texLinear * fragColor.rgb, vec3(0.0), vec3(1.0));

    vec3 orm = texture(ormMap, fragTexCoord).rgb;
    float metallic = clamp(pcs.normalCol0.w * orm.b, 0.0, 1.0);
    float rawRoughness = clamp(pcs.normalCol1.w * orm.g, 0.0, 1.0);
    float emissive = max(pcs.normalCol2.w, 0.0);

    vec3 dndu = dFdx(N);
    vec3 dndv = dFdy(N);
    float variance = 0.25 * (dot(dndu, dndu) + dot(dndv, dndv));
    float kernelRoughness2 = min(2.0 * variance, 0.18) * smoothstep(0.04, 0.20, rawRoughness);
    float roughness = clamp(sqrt(rawRoughness * rawRoughness + kernelRoughness2), 0.0, 1.0);
    // Zachowaj fizyczną ostrość i energię odbicia bezpośredniego światła (minimum roughness = 0.04)
    float alphaDirect = max(roughness * roughness, 0.0016);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec2 dfg = texture(brdfLut, vec2(NdotV, roughness)).rg;
    vec3 energyCompensation = 1.0 + F0 * (1.0 / max(dfg.x + dfg.y, 1e-3) - 1.0);

    float ign = interleavedGradientNoise(gl_FragCoord.xy + float(ubo.frameIndex % 64) * vec2(5.588238, 5.588238));
    float viewDepth = max(0.0, -(ubo.view * vec4(fragWorldPos, 1.0)).z);

    vec3 directLighting = vec3(0.0);

    // 1. Światło kierunkowe (Słońce + przełącznik Soft/Hard Shadows)
    float sunIntensity = max(ubo.sunDirection.w, 0.0);
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    float minSunDot = softShadows ? -0.18 : -0.02;
    if (sunIntensity > 0.001 && dot(geomN, sunDir) > minSunDot) {
        float sunShadow = computeDirectionalShadow(fragWorldPos, geomN, sunDir, viewDepth, ign, softShadows);
        vec3 sunRadiance = ubo.sunColor.rgb * sunIntensity * sunShadow;
        directLighting += evaluateBRDF(N, V, sunDir, sunRadiance, albedo, metallic, F0, alphaDirect, roughness, energyCompensation, softShadows);
    }

    // 2. Lokalnie cieniowane światła Point, Spot oraz Area
    int lightCount = min(ubo.numLights, 8);
    int shadowedPointIdx = int(round(ubo.pointShadowParams.x));
    float localLightEnergy = 0.0;

    for (int i = 0; i < lightCount; ++i) {
        vec3 lPos = ubo.lightsPos[i].xyz;
        float lRange = max(ubo.lightsPos[i].w, 0.1);
        vec3 lCol = ubo.lightsCol[i].rgb;
        float lIntensity = max(ubo.lightsCol[i].w, 0.0);
        localLightEnergy += lIntensity;
        int lType = int(round(ubo.lightsDir[i].w));

        vec3 toLight = lPos - fragWorldPos;
        if (lType == 2) {
            vec3 lForward = normalize(ubo.lightsDir[i].xyz);
            vec3 lRight = normalize(cross(lForward, abs(lForward.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
            vec3 lUp = cross(lRight, lForward);
            vec3 rel = fragWorldPos - lPos;
            float halfW = ubo.lightsParams[i].z * 0.5;
            float halfH = ubo.lightsParams[i].w * 0.5;
            vec3 closestPt = lPos
                + lRight * clamp(dot(rel, lRight), -halfW, halfW)
                + lUp * clamp(dot(rel, lUp), -halfH, halfH);
            toLight = closestPt - fragWorldPos;
        }

        float dist = length(toLight);
        if (dist >= lRange || lIntensity <= 0.001) continue;
        vec3 L = toLight / max(dist, 1e-4);

        float distRatio = dist / lRange;
        float window = clamp(1.0 - pow(distRatio, 4.0), 0.0, 1.0);
        float attenuation = (window * window) / (dist * dist + 1.0);

        if (lType == 1) {
            vec3 spotDir = normalize(ubo.lightsDir[i].xyz);
            float cosTheta = dot(-L, spotDir);
            float innerCos = ubo.lightsParams[i].x;
            float outerCos = ubo.lightsParams[i].y;
            attenuation *= smoothstep(outerCos, innerCos, cosTheta);
        }

        if (attenuation <= 1e-5) continue;

        float shadowVis = 1.0;
        if (i == shadowedPointIdx && (lType == 0 || lType == 1)) {
            shadowVis = samplePointLightShadow(toLight, dist, geomN, ign, softShadows);
        }

        vec3 radiance = lCol * (lIntensity * 4.0 * attenuation * shadowVis);
        directLighting += evaluateBRDF(N, V, L, radiance, albedo, metallic, F0, alphaDirect, roughness, energyCompensation, softShadows);
    }

    // 3. Oświetlenie otoczenia Cubemap IBL + Fizyczne GTAO
    vec2 screenUV = clamp(gl_FragCoord.xy * ubo.screenParams.zw, vec2(0.0), vec2(1.0));
    vec2 ssaoTexel = 1.0 / vec2(max(textureSize(ssaoMap, 0), ivec2(1)));
    float curvature = length(dFdx(geomN)) + length(dFdy(geomN));
    vec2 aoOff = ssaoTexel * clamp(curvature * 8.0, 0.0, 1.25);

    vec4 gtaoData = texture(ssaoMap, screenUV) * 0.4
        + texture(ssaoMap, clamp(screenUV + vec2( aoOff.x,  aoOff.y), vec2(0.0), vec2(1.0))) * 0.15
        + texture(ssaoMap, clamp(screenUV + vec2(-aoOff.x,  aoOff.y), vec2(0.0), vec2(1.0))) * 0.15
        + texture(ssaoMap, clamp(screenUV + vec2( aoOff.x, -aoOff.y), vec2(0.0), vec2(1.0))) * 0.15
        + texture(ssaoMap, clamp(screenUV + vec2(-aoOff.x, -aoOff.y), vec2(0.0), vec2(1.0))) * 0.15;

    float ssao = clamp(gtaoData.a, 0.0, 1.0);
    vec3 ssgiDiffuse = sanitizeHDR(gtaoData.rgb);
    float matAO = (pcs.baseColorFactor.a > 0.001) ? min(orm.r, pcs.baseColorFactor.a) : orm.r;
    float combinedAO = clamp(ssao * matAO, 0.0, 1.0);

    float totalSceneLight = sunIntensity + localLightEnergy * 0.25;
    float lightDrivenSkyWeight = smoothstep(0.0, 0.35, totalSceneLight);
    // Local lights must never enable environment lighting. Skybox visibility and
    // IBL are independent switches (bit 1 in skyboxMode).
    float iblIntensity = ((ubo.skyboxMode & 2) != 0)
        ? max(ubo.ambientColor.w, 0.0)
        : 0.0;

    vec3 ambientLighting = vec3(0.0);
    if (iblIntensity > 0.0001) {
        vec3 kS = fresnelSchlickRoughness(NdotV, F0, roughness);
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        vec3 irradiance = sanitizeHDR(texture(irradianceMap, N).rgb);
        vec3 ambientTint = mix(vec3(1.0), ubo.ambientColor.rgb * 2.2, 0.35);
        vec3 diffuseIBL = irradiance * albedo * ambientTint * gtaoMultiBounce(combinedAO, albedo);

        vec3 R = reflect(-V, N);
        float horizonFade = clamp(1.0 + dot(R, geomN), 0.0, 1.0);
        horizonFade *= horizonFade;

        vec3 prefilteredColor = sanitizeHDR(textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb);
        vec3 specularBRDF = (F0 * dfg.x + dfg.y) * energyCompensation;
        float specAO = computeSpecularAO(NdotV, combinedAO, roughness);
        vec3 specularIBL = prefilteredColor * specularBRDF * specAO * horizonFade;

        ambientLighting = (kD * diffuseIBL + specularIBL) * iblIntensity;
    }

    // 3B. Indirect Diffuse GI: SSGI + analityczny Ground Bounce
    float anySceneEnergy = clamp(totalSceneLight, 0.0, 1.0);
    vec3 bounceLighting = vec3(0.0);
    if (ubo.bounceParams.x > 0.5 && anySceneEnergy > 0.0001) {
        float bounceIntensity = max(ubo.bounceParams.y, 0.0);
        float groundAlbedo = clamp(ubo.bounceParams.z, 0.0, 1.0);
        vec3 groundTint = max(ubo.bounceColor.rgb, vec3(0.0));

        vec3 F_bounce = fresnelSchlickRoughness(NdotV, F0, roughness);
        vec3 kD_bounce = (vec3(1.0) - F_bounce) * (1.0 - metallic);

        vec3 ssgiBounce = (kD_bounce * albedo / PI) * ssgiDiffuse * smoothstep(0.0, 0.25, anySceneEnergy);
        float ssgiScreenCoverage = clamp(length(ssgiDiffuse) * 1.5, 0.0, 1.0);

        vec3 groundBounce = vec3(0.0);
        float groundY = 0.0;
        float hemisphereWeight = clamp((1.0 - N.y) * 0.5, 0.0, 1.0);
        float hFrag = max(fragWorldPos.y - groundY, 0.0);

        if (sunIntensity > 0.001 && hemisphereWeight > 0.001) {
            vec3 floorPt = vec3(fragWorldPos.x, groundY, fragWorldPos.z) + vec3(N.x, 0.0, N.z) * 0.5;
            vec3 floorN = vec3(0.0, 1.0, 0.0);

            float floorViewZ = -(ubo.view * vec4(floorPt, 1.0)).z;
            float floorSunShadow = computeDirectionalShadow(floorPt, floorN, sunDir, floorViewZ, ign, softShadows);
            floorSunShadow = mix(floorSunShadow, 1.0, clamp(hFrag / 4.0, 0.0, 1.0));

            float sunOnGround = max(sunDir.y, 0.0) * sunIntensity * floorSunShadow;
            vec3 groundExitance = ubo.sunColor.rgb * sunOnGround * (groundAlbedo * groundTint);

            groundBounce += (kD_bounce * albedo / PI) * groundExitance * hemisphereWeight * combinedAO;
        }

        for (int i = 0; i < lightCount; ++i) {
            vec3 lPos = ubo.lightsPos[i].xyz;
            float lRange = max(ubo.lightsPos[i].w, 0.1);
            vec3 lCol = ubo.lightsCol[i].rgb;
            float lIntensity = ubo.lightsCol[i].w;
            if (lIntensity <= 0.001) continue;

            float hL = lPos.y - groundY;
            if (hL <= 0.0) continue;

            int lType = int(ubo.lightsDir[i].w + 0.5);

            if (lType == 1) {
                vec3 spotDir = normalize(ubo.lightsDir[i].xyz);
                if (spotDir.y < -0.05) {
                    float tFloor = hL / (-spotDir.y);
                    if (tFloor < lRange) {
                        vec3 patchPos = lPos + spotDir * tFloor;
                        vec3 toFrag = fragWorldPos - patchPos;
                        float distSq = dot(toFrag, toFrag);
                        vec3 bounceDir = toFrag * inversesqrt(max(distSq, 1e-4));

                        float dist = length(lPos - fragWorldPos);
                        float rangeFade = clamp(1.0 - dist / lRange, 0.0, 1.0);
                        rangeFade *= rangeFade;

                        float coneFluxWeight = clamp(1.0 - ubo.lightsParams[i].y, 0.02, 1.0);
                        float incidentAtten = (-spotDir.y) / (1.0 + tFloor * tFloor);
                        float formFactor = max(bounceDir.y, 0.0) * max(dot(N, bounceDir), 0.0) / (1.0 + distSq);

                        float bounceAtten = rangeFade * incidentAtten * coneFluxWeight * formFactor;
                        vec3 localGroundExitance = lCol * (lIntensity * bounceAtten) * (groundAlbedo * groundTint);
                        groundBounce += (kD_bounce * albedo / PI) * localGroundExitance * combinedAO;
                    }
                }
                continue;
            }

            vec2 dXZ = fragWorldPos.xz - lPos.xz;
            float dBounceSq = dot(dXZ, dXZ) + (hL + hFrag) * (hL + hFrag);

            float dist = length(lPos - fragWorldPos);
            if (dist >= lRange) continue;
            float rangeFade = clamp(1.0 - dist / lRange, 0.0, 1.0);
            rangeFade *= rangeFade;

            float bounceAtten = rangeFade / (1.0 + dBounceSq);
            vec3 localGroundExitance = lCol * (lIntensity * bounceAtten) * (groundAlbedo * groundTint);
            groundBounce += (kD_bounce * albedo / PI) * localGroundExitance * hemisphereWeight * combinedAO;
        }

        groundBounce *= (1.0 - ssgiScreenCoverage);
        bounceLighting = (ssgiBounce + groundBounce) * bounceIntensity;
    }

    // 4. Emisja
    vec3 emissiveColor = albedo * emissive * 3.5;

    vec3 hdrColor = directLighting + ambientLighting + bounceLighting + emissiveColor;

    // 5. Eksponencjalna mgła wysokościowa + rozpraszanie słoneczne Mie
    if (ubo.fogParams.x > 0.5) {
        float fogDensity = ubo.fogParams.y;
        float heightFalloff = max(ubo.fogParams.z, 1e-4);
        vec3 rayVec = fragWorldPos - ubo.cameraPos.xyz;
        float rayDist = length(rayVec);
        vec3 rayDir = rayVec / max(rayDist, 1e-4);

        float camDensity = fogDensity * exp(-heightFalloff * max(ubo.cameraPos.y, 0.0));
        float opticalDepth = camDensity * rayDist;
        if (abs(rayDir.y) > 1e-4) {
            float tH = heightFalloff * rayVec.y;
            opticalDepth = camDensity * rayDist * (1.0 - exp(-tH)) / tH;
        }
        float fogTransmittance = exp(-clamp(opticalDepth, 0.0, 20.0));

        float cosSun = max(dot(rayDir, sunDir), 0.0);
        const float g = 0.76;
        float miePhase = (1.0 - g * g) / (4.0 * PI * pow(1.0 + g * g - 2.0 * g * cosSun, 1.5));
        float mieWeight = (sunIntensity > 0.001) ? clamp(miePhase * 0.45, 0.0, 0.75) : 0.0;
        vec3 inscatterColor = mix(ubo.fogColor.rgb * anySceneEnergy, ubo.sunColor.rgb * (sunIntensity * 0.35), mieWeight);
        hdrColor = mix(inscatterColor, hdrColor, fogTransmittance);
    }

    outColor = vec4(sanitizeHDR(hdrColor), 1.0);
}