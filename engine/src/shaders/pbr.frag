#version 450

// ============================================================================
// Lucid Engine - High-Fidelity PBR Shader adapted from Unreal Engine 5.8
// Cook-Torrance GGX / Smith / Schlick BRDF + UE5 Grid + 4-Cascade CSM + IBL
// ============================================================================

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec4 fragLightSpacePos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2DArray shadowMap;
layout(set = 0, binding = 2) uniform samplerCube pointShadowMap;
layout(set = 0, binding = 3) uniform sampler2D environmentMap;
layout(set = 0, binding = 4) uniform sampler2D albedoMap;
layout(set = 0, binding = 5) uniform sampler2D normalMap;
layout(set = 0, binding = 6) uniform sampler2D ormMap;

struct OccluderData {
    vec4 centerRadius; // xyz = center, w = radius
    vec4 halfExtents;  // xyz = half-extents, w = shapeType (0: Box, 1: Sphere, 2: Cylinder)
};

layout(set = 0, binding = 1) uniform SceneUbo {
    mat4 cascadeLightSpace[4];
    vec4 cascadeSplits;
    vec4 sunDirection;      // xyz = sun direction, w = intensity
    vec4 sunPosition;       // xyz = sun world pos, w = positional influence
    vec4 sunColor;          // rgb = sun color, w = unused
    vec4 ambientColor;      // rgb = sky ambient radiance, w = ground/IBL radiance scale
    vec4 fogParams;         // x = enabled (1.0 or 0.0), y = near, z = far, w = antiAliasingMode
    vec4 fogColor;          // rgb = fog color
    vec4 lightsPos[8];      // xyz = pos, w = range
    vec4 lightsCol[8];      // rgb = color, w = intensity
    vec4 lightsDir[8];      // xyz = forward dir, w = type (0: Point, 1: Spot, 2: Area)
    vec4 lightsParams[8];   // x = cos(inner), y = cos(outer), z = sizeX, w = sizeY
    vec4 pointShadowParams; // x = shadowed light index, y = near, z = far, w = bias
    int numLights;
    int numOccluders;
    vec2 padding;
    OccluderData occluders[32];
} scene;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
    mat4 lightSpaceMatrix;
    vec4 baseColorFactor;
    vec4 cameraPos;        // xyz = camera position in world space, w = time
    vec4 materialParams;   // x = metallic, y = roughness, z = isGround, w = emissive
} pcs;

const float PI = 3.14159265358979323846;

// Equirectangular lat-long mapping: +Y (zenit/niebo) -> v = 0.0, -Y (nadir/ziemia) -> v = 1.0
vec2 SampleSphericalMap(vec3 v) {
    vec3 dir = normalize(v);
    vec2 uv = vec2(atan(dir.z, dir.x), asin(clamp(dir.y, -1.0, 1.0)));
    uv *= vec2(0.15915494, -0.31830989); // 1 / (2*PI), -1 / PI (naprawia odwrocona orientacje EXR!)
    uv += 0.5;
    return uv;
}

// Bezpieczny odczyt mapy OpenEXR eliminujacy NaN/Inf oraz ekstremalne piki wypalajace Bloom
vec3 SampleEnvSafe(vec2 uv, float lod) {
    vec3 c = textureLod(environmentMap, uv, lod).rgb;
    if (any(isnan(c)) || any(isinf(c))) {
        return vec3(0.0);
    }
    c = max(c, vec3(0.0));
    float peak = max(max(c.r, c.g), c.b);
    if (peak > 8.0) {
        c *= (8.0 + log2(peak - 7.0)) / peak;
    }
    return c;
}

// Wieloprobkowe calkowanie hemisfery cosinusowej dla Diffuse Irradiance IBL
vec3 SampleDiffuseIrradiance(vec3 N) {
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = cross(N, right);

    const vec3 dirs[8] = vec3[](
        vec3( 0.000,  0.000, 1.000),
        vec3( 0.577,  0.000, 0.816),
        vec3(-0.577,  0.000, 0.816),
        vec3( 0.000,  0.577, 0.816),
        vec3( 0.000, -0.577, 0.816),
        vec3( 0.655,  0.655, 0.378),
        vec3(-0.655,  0.655, 0.378),
        vec3( 0.000, -0.880, 0.475)
    );

    vec3 irradiance = vec3(0.0);
    float weightSum = 0.0;
    for (int i = 0; i < 8; ++i) {
        vec3 sampleDir = normalize(right * dirs[i].x + up * dirs[i].y + N * dirs[i].z);
        float w = max(dirs[i].z, 0.0);
        irradiance += SampleEnvSafe(SampleSphericalMap(sampleDir), 5.5) * w;
        weightSum += w;
    }
    return irradiance / max(weightSum, 1e-4);
}

// Filtrowanie odbic kierunkowych Specular IBL z uwzglednieniem chropowatosci (dziala takze bez mipmap!)
vec3 SampleSpecularPrefiltered(vec3 R, float roughness) {
    float lod = roughness * 6.0;
    vec3 center = SampleEnvSafe(SampleSphericalMap(R), lod);
    if (roughness < 0.10) {
        return center;
    }

    vec3 up = abs(R.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, R));
    up = cross(R, right);
    float spread = roughness * roughness * 0.35;

    vec3 acc = center * 2.0;
    acc += SampleEnvSafe(SampleSphericalMap(normalize(R + right * spread)), lod);
    acc += SampleEnvSafe(SampleSphericalMap(normalize(R - right * spread)), lod);
    acc += SampleEnvSafe(SampleSphericalMap(normalize(R + up * spread)), lod);
    acc += SampleEnvSafe(SampleSphericalMap(normalize(R - up * spread)), lod);
    return acc / 6.0;
}

// Analityczna aproksymacja calki Split-Sum BRDF (Lazarov / UE)
vec2 EnvBRDFApprox(float roughness, float NoV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

float D_GGX(float a2, float NoH) {
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d);
}

float Vis_SmithJointApprox(float a2, float NoV, float NoL) {
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / max(Vis_SmithV + Vis_SmithL, 1e-5);
}

vec3 F_Schlick(vec3 F0, float VoH) {
    float Fc = pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
    return F0 + (vec3(1.0) - F0) * Fc;
}

vec3 F_SchlickRoughness(vec3 F0, float NoV, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - NoV, 0.0, 1.0), 5.0);
}

vec3 Diffuse_Burley(vec3 diffColor, float Roughness, float NoV, float NoL, float VoH) {
    float FD90 = 0.5 + 2.0 * VoH * VoH * Roughness;
    float FdV = 1.0 + (FD90 - 1.0) * pow(clamp(1.0 - NoV, 0.0, 1.0), 5.0);
    float FdL = 1.0 + (FD90 - 1.0) * pow(clamp(1.0 - NoL, 0.0, 1.0), 5.0);
    return diffColor * ((1.0 / PI) * FdV * FdL);
}

// Odporna na NaN generacja przestrzeni stycznej z pochodnych obliczonych w jednorodnym przeplywie
mat3 BuildCotangentFrame(vec3 N, vec3 dp1, vec3 dp2, vec2 duv1, vec2 duv2) {
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float maxLenSq = max(dot(T, T), dot(B, B));
    if (maxLenSq < 1e-10) {
        vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        T = normalize(cross(up, N));
        B = cross(N, T);
        return mat3(T, B, N);
    }
    float invmax = inversesqrt(maxLenSq);
    return mat3(T * invmax, B * invmax, N);
}

// [UE5 PointLightShadow: 16-Tap Vogel Disk Sampling on Depth Cubemap]
float CalculatePointCubemapShadow(vec3 fragPos, vec3 lightPos, vec3 N, float nearPlane, float farPlane) {
    vec3 toFrag = fragPos - lightPos;
    float currentDist = length(toFrag);
    if (currentDist >= farPlane || currentDist <= nearPlane) return 0.0;

    vec3 L = -normalize(toFrag);
    float cosTheta = max(dot(N, L), 0.0);

    // Adaptacyjny normal-bias zalezny od kata padania swiatla (usuwa shadow acne)
    vec3 ro = fragPos + N * (0.015 + 0.035 * (1.0 - cosTheta));
    vec3 biasedToFrag = ro - lightPos;

    // Metryka rzutowana w przestrzeni osiowej scianki cubemapy (naprawia 41%-73% blad euklidesowy)
    float compDist = max(abs(biasedToFrag.x), max(abs(biasedToFrag.y), abs(biasedToFrag.z)));
    if (compDist <= nearPlane || compDist >= farPlane) return 0.0;

    vec3 sampleBaseDir = normalize(biasedToFrag);
    vec3 up = abs(sampleBaseDir.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, sampleBaseDir));
    up = cross(sampleBaseDir, right);

    float filterRadius = 0.012 / (currentDist * 0.10 + 1.0);

    const vec2 diskSamples[16] = vec2[](
        vec2(-0.35, 0.18), vec2(0.12, -0.42), vec2(-0.25, -0.38), vec2(0.52, 0.15),
        vec2(-0.48, -0.25), vec2(0.38, -0.45), vec2(-0.15, 0.58), vec2(0.65, -0.32),
        vec2(-0.62, 0.38), vec2(0.42, 0.62), vec2(-0.35, -0.65), vec2(0.78, 0.22),
        vec2(-0.75, -0.35), vec2(0.25, -0.82), vec2(-0.18, 0.85), vec2(0.0, 0.0)
    );

    float depthBias = max(0.040 * (1.0 - cosTheta), 0.012);
    float shadow = 0.0;
    for (int k = 0; k < 16; ++k) {
        vec3 sDir = normalize(sampleBaseDir + (right * diskSamples[k].x + up * diskSamples[k].y) * filterRadius);
        float sampledDepth = texture(pointShadowMap, sDir).r;
        float linDepth = (nearPlane * farPlane) / max(farPlane - sampledDepth * (farPlane - nearPlane), 1e-4);

        if ((compDist - depthBias) > linDepth) {
            shadow += 1.0;
        }
    }

    return (shadow / 16.0);
}

// Analytical / Raytraced Dynamic Shadow for Point, Spot, and Area Lights
float CalculatePointShadow(vec3 fragPos, vec3 lightPos, vec3 N) {
    vec3 toLight = lightPos - fragPos;
    float distToLight = length(toLight);
    if (distToLight <= 1e-4) return 0.0;
    vec3 L = toLight / distToLight;

    vec3 ro = fragPos + N * 0.035;
    float shadow = 0.0;

    for (int k = 0; k < scene.numOccluders && k < 32; ++k) {
        vec3 center = scene.occluders[k].centerRadius.xyz;
        float radius = scene.occluders[k].centerRadius.w;
        vec3 halfExt = scene.occluders[k].halfExtents.xyz;
        int shapeType = int(scene.occluders[k].halfExtents.w + 0.5);

        vec3 toCenter = center - ro;
        float proj = dot(toCenter, L);
        if (proj <= 0.02 || proj >= (distToLight - 0.05)) continue;

        float distSq = dot(toCenter, toCenter) - proj * proj;
        if (distSq > (radius * radius * 1.5)) continue;

        if (shapeType == 0) {
            vec3 boxMin = center - halfExt;
            vec3 boxMax = center + halfExt;

            vec3 invD = 1.0 / (abs(L) + vec3(1e-7)) * sign(L);
            vec3 t0 = (boxMin - ro) * invD;
            vec3 t1 = (boxMax - ro) * invD;
            vec3 tmin = min(t0, t1);
            vec3 tmax = max(t0, t1);
            float tNear = max(max(tmin.x, tmin.y), tmin.z);
            float tFar = min(min(tmax.x, tmax.y), tmax.z);

            if (tNear <= tFar && tFar > 0.04 && tNear < (distToLight - 0.05)) {
                float penumbra = clamp((distToLight - tNear) / max(distToLight, 1e-3) * 1.25, 0.7, 1.0);
                shadow = max(shadow, penumbra);
            }
        } else {
            float rSq = radius * radius;
            if (distSq < rSq) {
                float penumbra = clamp(1.0 - sqrt(distSq) / radius, 0.7, 1.0);
                shadow = max(shadow, penumbra);
            }
        }

        if (shadow >= 0.98) break;
    }

    return clamp(shadow, 0.0, 1.0);
}

// High-Fidelity 16-Tap Poisson Disk PCF per Cascade (zoptymalizowane pod 4096x4096)
float SampleSingleCascadeShadow(vec3 worldPos, vec3 N, vec3 shadowDirL, int cascadeIdx) {
    float normalOffset = 0.012 * (1.0 + float(cascadeIdx) * 1.25);
    vec4 lightSpacePos = scene.cascadeLightSpace[cascadeIdx] * vec4(worldPos + N * normalOffset, 1.0);

    if (abs(lightSpacePos.w) < 1e-6) return 0.0;
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    if (any(isnan(projCoords)) || any(isinf(projCoords))) return 0.0;

    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    float currentDepth = projCoords.z;
    float cosTheta = max(dot(N, shadowDirL), 0.0);
    float cascadeBiasScale = 1.0 + float(cascadeIdx) * 0.5;
    float bias = max(0.00055 * (1.0 - cosTheta), 0.00012) * cascadeBiasScale;

    float edgeFade = 1.0;
    if (cascadeIdx == 3) {
        vec2 edgeDist = min(projCoords.xy, 1.0 - projCoords.xy);
        edgeFade = clamp(min(edgeDist.x, edgeDist.y) * 12.0, 0.0, 1.0);
    }

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0).xy);

    const vec2 poissonDisk[16] = vec2[](
        vec2(-0.94201624, -0.39906216),
        vec2( 0.94558609, -0.76890725),
        vec2(-0.09418410, -0.92938870),
        vec2( 0.34495938,  0.29387760),
        vec2(-0.91588581,  0.45771432),
        vec2(-0.81544232, -0.87912464),
        vec2(-0.38277543,  0.27676845),
        vec2( 0.97484398,  0.75648379),
        vec2( 0.44323325, -0.97511554),
        vec2( 0.53742981, -0.47373420),
        vec2(-0.26496911, -0.41893023),
        vec2( 0.79197514,  0.19090188),
        vec2(-0.24188840,  0.99706507),
        vec2(-0.81409955,  0.91437590),
        vec2( 0.19984126,  0.78641367),
        vec2( 0.14383161, -0.14100790)
    );

    float filterRadius = 2.0;
    for (int i = 0; i < 16; ++i) {
        vec3 sampleCoord = vec3(projCoords.xy + poissonDisk[i] * texelSize * filterRadius, float(cascadeIdx));
        float pcfDepth = texture(shadowMap, sampleCoord).r;
        if (currentDepth - bias > pcfDepth) {
            shadow += 1.0;
        }
    }
    return (shadow / 16.0) * edgeFade;
}

float CalculateShadow(vec3 worldPos, vec3 N, vec3 shadowDirL) {
    float viewDist = length(worldPos - pcs.cameraPos.xyz);

    int cascadeIdx = 0;
    if (viewDist > scene.cascadeSplits.z) cascadeIdx = 3;
    else if (viewDist > scene.cascadeSplits.y) cascadeIdx = 2;
    else if (viewDist > scene.cascadeSplits.x) cascadeIdx = 1;
    else cascadeIdx = 0;

    float shadow = SampleSingleCascadeShadow(worldPos, N, shadowDirL, cascadeIdx);

    if (cascadeIdx < 3) {
        float splitDist = (cascadeIdx == 0) ? scene.cascadeSplits.x : ((cascadeIdx == 1) ? scene.cascadeSplits.y : scene.cascadeSplits.z);
        float blendStart = splitDist * 0.85;
        if (viewDist > blendStart) {
            float blendAlpha = clamp((viewDist - blendStart) / max(splitDist - blendStart, 1e-4), 0.0, 1.0);
            float nextShadow = SampleSingleCascadeShadow(worldPos, N, shadowDirL, cascadeIdx + 1);
            shadow = mix(shadow, nextShadow, blendAlpha);
        }
    }

    return shadow;
}

void main() {
    vec3 surfN = normalize(fragNormal);
    vec3 N = surfN;
    vec3 V = normalize(pcs.cameraPos.xyz - fragWorldPos);

    // Pochodne ekranowe liczone w jednorodnym przeplywie sterowania (zgodnie ze specyfikacja SPIR-V)
    vec3 dPdx = dFdx(fragWorldPos);
    vec3 dPdy = dFdy(fragWorldPos);
    vec2 dUVdx = dFdx(fragTexCoord);
    vec2 dUVdy = dFdy(fragTexCoord);
    vec2 dGridCoord = fwidth(fragWorldPos.xz);
    vec2 dMajorGridCoord = fwidth(fragWorldPos.xz * 0.2);

    vec3 baseColor = fragColor;
    float metallic = clamp(pcs.materialParams.x, 0.0, 1.0);
    float roughness = clamp(pcs.materialParams.y, 0.04, 1.0);
    float materialAo = 1.0;
    bool isGround = (pcs.materialParams.z > 0.5) || (surfN.y > 0.95 && abs(fragWorldPos.y) < 0.05);

    // PBR Textures (Albedo, Safe Cotangent Screen-Space TBN Normal Mapping, ORM)
    if (!isGround) {
        vec4 texAlbedo = texture(albedoMap, fragTexCoord);
        if (texAlbedo.a > 0.01) {
            baseColor *= texAlbedo.rgb;
        }

        vec3 nMap = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
        if (length(nMap) > 0.1 && (abs(nMap.x) > 0.01 || abs(nMap.y) > 0.01)) {
            mat3 TBN = BuildCotangentFrame(surfN, dPdx, dPdy, dUVdx, dUVdy);
            N = normalize(TBN * nMap);
        }

        vec4 orm = texture(ormMap, fragTexCoord);
        if (orm.a > 0.01 && (orm.r + orm.g + orm.b) < 2.99) {
            materialAo = clamp(orm.r, 0.0, 1.0);
            roughness = clamp(roughness * orm.g, 0.04, 1.0);
            metallic = clamp(metallic * orm.b, 0.0, 1.0);
        }
    }

    // Liniowy kolor horyzontu (w przestrzeni Linear HDR 0.008 odpowiada ciemnemu granatowi po sRGB!)
    vec3 horizonColor = (scene.fogParams.x > 0.5) ? scene.fogColor.rgb : vec3(0.006, 0.008, 0.016);

    if (isGround) {
        vec2 coord = fragWorldPos.xz;

        // 1. Minor Grid (1m)
        vec2 grid = abs(fract(coord - 0.5) - 0.5) / max(dGridCoord, vec2(1e-4));
        float line = min(grid.x, grid.y);
        float lineAlpha = 1.0 - min(line, 1.0);

        // 2. Major Grid (5m)
        vec2 majorCoord = coord * 0.2;
        vec2 majorGrid = abs(fract(majorCoord - 0.5) - 0.5) / max(dMajorGridCoord, vec2(1e-4));
        float majorLine = min(majorGrid.x, majorGrid.y);
        float majorLineAlpha = 1.0 - min(majorLine, 1.0);

        // 3. Central coordinate axes (Red = X, Cyan = Z)
        float axisZ = 1.0 - min(abs(coord.x) / max(dGridCoord.x, 1e-4), 1.0);
        float axisX = 1.0 - min(abs(coord.y) / max(dGridCoord.y, 1e-4), 1.0);

        // Dielektryczny lupek UE5 o zbalansowanym albedo (zapewnia wysoki kontrast cieni slonecznych!)
        vec3 groundSurface  = vec3(0.105, 0.120, 0.150);
        vec3 subGridColor   = vec3(0.160, 0.195, 0.250);
        vec3 majorGridColor = vec3(0.240, 0.310, 0.410);
        vec3 axisCyan       = vec3(0.0, 0.78, 0.92);
        vec3 axisRed        = vec3(0.92, 0.22, 0.32);

        baseColor = mix(groundSurface, subGridColor, lineAlpha * 0.50);
        baseColor = mix(baseColor, majorGridColor, majorLineAlpha * 0.75);
        baseColor = mix(baseColor, axisCyan, axisZ * 0.90);
        baseColor = mix(baseColor, axisRed, axisX * 0.90);

        float dist = length(fragWorldPos.xz - pcs.cameraPos.xz);
        float fog = clamp((dist - 40.0) / 110.0, 0.0, 1.0);
        baseColor = mix(baseColor, horizonColor, fog);

        roughness = 0.78;
        metallic = 0.0;
    }

    float a = roughness * roughness;
    float a2 = a * a;

    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    vec3 diffColor = baseColor * (1.0 - metallic);

    // --- 1. Primary Celestial Sun Light ---
    vec3 sunPos = scene.sunPosition.xyz;
    vec3 orthoSunL = normalize(scene.sunDirection.xyz);
    vec3 L = orthoSunL;

    if (scene.sunPosition.w > 0.05) {
        vec3 toSunPos = sunPos - fragWorldPos;
        float dSun = length(toSunPos);
        if (dSun > 1e-4) {
            vec3 posL = toSunPos / dSun;
            L = normalize(mix(L, posL, clamp(scene.sunPosition.w * 0.75, 0.0, 1.0)));
        }
    }

    vec3 H = normalize(V + L);

    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float NoV = clamp(abs(dot(N, V)) + 1e-5, 0.0, 1.0);
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);

    float D = D_GGX(a2, NoH);
    float Vis = Vis_SmithJointApprox(a2, NoV, NoL);
    vec3 F_sun = F_Schlick(F0, VoH);
    vec3 specular = D * Vis * F_sun;
    vec3 diffuse = Diffuse_Burley(diffColor, roughness, NoV, NoL, VoH);

    float heightFade = 1.0;
    if (scene.sunPosition.w > 0.05) {
        if (fragWorldPos.y >= (sunPos.y - 0.02)) {
            heightFade = 0.0;
        } else if (fragWorldPos.y > (sunPos.y - 0.5)) {
            heightFade = smoothstep(0.0, 1.0, (sunPos.y - fragWorldPos.y) / 0.5);
        }
    } else {
        if (orthoSunL.y <= 0.0) heightFade = 0.0;
    }

    vec3 sunRadiance = scene.sunColor.rgb * scene.sunDirection.w * heightFade;
    float shadow = (heightFade > 0.001 && NoL > 0.0) ? CalculateShadow(fragWorldPos, surfN, orthoSunL) : 0.0;
    vec3 directLighting = (diffuse + specular) * sunRadiance * NoL * (1.0 - shadow);

    // --- 2. Dynamic Multi-Type PBR Lights (Point, Spot, Area) ---
    vec3 pointLighting = vec3(0.0);
    for (int i = 0; i < scene.numLights && i < 8; ++i) {
        vec3 lightPos = scene.lightsPos[i].xyz;
        float range = scene.lightsPos[i].w;
        vec3 lightCol = scene.lightsCol[i].rgb;
        float intensity = scene.lightsCol[i].w;
        int lType = int(scene.lightsDir[i].w + 0.5);

        vec3 lightVec = lightPos - fragWorldPos;
        float dist = length(lightVec);
        if (dist >= range || dist <= 1e-4) continue;

        vec3 pL = lightVec / dist;
        float pNoL = clamp(dot(N, pL), 0.0, 1.0);
        if (pNoL <= 0.0) continue;

        float distRatio = dist / range;
        float win = clamp(1.0 - distRatio * distRatio * distRatio * distRatio, 0.0, 1.0);
        float atten = (win * win) / (dist * dist + 1.0);

        if (lType == 1) {
            vec3 spotDir = normalize(scene.lightsDir[i].xyz);
            float cosAngle = dot(-pL, spotDir);
            float cosInner = scene.lightsParams[i].x;
            float cosOuter = scene.lightsParams[i].y;
            float spotFactor = clamp((cosAngle - cosOuter) / max(cosInner - cosOuter, 0.001), 0.0, 1.0);
            atten *= (spotFactor * spotFactor);
        } else if (lType == 2) {
            vec3 areaDir = normalize(scene.lightsDir[i].xyz);
            float sizeX = max(scene.lightsParams[i].z, 0.1);
            float sizeY = max(scene.lightsParams[i].w, 0.1);

            float forwardDot = dot(-pL, areaDir);
            if (forwardDot <= 0.0) continue;

            vec3 right = normalize(cross(areaDir, abs(areaDir.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
            vec3 up = cross(right, areaDir);

            vec3 toFrag = fragWorldPos - lightPos;
            float px = clamp(dot(toFrag, right), -sizeX * 0.5, sizeX * 0.5);
            float py = clamp(dot(toFrag, up), -sizeY * 0.5, sizeY * 0.5);
            vec3 repPoint = lightPos + right * px + up * py;
            vec3 repVec = repPoint - fragWorldPos;
            float repDist = max(length(repVec), 1e-4);
            pL = repVec / repDist;
            pNoL = clamp(dot(N, pL), 0.0, 1.0);

            float repDistRatio = repDist / range;
            float repWin = clamp(1.0 - repDistRatio * repDistRatio * repDistRatio * repDistRatio, 0.0, 1.0);
            atten = (repWin * repWin) / (repDist * repDist + 1.0) * forwardDot * (1.0 + sizeX * sizeY * 0.15);
        }

        if (atten <= 1e-5) continue;

        vec3 pH = normalize(V + pL);
        float pNoH = clamp(dot(N, pH), 0.0, 1.0);
        float pVoH = clamp(dot(V, pH), 0.0, 1.0);

        float pD = D_GGX(a2, pNoH);
        float pVis = Vis_SmithJointApprox(a2, NoV, pNoL);
        vec3 pF = F_Schlick(F0, pVoH);

        vec3 pSpec = pD * pVis * pF;
        vec3 pDiff = Diffuse_Burley(diffColor, roughness, NoV, pNoL, pVoH);
        vec3 pLightColor = lightCol * (intensity * 3.0);

        float pShadow = 0.0;
        if (int(scene.pointShadowParams.x + 0.5) == i && lType == 0) {
            pShadow = CalculatePointCubemapShadow(fragWorldPos, lightPos, surfN, scene.pointShadowParams.y, scene.pointShadowParams.z);
        } else {
            pShadow = CalculatePointShadow(fragWorldPos, lightPos, surfN);
        }
        pointLighting += (pDiff + pSpec) * pLightColor * pNoL * atten * (1.0 - pShadow);
    }

    // --- 3. Fizycznie skalibrowane Image-Based Lighting (IBL) ---
    vec3 R = reflect(-V, N);
    vec3 kS_ibl = F_SchlickRoughness(F0, NoV, roughness);
    vec3 kD_ibl = (vec3(1.0) - kS_ibl) * (1.0 - metallic);
    vec2 envBrdf = EnvBRDFApprox(roughness, NoV);

    vec3 diffuseIbl  = SampleDiffuseIrradiance(N);
    vec3 specularIbl = SampleSpecularPrefiltered(R, roughness);

    float iblScale = clamp(scene.ambientColor.w, 0.0, 2.0) * 0.25;
    vec3 iblAmbient = (kD_ibl * diffColor * diffuseIbl + specularIbl * (kS_ibl * envBrdf.x + envBrdf.y)) * iblScale;
    vec3 skyFallback = diffColor * scene.ambientColor.rgb * 0.30;
    vec3 ambientLighting = mix(skyFallback, iblAmbient, clamp(scene.ambientColor.w, 0.0, 1.0)) * materialAo;

    // --- 4. Emissive Component ---
    vec3 emissive = baseColor * pcs.materialParams.w * 2.5;

    // Total High Dynamic Range Radiance
    vec3 hdrColor = directLighting + pointLighting + ambientLighting + emissive;

    // --- 5. Distance Fog ---
    if (scene.fogParams.x > 0.5) {
        float fdist = length(fragWorldPos - pcs.cameraPos.xyz);
        float fogFactor = clamp((fdist - scene.fogParams.y) / max(scene.fogParams.z - scene.fogParams.y, 1e-4), 0.0, 1.0);
        hdrColor = mix(hdrColor, scene.fogColor.rgb, fogFactor);
    }

    outColor = vec4(max(hdrColor, vec3(0.0)), 1.0);
}
