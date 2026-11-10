// rt_common.glsl - shared ray-query code for pbr_rt.frag and the RT indirect compute passes.
// The including shader must enable: GL_EXT_ray_query, GL_EXT_buffer_reference2,
// GL_EXT_scalar_block_layout, GL_EXT_shader_explicit_arithmetic_types_int64,
// GL_EXT_nonuniform_qualifier. Descriptor set 0 is the renderer's main scene set.
#ifndef RT_COMMON_GLSL
#define RT_COMMON_GLSL

struct OccluderData {
    vec4 centerRadius;
    vec4 halfExtents;
};

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
    vec4 rtFeatureParams; // x = diffuse GI, y = RT instance count, z = GI intensity
} ubo;
layout(set = 0, binding = 3) uniform samplerCube envCubemap;
layout(set = 0, binding = 11) uniform accelerationStructureEXT sceneTLAS;

struct RtInstanceData {
    mat4 model;
    mat4 normalMatrix;
    vec4 baseColor;
    vec4 material; // metallic, roughness, emissive, AO
    uint64_t vertexAddress;
    uint64_t indexAddress;
    uint vertexCount;
    uint indexCount;
    uvec2 padding;
    uvec4 textureIndices;
};
layout(set = 0, binding = 12, std430) readonly buffer RtInstanceBuffer {
    RtInstanceData instances[];
} rtScene;
layout(set = 0, binding = 13) uniform sampler2D rtAlbedoTextures[256];
layout(set = 0, binding = 14) uniform sampler2D rtNormalTextures[256];
layout(set = 0, binding = 15) uniform sampler2D rtOrmTextures[256];

struct RtVertex {
    vec3 position;
    vec3 normal;
    vec2 texCoord;
    vec4 tangent;
    vec4 color;
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer RtVertexBuffer {
    RtVertex values[];
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer RtIndexBuffer {
    uint values[];
};

const float PI = 3.14159265359;
const float MAX_HDR_VALUE = 65504.0;

vec3 sanitizeHDR(vec3 c) {
    if (any(isnan(c)) || any(isinf(c))) return vec3(0.0);
    return clamp(c, vec3(0.0), vec3(MAX_HDR_VALUE));
}

// ---- Per-pixel / per-frame PCG RNG (replaces IGN/fract-sin style structured noise for RT) ----
uint g_rngState;

uint pcgHash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

void rngInit(uvec2 pixel, uint frame) {
    g_rngState = pcgHash(pixel.x + pcgHash(pixel.y + pcgHash(frame)));
}

float rand1() {
    g_rngState = pcgHash(g_rngState);
    return float(g_rngState >> 8u) * (1.0 / 16777216.0); // [0,1)
}

vec2 rand2() { float a = rand1(); return vec2(a, rand1()); }

// Orthonormal basis without singularity (Duff et al. 2017).
void buildBasis(vec3 n, out vec3 t, out vec3 b) {
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = vec3(c, s + n.y * n.y * a, -n.y);
}

// Cosine-weighted hemisphere (pdf = cos/PI), u in [0,1)^2.
vec3 sampleCosineHemisphere(vec3 n, vec2 u) {
    float r = sqrt(u.x);
    float phi = 2.0 * PI * u.y;
    vec3 t, b;
    buildBasis(n, t, b);
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(1.0 - u.x, 0.0)));
}

vec2 sampleUniformDisk(vec2 u) {
    float r = sqrt(u.x);
    float a = 2.0 * PI * u.y;
    return vec2(cos(a), sin(a)) * r;
}

// Firefly suppression: clamps the luminance of a single path sample.
vec3 clampFirefly(vec3 c, float maxLum) {
    float l = max(max(c.r, c.g), c.b);
    return l > maxLum ? c * (maxLum / l) : c;
}

bool rayQueryOccluded(vec3 origin, vec3 direction, float maxDistance) {
    if (maxDistance <= 0.002) return false;
    rayQueryEXT query;
    rayQueryInitializeEXT(
        query,
        sceneTLAS, gl_RayFlagsTerminateOnFirstHitEXT |
        gl_RayFlagsOpaqueEXT,
        0xFF,
        origin,
        0.002,
        normalize(direction),
        maxDistance
    );
    while (rayQueryProceedEXT(query)) { }
    return rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}

bool traceSceneSurface(
    vec3 origin, vec3 direction, float maxDistance,
    out RtInstanceData hitInstance, out vec3 hitPosition,
    out vec3 hitNormal, out vec2 hitUv, out vec4 hitTangent, out float hitDistance
) {
    rayQueryEXT query;
    rayQueryInitializeEXT(
        query, sceneTLAS,
        gl_RayFlagsOpaqueEXT,
        0xFF, origin, 0.003, normalize(direction), maxDistance
    );
    while (rayQueryProceedEXT(query)) { }

    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT)
        return false;

    uint instanceIndex = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true);
    uint instanceCount = uint(max(ubo.rtFeatureParams.y, 0.0));
    if (instanceIndex >= instanceCount) return false;

    hitInstance = rtScene.instances[instanceIndex];
    uint primitiveIndex = rayQueryGetIntersectionPrimitiveIndexEXT(query, true);
    uint firstIndex = primitiveIndex * 3u;
    if (firstIndex + 2u >= hitInstance.indexCount) return false;

    RtIndexBuffer indices = RtIndexBuffer(hitInstance.indexAddress);
    RtVertexBuffer vertices = RtVertexBuffer(hitInstance.vertexAddress);
    uint i0 = indices.values[firstIndex + 0u];
    uint i1 = indices.values[firstIndex + 1u];
    uint i2 = indices.values[firstIndex + 2u];
    if (i0 >= hitInstance.vertexCount || i1 >= hitInstance.vertexCount || i2 >= hitInstance.vertexCount)
        return false;

    RtVertex v0 = vertices.values[i0];
    RtVertex v1 = vertices.values[i1];
    RtVertex v2 = vertices.values[i2];
    vec2 bary2 = rayQueryGetIntersectionBarycentricsEXT(query, true);
    vec3 bary = vec3(1.0 - bary2.x - bary2.y, bary2.x, bary2.y);

    vec3 localNormal = normalize(v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z);
    hitNormal = normalize(mat3(hitInstance.normalMatrix) * localNormal);
    hitDistance = rayQueryGetIntersectionTEXT(query, true);
    hitPosition = origin + normalize(direction) * hitDistance;
    hitUv = v0.texCoord * bary.x + v1.texCoord * bary.y + v2.texCoord * bary.z;
    vec4 localTangent = v0.tangent * bary.x + v1.tangent * bary.y + v2.tangent * bary.z;
    vec3 worldTangent = mat3(hitInstance.model) * localTangent.xyz;
    worldTangent = normalize(worldTangent - hitNormal * dot(worldTangent, hitNormal));
    hitTangent = vec4(worldTangent, localTangent.w);
    if (dot(hitNormal, direction) > 0.0) hitNormal = -hitNormal;
    return true;
}


vec3 sampleRtSkyRadiance(vec3 dir) {
    if ((ubo.skyboxMode & 3) == 0) {
        return vec3(0.0);
    }
    // Atmospheric analytical sky matching skybox.frag
    vec3 sky = textureLod(envCubemap, dir, 0.0).rgb * max(ubo.ambientColor.w, 0.0);
    if (ubo.sunDirection.w > 0.05) {
        vec3 sunDir = normalize(ubo.sunDirection.xyz);
        float cosTheta = dot(normalize(dir), sunDir);
        float sunDisk = smoothstep(0.99965, 0.99990, cosTheta);
        float sunCorona = pow(max(cosTheta, 0.0), 256.0) * 0.35;
        sky += ubo.sunColor.rgb * ubo.sunDirection.w * (sunDisk * 8.0 + sunCorona);
    }
    return sky;
}

// specSkyScale < 0 keeps legacy scaling; >= 0 explicitly controls environment specular.
// singleLightIdx < 0 evaluates all active lights; >= 0 importance-samples only that single light with singleLightWeight.
// mipLod controls the texture mip level sampled at the hit (higher mip levels on secondary bounces reduce GPU cache thrashing).
vec3 shadeRtHitEx(RtInstanceData hit, vec3 hitPosition, vec3 hitNormal, vec2 hitUv, vec4 hitTangent,
                  vec3 rayDir, bool isDiffuseGI, float specSkyScale,
                  int singleLightIdx, float singleLightWeight, float mipLod) {
    vec4 albedoSample = textureLod(rtAlbedoTextures[nonuniformEXT(hit.textureIndices.x)], hitUv, mipLod);
    vec3 texLinear = pow(max(albedoSample.rgb, vec3(0.0)), vec3(2.2));
    vec3 albedo = clamp(hit.baseColor.rgb * texLinear, vec3(0.0), vec3(1.0));
    vec3 orm = textureLod(rtOrmTextures[nonuniformEXT(hit.textureIndices.z)], hitUv, mipLod).rgb;
    float metallic = clamp(hit.material.x * orm.b, 0.0, 1.0);
    float rawRoughness = clamp(hit.material.y * orm.g, 0.0, 1.0);

    // Roughness Regularization:
    // For diffuse GI: widen GGX lobe to avoid delta fireflies while preserving directionality
    // For reflections: minimum roughness for analytical point lights (sphere light approximation)
    float effRoughness = isDiffuseGI ? clamp(rawRoughness, 0.20, 0.45) : max(rawRoughness, 0.12);

    vec3 shadingNormal = normalize(hitNormal);
    vec3 tangentNormal = textureLod(rtNormalTextures[nonuniformEXT(hit.textureIndices.y)], hitUv, mipLod).xyz * 2.0 - 1.0;
    if (dot(tangentNormal.xy, tangentNormal.xy) > 1e-4) {
        vec3 T = normalize(hitTangent.xyz - dot(hitTangent.xyz, shadingNormal) * shadingNormal);
        vec3 B = cross(shadingNormal, T) * (abs(hitTangent.w) > 0.5 ? sign(hitTangent.w) : 1.0);
        shadingNormal = normalize(mat3(T, B, shadingNormal) * tangentNormal);
    }
    hitNormal = shadingNormal;
    float emissive = max(hit.material.z, 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    // Diffuse is strictly for non-metallic part (pure metals do not scatter diffuse light sideways)
    vec3 diffuse = (albedo / PI);

    vec3 V = -normalize(rayDir);
    float NdotV = max(dot(hitNormal, V), 0.001);

    // Emissive surface: acts as secondary light source
    vec3 radiance = albedo * (emissive * 6.0) + (((ubo.skyboxMode & 2) != 0) ? (albedo * 0.012 * (1.0 - rawRoughness * 0.25)) : vec3(0.0));

    // Microfacet GGX parameters
    float a = max(effRoughness * effRoughness, 1e-5);
    float a2 = a * a;
    float k = (effRoughness + 1.0) * (effRoughness + 1.0) / 8.0;

    // Specular boost for directional bounce in diffuse GI to compensate for cosine sampling
    float giSpecBoost = isDiffuseGI ? mix(1.0, 2.5, metallic * (1.0 - rawRoughness)) : 1.0;

    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    float sunNdotL = max(dot(hitNormal, sunDir), 0.0);
    float sunIntensity = max(ubo.sunDirection.w, 0.0);
    if (sunNdotL > 0.0 && sunIntensity > 0.001 &&
        !rayQueryOccluded(hitPosition + hitNormal * 0.008, sunDir, 10000.0)) {
        vec3 H = normalize(V + sunDir);
        float NdotH = max(dot(hitNormal, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

        float dDenom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
        float D = a2 / max(PI * dDenom * dDenom, 1e-12);
        float g1v = NdotV / (NdotV * (1.0 - k) + k);
        float g1l = sunNdotL / (sunNdotL * (1.0 - k) + k);
        float G = g1v * g1l;
        vec3 F = F0 + (1.0 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);

        vec3 spec = (D * G * F) / max(4.0 * NdotV * sunNdotL, 0.001) * giSpecBoost;
        vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
        radiance += (kd * diffuse + spec) * ubo.sunColor.rgb * (sunIntensity * sunNdotL);
    }

    int numActiveLights = min(ubo.numLights, 8);
    int startLight = 0;
    int endLight = numActiveLights;
    float lightMultiplier = 1.0;

    if (singleLightIdx >= 0 && numActiveLights > 0) {
        startLight = clamp(singleLightIdx, 0, numActiveLights - 1);
        endLight = startLight + 1;
        lightMultiplier = singleLightWeight;
    }

    for (int i = startLight; i < endLight; ++i) {
        vec3 toLight = ubo.lightsPos[i].xyz - hitPosition;
        float distanceToLight = length(toLight);
        float range = max(ubo.lightsPos[i].w, 0.1);
        float intensity = max(ubo.lightsCol[i].w, 0.0);
        if (distanceToLight >= range || intensity <= 0.001) continue;
        vec3 L = toLight / max(distanceToLight, 1e-4);
        float ndotl = max(dot(hitNormal, L), 0.0);
        if (ndotl <= 0.0) continue;

        float ratio = distanceToLight / range;
        float window = clamp(1.0 - pow(ratio, 4.0), 0.0, 1.0);
        float attenuation = window * window / (distanceToLight * distanceToLight + 1.0);
        int type = int(round(ubo.lightsDir[i].w));
        if (type == 1) {
            float cone = dot(-L, normalize(ubo.lightsDir[i].xyz));
            attenuation *= smoothstep(ubo.lightsParams[i].y, ubo.lightsParams[i].x, cone);
        }
        if (attenuation <= 1e-5 ||
            rayQueryOccluded(hitPosition + hitNormal * 0.008, L, max(distanceToLight - 0.02, 0.003)))
            continue;

        vec3 H = normalize(V + L);
        float NdotH = max(dot(hitNormal, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

        float dDenom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
        float D = a2 / max(PI * dDenom * dDenom, 1e-12);
        float g1v = NdotV / (NdotV * (1.0 - k) + k);
        float g1l = ndotl / (ndotl * (1.0 - k) + k);
        float G = g1v * g1l;
        vec3 F = F0 + (1.0 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);

        vec3 spec = (D * G * F) / max(4.0 * NdotV * ndotl, 0.001) * giSpecBoost;
        vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
        radiance += (kd * diffuse + spec) * ubo.lightsCol[i].rgb *
                    (intensity * 4.0 * attenuation * ndotl * lightMultiplier);
    }

    // Ambient / Environment lighting at hit point
    if ((ubo.skyboxMode & 2) != 0) {
        vec3 ambientIBL = sampleRtSkyRadiance(hitNormal) * 0.5;
        vec3 ambientF = F0 + (1.0 - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);
        vec3 ambientKD = (vec3(1.0) - ambientF) * (1.0 - metallic);
        radiance += ambientKD * diffuse * ambientIBL;

        vec3 R = reflect(-V, hitNormal);
        vec3 specSky = sampleRtSkyRadiance(R) * mix(F0, vec3(1.0), pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0)) * (1.0 - rawRoughness);
        float skyScale = (specSkyScale >= 0.0) ? specSkyScale : (isDiffuseGI ? 0.15 : 0.4);
        radiance += specSky * skyScale;
    }

    return sanitizeHDR(radiance);
}

vec3 shadeRtHitEx(RtInstanceData hit, vec3 hitPosition, vec3 hitNormal, vec2 hitUv, vec4 hitTangent, vec3 rayDir, bool isDiffuseGI, float specSkyScale) {
    return shadeRtHitEx(hit, hitPosition, hitNormal, hitUv, hitTangent, rayDir, isDiffuseGI, specSkyScale, -1, 1.0, 0.0);
}

vec3 shadeRtHit(RtInstanceData hit, vec3 hitPosition, vec3 hitNormal, vec2 hitUv, vec4 hitTangent, vec3 rayDir, bool isDiffuseGI) {
    return shadeRtHitEx(hit, hitPosition, hitNormal, hitUv, hitTangent, rayDir, isDiffuseGI, -1.0);
}

vec3 shadeRtHit(RtInstanceData hit, vec3 hitPosition, vec3 hitNormal, vec2 hitUv, vec4 hitTangent, vec3 rayDir) {
    return shadeRtHit(hit, hitPosition, hitNormal, hitUv, hitTangent, rayDir, false);
}

vec3 shadeRtHit(RtInstanceData hit, vec3 hitPosition, vec3 hitNormal, vec2 hitUv, vec4 hitTangent) {
    return shadeRtHit(hit, hitPosition, hitNormal, hitUv, hitTangent, hitNormal, false);
}

#endif
