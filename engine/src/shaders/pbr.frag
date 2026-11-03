#version 450

// ============================================================================
// Lucid Engine - High-Fidelity PBR Shader adapted from Unreal Engine 5.8
// Cook-Torrance GGX / Smith / Schlick BRDF + UE5 Grid + PCF 16-Tap Soft Shadows + ACES
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
    vec4 ambientColor;      // rgb = sky ambient radiance, w = ground radiance scale
    vec4 fogParams;         // x = enabled (1.0 or 0.0), y = near, z = far, w = antiAliasingMode (0: Off, 1: FXAA Fast, 2: FXAA Quality, 3: TAA)
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

// [UE5 BRDF.ush: D_GGX] Normal Distribution Function
vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(clamp(v.y, -1.0, 1.0)));
    uv *= vec2(0.1591549, 0.3183098); // 1 / (2*pi), 1 / pi
    uv += 0.5;
    return uv;
}

float D_GGX(float a2, float NoH) {
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d);
}

// [UE5 BRDF.ush: Vis_SmithJointApprox] Masking-Shadowing Function
float Vis_SmithJointApprox(float a2, float NoV, float NoL) {
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / max(Vis_SmithV + Vis_SmithL, 1e-5);
}

// [UE5 BRDF.ush: F_Schlick] Fresnel Approximation
vec3 F_Schlick(vec3 F0, float VoH) {
    float Fc = pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
    return F0 + (vec3(1.0) - F0) * Fc;
}

// [UE5 BRDF.ush: Diffuse_Burley] Energy-Conserving Disney/Burley Diffuse
vec3 Diffuse_Burley(vec3 diffColor, float Roughness, float NoV, float NoL, float VoH) {
    float FD90 = 0.5 + 2.0 * VoH * VoH * Roughness;
    float FdV = 1.0 + (FD90 - 1.0) * pow(clamp(1.0 - NoV, 0.0, 1.0), 5.0);
    float FdL = 1.0 + (FD90 - 1.0) * pow(clamp(1.0 - NoL, 0.0, 1.0), 5.0);
    return diffColor * ((1.0 / PI) * FdV * FdL);
}

// [UE5 TonemapCommon.ush: ACESFilm] ACES Filmic Tone Mapping Curve
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// [UE5 ShadowPercentageCloserFiltering.ush: High-Fidelity 16-Tap Poisson Filter + Slope Bias + Edge Fade]
// [UE5 PointLightShadow: 16-Tap Vogel Disk Sampling on Depth Cubemap]
float CalculatePointCubemapShadow(vec3 fragPos, vec3 lightPos, vec3 N, float nearPlane, float farPlane) {
    vec3 toFrag = fragPos - lightPos;
    float currentDist = length(toFrag);
    if (currentDist >= farPlane || currentDist <= nearPlane) return 0.0;

    // Normal offset bias to avoid self-shadowing acne
    vec3 ro = fragPos + N * 0.035;
    vec3 biasedToFrag = ro - lightPos;
    float biasedDist = length(biasedToFrag);
    vec3 sampleBaseDir = normalize(biasedToFrag);

    // Coordinate frame around direction
    vec3 up = abs(sampleBaseDir.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, sampleBaseDir));
    up = cross(sampleBaseDir, right);

    float filterRadius = 0.025 / (currentDist * 0.15 + 1.0);

    const vec2 diskSamples[16] = vec2[](
        vec2(-0.35, 0.18), vec2(0.12, -0.42), vec2(-0.25, -0.38), vec2(0.52, 0.15),
        vec2(-0.48, -0.25), vec2(0.38, -0.45), vec2(-0.15, 0.58), vec2(0.65, -0.32),
        vec2(-0.62, 0.38), vec2(0.42, 0.62), vec2(-0.35, -0.65), vec2(0.78, 0.22),
        vec2(-0.75, -0.35), vec2(0.25, -0.82), vec2(-0.18, 0.85), vec2(0.0, 0.0)
    );

    float shadow = 0.0;
    for (int k = 0; k < 16; ++k) {
        vec3 sDir = normalize(sampleBaseDir + (right * diskSamples[k].x + up * diskSamples[k].y) * filterRadius);
        float sampledDepth = texture(pointShadowMap, sDir).r;

        // Linearize depth: (near * far) / (far - sampledDepth * (far - near))
        float linDepth = (nearPlane * farPlane) / max(farPlane - sampledDepth * (farPlane - nearPlane), 1e-4);

        if ((biasedDist - 0.035) > linDepth) {
            shadow += 1.0;
        }
    }

    return (shadow / 16.0) * 0.92;
}

// Analytical / Raytraced Dynamic Shadow for Point, Spot, and Area Lights
float CalculatePointShadow(vec3 fragPos, vec3 lightPos, vec3 N) {
    vec3 toLight = lightPos - fragPos;
    float distToLight = length(toLight);
    if (distToLight <= 1e-4) return 0.0;
    vec3 L = toLight / distToLight;

    // Normal offset bias to eliminate self-shadowing acne
    vec3 ro = fragPos + N * 0.035;

    float shadow = 0.0;

    for (int k = 0; k < scene.numOccluders && k < 32; ++k) {
        vec3 center = scene.occluders[k].centerRadius.xyz;
        float radius = scene.occluders[k].centerRadius.w;
        vec3 halfExt = scene.occluders[k].halfExtents.xyz;
        int shapeType = int(scene.occluders[k].halfExtents.w + 0.5);

        // Quick spherical bounding check
        vec3 toCenter = center - ro;
        float proj = dot(toCenter, L);

        // Occluder must be strictly between the surface and the light source
        if (proj <= 0.02 || proj >= (distToLight - 0.05)) continue;

        float distSq = dot(toCenter, toCenter) - proj * proj;
        if (distSq > (radius * radius * 1.5)) continue;

        // Exact Box Ray-AABB Intersection
        if (shapeType == 0) { // Box
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
                // Soft penumbra based on occluder distance
                float penumbra = clamp((distToLight - tNear) / max(distToLight, 1e-3) * 1.25, 0.7, 1.0);
                shadow = max(shadow, penumbra);
            }
        } else { // Sphere / Capsule approximation
            float rSq = radius * radius;
            if (distSq < rSq) {
                float penumbra = clamp(1.0 - sqrt(distSq) / radius, 0.7, 1.0);
                shadow = max(shadow, penumbra);
            }
        }

        if (shadow >= 0.98) break; // Early out if fully occluded
    }

    return clamp(shadow * 0.90, 0.0, 1.0);
}

// High-Fidelity 16-Tap Poisson Disk PCF per Cascade
float SampleSingleCascadeShadow(vec4 lightSpacePos, vec3 N, vec3 L, int cascadeIdx) {
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Check bounds of shadow frustum
    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    float currentDepth = projCoords.z;
    float cosTheta = max(dot(N, L), 0.0);
    // Slope-scaled depth bias scaled slightly per cascade
    float cascadeBiasScale = 1.0 + float(cascadeIdx) * 0.45;
    float bias = max(0.0016 * (1.0 - cosTheta), 0.00045) * cascadeBiasScale;

    // Smooth edge fade on the outermost boundary of the last cascade
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

    float filterRadius = 1.4;
    for (int i = 0; i < 16; ++i) {
        vec3 sampleCoord = vec3(projCoords.xy + poissonDisk[i] * texelSize * filterRadius, float(cascadeIdx));
        float pcfDepth = texture(shadowMap, sampleCoord).r;
        if (currentDepth - bias > pcfDepth) {
            shadow += 1.0;
        }
    }
    return (shadow / 16.0) * edgeFade * 0.85;
}

// Cascaded Shadow Mapping with 4 Cascades & Seamless Transition Blending
float CalculateShadow(vec3 worldPos, vec3 N, vec3 L) {
    float viewDist = length(worldPos - pcs.cameraPos.xyz);

    int cascadeIdx = 0;
    if (viewDist > scene.cascadeSplits.z) cascadeIdx = 3;
    else if (viewDist > scene.cascadeSplits.y) cascadeIdx = 2;
    else if (viewDist > scene.cascadeSplits.x) cascadeIdx = 1;
    else cascadeIdx = 0;

    vec4 lightSpacePos = scene.cascadeLightSpace[cascadeIdx] * vec4(worldPos, 1.0);
    float shadow = SampleSingleCascadeShadow(lightSpacePos, N, L, cascadeIdx);

    // Smooth blending between adjacent cascades in boundary overlap zone
    if (cascadeIdx < 3) {
        float splitDist = (cascadeIdx == 0) ? scene.cascadeSplits.x : ((cascadeIdx == 1) ? scene.cascadeSplits.y : scene.cascadeSplits.z);
        float blendStart = splitDist * 0.82;
        if (viewDist > blendStart) {
            float blendAlpha = clamp((viewDist - blendStart) / max(splitDist - blendStart, 1e-4), 0.0, 1.0);
            vec4 nextLightSpacePos = scene.cascadeLightSpace[cascadeIdx + 1] * vec4(worldPos, 1.0);
            float nextShadow = SampleSingleCascadeShadow(nextLightSpacePos, N, L, cascadeIdx + 1);
            shadow = mix(shadow, nextShadow, blendAlpha);
        }
    }

    return shadow;
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(pcs.cameraPos.xyz - fragWorldPos);

    vec3 baseColor = fragColor;
    float metallic = clamp(pcs.materialParams.x, 0.0, 1.0);
    float roughness = clamp(pcs.materialParams.y, 0.04, 1.0);
    bool isGround = (pcs.materialParams.z > 0.5) || (N.y > 0.8 && abs(fragWorldPos.y) < 0.2);

    // PBR Textures (Albedo, Tangent Normal Mapping via Screen-Space TBN, ORM)
    if (!isGround) {
        vec4 texAlbedo = texture(albedoMap, fragTexCoord);
        if (texAlbedo.a > 0.01) {
            baseColor *= texAlbedo.rgb;
        }

        vec3 dPdx = dFdx(fragWorldPos);
        vec3 dPdy = dFdy(fragWorldPos);
        vec2 dUVdx = dFdx(fragTexCoord);
        vec2 dUVdy = dFdy(fragTexCoord);
        vec3 surfN = normalize(fragNormal);
        vec3 T = normalize(dPdx * dUVdy.y - dPdy * dUVdx.y);
        vec3 B = -normalize(cross(surfN, T));
        mat3 TBN = mat3(T, B, surfN);

        vec3 nMap = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
        if (length(nMap) > 0.1 && abs(nMap.z) < 0.999) {
            N = normalize(TBN * nMap);
        }

        vec4 orm = texture(ormMap, fragTexCoord);
        if (orm.a > 0.01 && (orm.r + orm.g + orm.b) < 2.99) {
            roughness = clamp(roughness * orm.g, 0.04, 1.0);
            metallic = clamp(metallic * orm.b, 0.0, 1.0);
        }
    }

    vec3 horizonColor = (scene.fogParams.x > 0.5) ? scene.fogColor.rgb : vec3(0.06, 0.07, 0.09);

    if (isGround) {
        vec2 coord = fragWorldPos.xz;
        vec2 dcoord = fwidth(coord);
        
        // 1. Minor Grid (1 unit)
        vec2 grid = abs(fract(coord - 0.5) - 0.5) / max(dcoord, vec2(1e-4));
        float line = min(grid.x, grid.y);
        float lineAlpha = 1.0 - min(line, 1.0);

        // 2. Major Grid (5 units)
        vec2 majorCoord = coord * 0.2;
        vec2 dmajorCoord = fwidth(majorCoord);
        vec2 majorGrid = abs(fract(majorCoord - 0.5) - 0.5) / max(dmajorCoord, vec2(1e-4));
        float majorLine = min(majorGrid.x, majorGrid.y);
        float majorLineAlpha = 1.0 - min(majorLine, 1.0);

        // 3. Central coordinate axes (Red = X-axis, Cyan = Z-axis)
        float axisZ = 1.0 - min(abs(coord.x) / max(dcoord.x, 1e-4), 1.0);
        float axisX = 1.0 - min(abs(coord.y) / max(dcoord.y, 1e-4), 1.0);

        vec3 groundSurface = vec3(0.04, 0.045, 0.06);
        vec3 subGridColor   = vec3(0.09, 0.12, 0.17);
        vec3 majorGridColor = vec3(0.14, 0.22, 0.32);
        vec3 axisCyan       = vec3(0.0, 0.85, 1.0);
        vec3 axisRed        = vec3(1.0, 0.25, 0.35);

        baseColor = mix(groundSurface, subGridColor, lineAlpha * 0.45);
        baseColor = mix(baseColor, majorGridColor, majorLineAlpha * 0.75);
        baseColor = mix(baseColor, axisCyan, axisZ * 0.95);
        baseColor = mix(baseColor, axisRed, axisX * 0.95);

        // Distance fog fade into viewport horizon
        float dist = length(fragWorldPos.xz - pcs.cameraPos.xz);
        float fog = clamp((dist - 25.0) / 100.0, 0.0, 1.0);
        baseColor = mix(baseColor, horizonColor, fog);

        roughness = 0.35;
        metallic = 0.1;
    }

    float a = roughness * roughness;
    float a2 = a * a;

    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    vec3 diffColor = baseColor * (1.0 - metallic);

    // --- 1. Primary Celestial Sun Light (with Height & Position Dynamics) ---
    vec3 sunPos = scene.sunPosition.xyz;
    vec3 L = normalize(scene.sunDirection.xyz);

    // If positional sun influence is enabled, blend directional vector with vector towards sun
    if (scene.sunPosition.w > 0.05) {
        vec3 toSunPos = sunPos - fragWorldPos;
        float dSun = length(toSunPos);
        if (dSun > 1e-4) {
            vec3 posL = normalize(toSunPos);
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
    vec3 F = F_Schlick(F0, VoH);
    vec3 specular = D * Vis * F;
    vec3 diffuse = Diffuse_Burley(diffColor, roughness, NoV, NoL, VoH);

    // Altitude cutoff: Surfaces placed higher than or at the sun do not receive downward sunlight
    float heightFade = 1.0;
    if (scene.sunPosition.w > 0.05) {
        if (fragWorldPos.y >= (sunPos.y - 0.02)) {
            heightFade = 0.0;
        } else if (fragWorldPos.y > (sunPos.y - 0.5)) {
            heightFade = smoothstep(0.0, 1.0, (sunPos.y - fragWorldPos.y) / 0.5);
        }
    } else {
        if (sunPos.y <= 0.0) heightFade = 0.0;
    }

    vec3 sunColor = scene.sunColor.rgb * scene.sunDirection.w * heightFade;
    float shadow = (heightFade > 0.001) ? CalculateShadow(fragWorldPos, N, L) : 0.0;
    vec3 directLighting = (diffuse + specular) * sunColor * NoL * (1.0 - shadow);

    // --- 2. Dynamic Multi-Type PBR Lights (Point, Spot, Area) ---
    vec3 pointLighting = vec3(0.0);
    for (int i = 0; i < scene.numLights && i < 8; ++i) {
        vec3 lightPos = scene.lightsPos[i].xyz;
        float range = scene.lightsPos[i].w;
        vec3 lightCol = scene.lightsCol[i].rgb;
        float intensity = scene.lightsCol[i].w;
        int lType = int(scene.lightsDir[i].w + 0.5); // 0 = Point, 1 = Spot, 2 = Area

        vec3 lightVec = lightPos - fragWorldPos;
        float dist = length(lightVec);

        if (dist >= range || dist <= 1e-4) continue;

        vec3 pL = normalize(lightVec);
        float pNoL = clamp(dot(N, pL), 0.0, 1.0);
        if (pNoL <= 0.0) continue;

        // Base distance attenuation (Karis / UE5 smooth windowed inverse-square)
        float distRatio = dist / range;
        float win = clamp(1.0 - distRatio * distRatio * distRatio * distRatio, 0.0, 1.0);
        float atten = (win * win) / (dist * dist + 1.0);

        if (lType == 1) {
            // === SPOT LIGHT (Conical angular attenuation) ===
            vec3 spotDir = normalize(scene.lightsDir[i].xyz);
            float cosAngle = dot(-pL, spotDir);
            float cosInner = scene.lightsParams[i].x;
            float cosOuter = scene.lightsParams[i].y;
            float spotFactor = clamp((cosAngle - cosOuter) / max(cosInner - cosOuter, 0.001), 0.0, 1.0);
            spotFactor = spotFactor * spotFactor; // smooth hermite falloff
            atten *= spotFactor;
        } else if (lType == 2) {
            // === AREA LIGHT (Soft rectangular emitter) ===
            vec3 areaDir = normalize(scene.lightsDir[i].xyz);
            float sizeX = max(scene.lightsParams[i].z, 0.1);
            float sizeY = max(scene.lightsParams[i].w, 0.1);

            float forwardDot = dot(-pL, areaDir);
            if (forwardDot <= 0.0) continue;

            vec3 right = normalize(cross(areaDir, vec3(0.0, 1.0, 0.0)));
            if (length(cross(areaDir, vec3(0.0, 1.0, 0.0))) < 0.1) right = vec3(1.0, 0.0, 0.0);
            vec3 up = cross(right, areaDir);

            vec3 toFrag = fragWorldPos - lightPos;
            float px = clamp(dot(toFrag, right), -sizeX * 0.5, sizeX * 0.5);
            float py = clamp(dot(toFrag, up), -sizeY * 0.5, sizeY * 0.5);
            vec3 repPoint = lightPos + right * px + up * py;
            vec3 repVec = repPoint - fragWorldPos;
            float repDist = length(repVec);
            pL = normalize(repVec);
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
        vec3 pLightColor = lightCol * (intensity * 3.5);

        float pShadow = 0.0;
        if (int(scene.pointShadowParams.x + 0.5) == i && lType == 0) {
            pShadow = CalculatePointCubemapShadow(fragWorldPos, lightPos, N, scene.pointShadowParams.y, scene.pointShadowParams.z);
        } else {
            pShadow = CalculatePointShadow(fragWorldPos, lightPos, N);
        }
        pointLighting += (pDiff + pSpec) * pLightColor * pNoL * atten * (1.0 - pShadow);
    }

    // --- 3. Image-Based Lighting (IBL) via Pre-filtered Equirectangular HDRI ---
    vec3 R = reflect(-V, N);
    vec2 reflUv = SampleSphericalMap(R);
    vec2 diffUv = SampleSphericalMap(N);

    // Specular IBL (roughness-dependent blur level)
    vec3 specularIbl = textureLod(environmentMap, reflUv, roughness * 6.0).rgb;
    // Diffuse IBL irradiance
    vec3 diffuseIbl = textureLod(environmentMap, diffUv, 6.5).rgb;

    vec3 iblAmbient = (diffColor * diffuseIbl + specularIbl * F);
    vec3 skyRadiance = scene.ambientColor.rgb;
    vec3 ambientLighting = mix(diffColor * skyRadiance, iblAmbient, clamp(scene.ambientColor.w * 0.8, 0.15, 1.0));

    // --- 4. Emissive Component ---
    vec3 emissive = baseColor * pcs.materialParams.w * 3.0;

    // Total High Dynamic Range Radiance
    vec3 hdrColor = directLighting + pointLighting + ambientLighting + emissive;

    // --- 5. Volumetric / Distance Fog ---
    if (scene.fogParams.x > 0.5) {
        float fdist = length(fragWorldPos - pcs.cameraPos.xyz);
        float fogFactor = clamp((fdist - scene.fogParams.y) / max(scene.fogParams.z - scene.fogParams.y, 1e-4), 0.0, 1.0);
        hdrColor = mix(hdrColor, scene.fogColor.rgb, fogFactor);
    }

    // --- 6. Anti-Aliasing Post-Filter (FXAA & Analytical Geometric Edge AA) ---
    float aaMode = scene.fogParams.w;
    if (aaMode > 0.5) {
        vec3 dX = dFdx(hdrColor);
        vec3 dY = dFdy(hdrColor);
        float edgeStrength = length(dX) + length(dY);
        if (edgeStrength > 0.08) {
            float blend = clamp(edgeStrength * (aaMode > 1.5 ? 0.35 : 0.20), 0.0, 0.45);
            hdrColor = mix(hdrColor, (hdrColor + 0.5 * (dX + dY)), -blend * 0.5);
        }
    }

    // Output full High Dynamic Range (HDR) radiance to R16G16B16A16_SFLOAT target
    outColor = vec4(hdrColor, 1.0);
}
