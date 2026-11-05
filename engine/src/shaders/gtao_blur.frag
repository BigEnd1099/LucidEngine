#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outAOGI; // rgb = indirect diffuse GI, a = AO

layout(set = 0, binding = 0) uniform sampler2D aoTex;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(push_constant) uniform BlurPush {
    vec4 params;       // x = invWidth, y = invHeight, z = zNear, w = zFar
    vec4 screenParams; // x = tanHalfFovY, y = aspect, z = unused, w = unused
} pcs;

float linearizeDepth(float d) {
    float zNear = pcs.params.z;
    float zFar = pcs.params.w;
    return (zNear * zFar) / max(zFar - d * (zFar - zNear), 1e-6);
}

vec3 uvToViewPos(vec2 uv, float rawDepth) {
    float z = linearizeDepth(rawDepth);
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return vec3(ndc.x * pcs.screenParams.x * pcs.screenParams.y * z, ndc.y * pcs.screenParams.x * z, -z);
}

vec3 reconstructNormal(vec2 uv, float centerDepth) {
    vec2 offset = pcs.params.xy;
    float dR = texture(depthTex, uv + vec2(offset.x, 0.0)).r;
    float dU = texture(depthTex, uv + vec2(0.0, offset.y)).r;
    vec3 pC = uvToViewPos(uv, centerDepth);
    vec3 pR = uvToViewPos(uv + vec2(offset.x, 0.0), dR);
    vec3 pU = uvToViewPos(uv + vec2(0.0, offset.y), dU);
    return normalize(cross(pR - pC, pU - pC));
}

void main() {
    float rawCenter = texture(depthTex, fragUV).r;
    if (rawCenter >= 0.9999) {
        outAOGI = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float centerZ = linearizeDepth(rawCenter);
    float depthSigma = max(0.05, centerZ * 0.035);
    const float spatialWeights[3] = float[](1.0, 0.85, 0.55);

    vec3 centerN = reconstructNormal(fragUV, rawCenter);

    vec4 sumVal = vec4(0.0);
    float sumWeight = 0.0;

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 sampleUV = fragUV + vec2(float(x), float(y)) * pcs.params.xy;
            float sRawDepth = texture(depthTex, sampleUV).r;
            float sZ = linearizeDepth(sRawDepth);

            float dz = abs(sZ - centerZ) / depthSigma;
            float wDepth = exp(-dz * dz);
            float wSpatial = spatialWeights[abs(x)] * spatialWeights[abs(y)];

            // WAGA NORMALNYCH: zapobiega rozmywaniu w poprzek stykow 90 stopni (np. sciana kostki z podloga)
            vec3 tapN = reconstructNormal(sampleUV, sRawDepth);
            float normalWeight = pow(max(dot(centerN, tapN), 0.0), 8.0);

            float w = wDepth * wSpatial * normalWeight;

            vec4 sVal = texture(aoTex, sampleUV);
            sumVal += sVal * w;
            sumWeight += w;
        }
    }

    outAOGI = sumVal / max(sumWeight, 1e-5);
}
