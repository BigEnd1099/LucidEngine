#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragTangent;

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
    if (dot(tangentNormal.xy, tangentNormal.xy) < 1e-5) {
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
    float a2 = alpha * alpha;
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float visibilitySmithGGXCorrelated(float NdotV, float NdotL, float alpha) {
    float a2 = alpha * alpha;
    float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
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

// Lagarde 2014: Fizyczna okluzja odbić zwierciadlanych (eliminuje świecące krawędzie w cieniu)
float computeSpecularAO(float NdotV, float ao, float roughness) {
    return clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

float sampleCascadePCF(int cascadeIdx, vec3 worldPos, vec3 geomN, vec3 L, float phi) {
    float cRadius = max(ubo.cascadeRadii[cascadeIdx], 1.0);
    float texelWorldSize = (2.0 * cRadius) / float(textureSize(shadowMap, 0).x);
    float sinTheta = sqrt(max(0.0, 1.0 - max(dot(geomN, L), 0.0) * max(dot(geomN, L), 0.0)));
    vec3 offsetPos = worldPos + geomN * (texelWorldSize * (1.2 + 2.0 * sinTheta));

    vec4 lsPos = ubo.cascadeLightSpace[cascadeIdx] * vec4(offsetPos, 1.0);
    vec3 proj = lsPos.xyz / lsPos.w;
    proj.xy = proj.xy * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.z < 0.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) {
        return 1.0;
    }

    // Stały promień półcienia w metrach świata (0.045m) przeskalowany do przestrzeni UV danej kaskady
    float uvFilterRadius = clamp(0.045 / (2.0 * cRadius), 0.00035, 0.0035);
    float depthBias = 0.00025 + 0.0006 * sinTheta;

    float visibility = 0.0;
    const int SAMPLES = 16;
    for (int i = 0; i < SAMPLES; ++i) {
        vec2 offset = vogelDiskSample(i, SAMPLES, phi) * uvFilterRadius;
        float closestDepth = texture(shadowMap, vec3(proj.xy + offset, float(cascadeIdx))).r;
        visibility += (proj.z - depthBias <= closestDepth) ? 1.0 : 0.0;
    }
    return visibility / float(SAMPLES);
}

float computeDirectionalShadow(vec3 worldPos, vec3 geomN, vec3 L, float viewDepth, float ign) {
    if (viewDepth >= ubo.cascadeSplits.w) return 1.0;

    int cascadeIdx = 3;
    if (viewDepth < ubo.cascadeSplits.x) cascadeIdx = 0;
    else if (viewDepth < ubo.cascadeSplits.y) cascadeIdx = 1;
    else if (viewDepth < ubo.cascadeSplits.z) cascadeIdx = 2;

    float phi = ign * 2.0 * PI;
    float shadow = sampleCascadePCF(cascadeIdx, worldPos, geomN, L, phi);

    // Płynne przenikanie na ostatnich 15% zasięgu kaskady (brak widocznych szwów CSM)
    if (cascadeIdx < 3) {
        float splitEnd = ubo.cascadeSplits[cascadeIdx];
        float splitStart = (cascadeIdx == 0) ? 0.0 : ubo.cascadeSplits[cascadeIdx - 1];
        float blendBand = (splitEnd - splitStart) * 0.15;
        float blendFactor = clamp((viewDepth - (splitEnd - blendBand)) / max(blendBand, 0.01), 0.0, 1.0);
        if (blendFactor > 0.001) {
            float nextShadow = sampleCascadePCF(cascadeIdx + 1, worldPos, geomN, L, phi);
            shadow = mix(shadow, nextShadow, blendFactor);
        }
    }

    float distFade = 1.0 - smoothstep(ubo.cascadeSplits.w * 0.88, ubo.cascadeSplits.w, viewDepth);
    return mix(1.0, shadow, distFade);
}

float samplePointLightShadow(vec3 toLight, float currentDist, vec3 geomN, float ign) {
    float nearPlane = max(ubo.pointShadowParams.y, 0.01);
    float farPlane = max(ubo.pointShadowParams.z, 1.0);
    if (currentDist >= farPlane) return 1.0;

    vec3 L = normalize(toLight);
    float NdotL = max(dot(geomN, L), 0.0);
    // Lekki offset wzdluz normalnej zapobiegajacy trądzikowi cienia na skosach
    vec3 dir = -toLight + geomN * (0.012 * (1.0 - NdotL));

    vec3 up = abs(dir.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, dir));
    vec3 bitan = cross(dir, right);

    float diskRadius = (0.012 + 0.02 * (currentDist / farPlane));
    float bias = max(0.03 * (1.0 - NdotL), ubo.pointShadowParams.w);
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

float computeAnalyticOcclusion(vec3 p, vec3 lightPos) {
    vec3 seg = lightPos - p;
    float segLen = length(seg);
    if (segLen < 0.05) return 1.0;
    vec3 rd = seg / segLen;
    float visibility = 1.0;

    int count = min(ubo.numOccluders, 32);
    for (int i = 0; i < count; ++i) {
        vec3 center = ubo.occluders[i].centerRadius.xyz;
        float rad = ubo.occluders[i].centerRadius.w;
        vec3 oc = center - p;
        if (length(oc) < rad * 1.02) continue;
        float tClosest = dot(oc, rd);
        if (tClosest <= 0.05 || tClosest >= segLen - 0.05) continue;
        float d2 = dot(oc, oc) - tClosest * tClosest;
        float softRad = rad * 0.85;
        if (d2 < softRad * softRad) {
            float penumbra = smoothstep(softRad * 0.45, softRad, sqrt(max(0.0, d2)));
            visibility = min(visibility, penumbra);
        }
    }
    return visibility;
}

vec3 evaluateBRDF(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, vec3 F0, float alpha, vec3 energyComp) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    float NdotV = max(dot(N, V), 1e-4);
    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float D = distributionGGX(NdotH, alpha);
    float Vis = visibilitySmithGGXCorrelated(NdotV, NdotL, alpha);
    vec3 F = fresnelSchlick(VdotH, F0);

    vec3 specular = (D * Vis) * F * energyComp;
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * (albedo / PI);
    return (diffuse + specular) * radiance * NdotL;
}

void main() {
    vec3 geomN = normalize(fragNormal);
    vec3 N = computeNormal(geomN);
    vec3 V = normalize(ubo.cameraPos.xyz - fragWorldPos);
    float NdotV = max(dot(N, V), 1e-4);

    vec4 texSample = texture(albedoMap, fragTexCoord);
    vec3 texLinear = pow(max(texSample.rgb, vec3(0.0)), vec3(2.2));
    vec3 albedo = clamp(pcs.baseColorFactor.rgb * texLinear, vec3(0.0), vec3(1.0));

    vec3 orm = texture(ormMap, fragTexCoord).rgb;
    float metallic = clamp(pcs.normalCol0.w * orm.b, 0.0, 1.0);
    float roughness = clamp(pcs.normalCol1.w * orm.g, 0.045, 1.0);
    float emissive = max(pcs.normalCol2.w, 0.0);

    // Geometryczne wygładzanie normalnych (Toksvig / Kaplanyan Specular Anti-Aliasing)
    vec3 dndu = dFdx(N);
    vec3 dndv = dFdy(N);
    float variance = 0.25 * (dot(dndu, dndu) + dot(dndv, dndv));
    float kernelRoughness2 = min(2.0 * variance, 0.18);
    float alpha = clamp(sqrt(roughness * roughness + kernelRoughness2), 0.045, 1.0);
    roughness = alpha;
    alpha = roughness * roughness;

    // Fizyczny model energetyczny glTF 2.0 / Filament (mnożenie przez 1 - metallic dokładnie raz w kD)
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Kulla-Conty / Lagarde kompensacja wielokrotnego rozpraszania GGX
    vec2 dfg = texture(brdfLut, vec2(NdotV, roughness)).rg;
    vec3 energyCompensation = 1.0 + F0 * (1.0 / max(dfg.x + dfg.y, 1e-3) - 1.0);

    float ign = interleavedGradientNoise(gl_FragCoord.xy + float(ubo.frameIndex % 64) * vec2(5.588238, 5.588238));
    float viewDepth = max(0.0, -(ubo.view * vec4(fragWorldPos, 1.0)).z);

    vec3 directLighting = vec3(0.0);

    // 1. Światło kierunkowe (Słońce + 4-kaskadowe cienie CSM Vogel PCF)
    float sunIntensity = ubo.sunDirection.w;
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    if (sunIntensity > 0.001 && dot(geomN, sunDir) > -0.05) {
        float sunShadow = computeDirectionalShadow(fragWorldPos, geomN, sunDir, viewDepth, ign);
        vec3 sunRadiance = ubo.sunColor.rgb * sunIntensity * sunShadow;
        directLighting += evaluateBRDF(N, V, sunDir, sunRadiance, albedo, metallic, F0, alpha, energyCompensation);
    }

    // 2. Lokalnie cieniowane światła Point, Spot oraz Area
    int lightCount = min(ubo.numLights, 8);
    int shadowedPointIdx = int(round(ubo.pointShadowParams.x));

    for (int i = 0; i < lightCount; ++i) {
        vec3 lPos = ubo.lightsPos[i].xyz;
        float lRange = max(ubo.lightsPos[i].w, 0.1);
        vec3 lCol = ubo.lightsCol[i].rgb;
        float lIntensity = ubo.lightsCol[i].w;
        int lType = int(round(ubo.lightsDir[i].w));

        vec3 toLight = lPos - fragWorldPos;
        if (lType == 2) {
            // Światło powierzchniowe Area Light (metoda najbliższego punktu na prostokącie Karis 2013)
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

        // Fizyczny zanik Karis/UE5 (okno 1 - (d/R)^4)^2 / (d^2 + 1)
        float distRatio = dist / lRange;
        float window = clamp(1.0 - pow(distRatio, 4.0), 0.0, 1.0);
        float attenuation = (window * window) / (dist * dist + 1.0);

        if (lType == 1) {
            // Stożek reflektora Spot Light
            vec3 spotDir = normalize(ubo.lightsDir[i].xyz);
            float cosTheta = dot(-L, spotDir);
            float innerCos = ubo.lightsParams[i].x;
            float outerCos = ubo.lightsParams[i].y;
            attenuation *= smoothstep(outerCos, innerCos, cosTheta);
        }

        if (attenuation <= 1e-5) continue;

        float shadowVis = 1.0;
        if (i == shadowedPointIdx && (lType == 0 || lType == 1)) {
            shadowVis = samplePointLightShadow(toLight, dist, geomN, ign);
        } else {
            shadowVis = computeAnalyticOcclusion(fragWorldPos, lPos);
        }

        vec3 radiance = lCol * (lIntensity * 4.0 * attenuation * shadowVis);
        directLighting += evaluateBRDF(N, V, L, radiance, albedo, metallic, F0, alpha, energyCompensation);
    }

    // 3. Prekalkulowane na GPU oświetlenie otoczenia Cubemap IBL + Fizyczne GTAO
    vec2 screenUV = gl_FragCoord.xy * ubo.screenParams.zw;
    float ssao = texture(ssaoMap, screenUV).r;
    float matAO = (pcs.baseColorFactor.a > 0.001) ? min(orm.r, pcs.baseColorFactor.a) : orm.r;
    float combinedAO = clamp(ssao * matAO, 0.0, 1.0);

    vec3 kS = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 ambientTint = mix(vec3(1.0), ubo.ambientColor.rgb * 2.2, 0.35);
    vec3 diffuseIBL = irradiance * albedo * ambientTint * gtaoMultiBounce(combinedAO, albedo);

    vec3 R = reflect(-V, N);
    // Tłumienie horyzontu odbić (zapobiega przeciekom światła spod powierzchni)
    float horizonFade = clamp(1.0 + dot(R, geomN), 0.0, 1.0);
    horizonFade *= horizonFade;

    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec3 specularBRDF = (F0 * dfg.x + dfg.y) * energyCompensation;
    float specAO = computeSpecularAO(NdotV, combinedAO, roughness);
    vec3 specularIBL = prefilteredColor * specularBRDF * specAO * horizonFade;

    float iblIntensity = max(ubo.ambientColor.w, 0.0);
    vec3 ambientLighting = (kD * diffuseIBL + specularIBL) * iblIntensity;

    // 4. Emisja (nie podlega tłumieniu przez AO!)
    vec3 emissiveColor = albedo * emissive * 3.5;

    vec3 hdrColor = directLighting + ambientLighting + emissiveColor;

    // 5. Eksponencjalna mgła wysokościowa + rozpraszanie słoneczne Mie (Henyey-Greenstein)
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

        // Faza Mie wokół tarczy słonecznej
        float cosSun = max(dot(rayDir, sunDir), 0.0);
        const float g = 0.76;
        float miePhase = (1.0 - g * g) / (4.0 * PI * pow(1.0 + g * g - 2.0 * g * cosSun, 1.5));
        vec3 inscatterColor = mix(ubo.fogColor.rgb, ubo.sunColor.rgb * max(sunIntensity * 0.35, 0.2), clamp(miePhase * 0.45, 0.0, 0.75));
        hdrColor = mix(inscatterColor, hdrColor, fogTransmittance);
    }

    outColor = vec4(max(hdrColor, vec3(0.0)), 1.0);
}
