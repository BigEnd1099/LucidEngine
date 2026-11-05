#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outAOGI; // rgb = indirect diffuse GI (color bleed), a = AO

layout(set = 0, binding = 0) uniform sampler2D depthTex;
layout(set = 0, binding = 1) uniform sampler2D colorTex; // Scene HDR lit/textured color from previous frame

layout(push_constant) uniform GtaoPush {
    vec4 params;       // x = worldRadius, y = intensity, z = tanHalfFovY, w = aspect
    vec4 screenParams; // x = invWidth, y = invHeight, z = zNear, w = zFar
    vec4 flags;        // x = gtaoEnabled (1.0/0.0), y = giEnabled (1.0/0.0), z = unused, w = unused
} pcs;

const float PI = 3.14159265359;
const float HALF_PI = 1.57079632679;

float linearizeDepth(float d) {
    float zNear = pcs.screenParams.z;
    float zFar = pcs.screenParams.w;
    return (zNear * zFar) / max(zFar - d * (zFar - zNear), 1e-6);
}

// Bezposrednia rekonstrukcja pozycji w przestrzeni widoku
vec3 uvToViewPos(vec2 uv, float rawDepth) {
    float z = linearizeDepth(rawDepth);
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return vec3(ndc.x * pcs.params.z * pcs.params.w * z, ndc.y * pcs.params.z * z, -z);
}

float interleavedGradientNoise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

float fastAcos(float x) {
    float ax = abs(x);
    float res = -0.156583 * ax + HALF_PI;
    res *= sqrt(max(0.0, 1.0 - ax));
    return (x >= 0.0) ? res : (PI - res);
}

void main() {
    float centerRawDepth = texture(depthTex, fragUV).r;
    if (centerRawDepth >= 0.9999) {
        outAOGI = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 viewPos = uvToViewPos(fragUV, centerRawDepth);
    float viewDist = -viewPos.z;

    vec3 dpdx = dFdx(viewPos);
    vec3 dpdy = dFdy(viewPos);
    vec3 viewNormal = normalize(cross(dpdy, dpdx));
    vec3 viewDir = normalize(-viewPos);

    float worldRadius = pcs.params.x;
    float projScale = 0.5 / max(pcs.params.z, 1e-4);
    vec2 radiusUV = vec2(worldRadius * projScale / (viewDist * pcs.params.w), worldRadius * projScale / viewDist);
    float maxRadiusPx = radiusUV.y / max(pcs.screenParams.y, 1e-5);
    if (maxRadiusPx < 1.5) {
        outAOGI = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    radiusUV = min(radiusUV, vec2(0.12));

    float noiseAngle = interleavedGradientNoise(gl_FragCoord.xy) * PI;
    float noiseOffset = fract(interleavedGradientNoise(gl_FragCoord.xy + vec2(17.0, 31.0)));

    const int NUM_SLICES = 3;
    const int STEPS_PER_SIDE = 4;
    float visibility = 0.0;
    float falloffRange = worldRadius * 1.35;

    vec3 accumGI = vec3(0.0);
    float accumWeight = 0.0;

    for (int slice = 0; slice < NUM_SLICES; ++slice) {
        float phi = noiseAngle + (float(slice) / float(NUM_SLICES)) * PI;
        vec2 omega = vec2(cos(phi), sin(phi));
        vec3 dirVec = vec3(omega.x, -omega.y, 0.0);
        vec3 orthoDir = dirVec - dot(dirVec, viewDir) * viewDir;
        vec3 axisVec = normalize(cross(orthoDir, viewDir));
        vec3 projNormal = viewNormal - axisVec * dot(viewNormal, axisVec);
        float projNormalLen = max(length(projNormal), 1e-4);

        float signN = sign(dot(orthoDir, projNormal));
        float cosNorm = clamp(dot(projNormal, viewDir) / projNormalLen, -1.0, 1.0);
        float n = signN * fastAcos(cosNorm);

        vec2 maxHorizons = vec2(-1.0);

        for (int step = 0; step < STEPS_PER_SIDE; ++step) {
            float s = (float(step) + noiseOffset * 0.75 + 0.25) / float(STEPS_PER_SIDE);
            s = max(s * s, 1.25 * pcs.screenParams.y / max(radiusUV.y, 1e-5));
            vec2 offsetUV = omega * radiusUV * s;

            // Dodatnia i ujemna polprosta horyzontu
            for (int side = 0; side < 2; ++side) {
                vec2 sampleUV = clamp(fragUV + (side == 0 ? offsetUV : -offsetUV), vec2(0.001), vec2(0.999));
                float sDepth = texture(depthTex, sampleUV).r;
                vec3 sPos = uvToViewPos(sampleUV, sDepth);
                vec3 diff = sPos - viewPos;
                float dist = length(diff);
                if (dist > 1e-4) {
                    float cosH = dot(diff / dist, viewDir);
                    float weight = clamp(1.0 - (dist / falloffRange) * (dist / falloffRange), 0.0, 1.0);
                    maxHorizons[side] = max(maxHorizons[side], mix(-1.0, cosH, weight));

                    // --- SCREEN-SPACE DIFFUSE GI (COLOR BLEED OD KAZDEGO ELEMENTU Z JEGO TEKSTURA) ---
                    if (pcs.flags.y > 0.5) {
                        vec3 L = diff / dist;
                        float NdotL = max(dot(viewNormal, L), 0.0);
                        if (NdotL > 0.001 && dist < falloffRange) {
                            vec3 sampleColor = texture(colorTex, sampleUV).rgb;
                            // Firefly clamping: przyciecie pikow HDR powyzej 6.0
                            sampleColor = min(sampleColor, vec3(6.0));

                            float distFactor = clamp(1.0 - (dist / falloffRange), 0.0, 1.0);
                            float geomAtten = (NdotL * distFactor * distFactor) / (1.0 + dist * dist * 0.5);

                            accumGI += sampleColor * geomAtten;
                            accumWeight += geomAtten;
                        }
                    }
                }
            }
        }

        float h1 = n + max(-fastAcos(maxHorizons.y) - n, -HALF_PI);
        float h2 = n + min( fastAcos(maxHorizons.x) - n,  HALF_PI);

        // Calka analityczna horyzontu cosinusowego (Jimenez 2016)
        float sliceVis = 0.25 * ((-cos(2.0 * h1 - n) + cosNorm + 2.0 * h1 * sin(n)) +
                                 (-cos(2.0 * h2 - n) + cosNorm + 2.0 * h2 * sin(n)));
        visibility += projNormalLen * sliceVis;
    }

    float finalAO = 1.0;
    if (pcs.flags.x > 0.5) {
        visibility = clamp(visibility / float(NUM_SLICES), 0.03, 1.0);
        finalAO = pow(visibility, max(pcs.params.y, 0.01));
    }

    vec3 finalGI = vec3(0.0);
    if (pcs.flags.y > 0.5 && accumWeight > 1e-4) {
        finalGI = accumGI / accumWeight;
        // Jesli GTAO jest wlaczone, tlumi bounce na stykach (eliminacja contact glow).
        // Jesli GTAO jest wylaczone przez uzytkownika, nie zerujemy bounce.
        if (pcs.flags.x > 0.5) {
            finalGI *= finalAO;
        }
    }

    outAOGI = vec4(finalGI, finalAO);
}
