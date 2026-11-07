#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragTangent;
layout(location = 4) in vec4 currClipPos;
layout(location = 5) in vec4 prevClipPos;

layout(location = 0) out vec4 outNormalRoughness; // xyz: normal, w: roughness
layout(location = 1) out vec4 outAlbedoMetallic;  // rgb: albedo (linear), a: metallic
layout(location = 2) out vec2 outMotion;          // rg: screen-space velocity in UV space

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
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return normalize(mat3(T * invmax, B * invmax, N) * tangentNormal);
}

void main() {
    vec4 albedoTex = texture(albedoMap, fragTexCoord);
    vec3 albedoLinear = pow(max(albedoTex.rgb, vec3(0.0)), vec3(2.2)) * pcs.baseColorFactor.rgb;

    vec3 N = normalize(fragNormal);
    vec3 finalNormal = computeNormal(N);

    vec3 orm = texture(ormMap, fragTexCoord).rgb;
    float roughness = clamp(orm.g * pcs.normalCol1.w, 0.0, 1.0);
    float metallic  = clamp(orm.b * pcs.normalCol0.w, 0.0, 1.0);

    vec2 curNDC  = currClipPos.xy / max(currClipPos.w, 1e-5);
    vec2 prevNDC = prevClipPos.xy / max(prevClipPos.w, 1e-5);
    // Convert NDC delta to UV space delta
    vec2 velocity = (curNDC - prevNDC) * 0.5;

    outNormalRoughness = vec4(finalNormal, roughness);
    outAlbedoMetallic  = vec4(albedoLinear, metallic);
    outMotion          = velocity;
}
