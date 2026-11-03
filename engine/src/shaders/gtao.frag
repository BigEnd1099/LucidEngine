#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uDepthTexture;

layout(push_constant) uniform GtaoConstants {
    vec4 params;       // x = radius (0.8m), y = intensity (1.5), z = fovRadians, w = aspect
    vec4 screenParams; // xy = texelSize, z = near (0.1), w = far (1000.0)
} pc;

// Reconstruct view-space position from non-linear depth buffer
vec3 ReconstructViewPos(vec2 uv, float depth) {
    float near = pc.screenParams.z;
    float far = pc.screenParams.w;
    float z = (near * far) / max(far - depth * (far - near), 1e-5);

    vec2 ndc = uv * 2.0 - 1.0;
    float tanHalfFov = tan(pc.params.z * 0.5);
    vec3 viewPos;
    viewPos.x = ndc.x * z * tanHalfFov * pc.params.w;
    viewPos.y = ndc.y * z * tanHalfFov;
    viewPos.z = z;
    return viewPos;
}

// Jorge Jimenez Interleaved Gradient Noise (IGN) - much smoother and regular than white noise
float InterleavedGradientNoise(vec2 coord) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(coord, magic.xy)));
}

void main() {
    float centerDepth = texture(uDepthTexture, inUV).r;
    if (centerDepth >= 0.9999) {
        outColor = vec4(0.0);
        return;
    }

    vec2 texelSize = pc.screenParams.xy;
    vec3 centerPos = ReconstructViewPos(inUV, centerDepth);

    // Reconstruct view-space normal from depth neighbor differences (screen-space gradients)
    float depthR = texture(uDepthTexture, inUV + vec2(texelSize.x, 0.0)).r;
    float depthL = texture(uDepthTexture, inUV - vec2(texelSize.x, 0.0)).r;
    float depthU = texture(uDepthTexture, inUV + vec2(0.0, texelSize.y)).r;
    float depthD = texture(uDepthTexture, inUV - vec2(0.0, texelSize.y)).r;

    vec3 posR = ReconstructViewPos(inUV + vec2(texelSize.x, 0.0), depthR);
    vec3 posL = ReconstructViewPos(inUV - vec2(texelSize.x, 0.0), depthL);
    vec3 posU = ReconstructViewPos(inUV + vec2(0.0, texelSize.y), depthU);
    vec3 posD = ReconstructViewPos(inUV - vec2(0.0, texelSize.y), depthD);

    vec3 dX = (abs(posR.z - centerPos.z) < abs(posL.z - centerPos.z)) ? (posR - centerPos) : (centerPos - posL);
    vec3 dY = (abs(posU.z - centerPos.z) < abs(posD.z - centerPos.z)) ? (posU - centerPos) : (centerPos - posD);
    vec3 N = normalize(cross(dX, dY));

    float aoRadius = pc.params.x > 0.01 ? pc.params.x : 0.8;
    float aoIntensity = pc.params.y > 0.01 ? pc.params.y : 1.5;

    // Projected radius in screen pixels
    float projRadius = (aoRadius / max(centerPos.z, 0.1)) * (0.5 / max(tan(pc.params.z * 0.5), 1e-4));
    projRadius = clamp(projRadius, 2.0, 48.0);

    float noise = InterleavedGradientNoise(gl_FragCoord.xy);
    float angleOffset = noise * 3.14159265;

    float totalOcclusion = 0.0;
    const int NUM_DIRECTIONS = 4;
    const int NUM_STEPS = 4;

    for (int d = 0; d < NUM_DIRECTIONS; ++d) {
        float phi = angleOffset + float(d) * (3.14159265 / float(NUM_DIRECTIONS));
        vec2 dir = vec2(cos(phi), sin(phi));

        float maxHorizonCos = 0.0;

        for (int s = 1; s <= NUM_STEPS; ++s) {
            float stepFrac = (float(s) + noise * 0.2) / float(NUM_STEPS);
            vec2 sampleUV = inUV + dir * texelSize * projRadius * stepFrac;

            if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) continue;

            float sampleDepth = texture(uDepthTexture, sampleUV).r;
            vec3 samplePos = ReconstructViewPos(sampleUV, sampleDepth);

            vec3 horizonVec = samplePos - centerPos;
            float horizonDist = length(horizonVec);

            if (horizonDist > 0.01 && horizonDist < (aoRadius * 1.5)) {
                vec3 horizonDir = horizonVec / horizonDist;
                // Angle bias of ~14 degrees (sin 14 deg ~ 0.24) prevents flat surfaces from self-occluding and creating grain
                float horizonCos = dot(N, horizonDir) - 0.24;
                if (horizonCos > 0.0) {
                    float distFactor = clamp(1.0 - horizonDist / aoRadius, 0.0, 1.0);
                    maxHorizonCos = max(maxHorizonCos, horizonCos * distFactor);
                }
            }
        }

        totalOcclusion += maxHorizonCos;
    }

    float ao = (totalOcclusion / float(NUM_DIRECTIONS)) * aoIntensity;
    ao = clamp(ao, 0.0, 1.0);

    // Quadratic falloff eliminates residual low-level noise on flat surfaces
    ao = ao * ao;

    outColor = vec4(ao, 0.0, 0.0, 1.0);
}
