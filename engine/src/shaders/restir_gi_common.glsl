// restir_gi_common.glsl - Shared data structures and routines for ReSTIR GI & PT
#ifndef RESTIR_GI_COMMON_GLSL
#define RESTIR_GI_COMMON_GLSL

struct GiReservoir {
    vec3 xs;         // World-space 3D hit position of the secondary surface
    float targetPdf; // Target function weight hat{p}(Y) evaluated at primary surface
    vec3 ns;         // Normal of the secondary surface at xs
    float wSum;      // Cumulative weight sum in RIS
    uint M;          // Number of consulted samples
    uint flags;      // Flags (bit 0: valid sample, bit 1: environment hit)
    vec3 Lo;         // Exitant radiance from xs toward primary surface x0
    float W;         // Unbiased weight W = wSum / (M * targetPdf)
};

GiReservoir emptyGiReservoir() {
    GiReservoir r;
    r.xs = vec3(0.0);
    r.targetPdf = 0.0;
    r.ns = vec3(0.0, 1.0, 0.0);
    r.wSum = 0.0;
    r.M = 0u;
    r.flags = 0u;
    r.Lo = vec3(0.0);
    r.W = 0.0;
    return r;
}

// Lossless, compact 3-buffer packing according to architecture roadmap:
// Reservoir A (RGBA32F): xyz = xs, w = targetPdf
// Reservoir B (RGBA32F): x = packed octahedral ns (16-bit snorm per axis), y = wSum, z = M, w = flags
// Reservoir C (RGBA32F): rgb = Lo, a = W
void packGiReservoir(GiReservoir r, out vec4 outA, out vec4 outB, out vec4 outC) {
    outA = vec4(r.xs, r.targetPdf);
    vec2 octN = rtOctEncode(r.ns);
    uint packedN = packSnorm2x16(octN);
    outB = vec4(uintBitsToFloat(packedN), r.wSum, uintBitsToFloat(r.M), uintBitsToFloat(r.flags));
    outC = vec4(r.Lo, r.W);
}

GiReservoir unpackGiReservoir(vec4 inA, vec4 inB, vec4 inC) {
    GiReservoir r;
    r.xs = inA.xyz;
    r.targetPdf = inA.w;
    uint packedN = floatBitsToUint(inB.x);
    vec2 octN = unpackSnorm2x16(packedN);
    r.ns = rtOctDecode(octN);
    r.wSum = inB.y;
    r.M = floatBitsToUint(inB.z);
    r.flags = floatBitsToUint(inB.w);
    r.Lo = inC.rgb;
    r.W = inC.a;
    return r;
}

// Evaluate target function hat{p} for sample Y=(xs, ns, Lo) at primary surface point (x0, n0).
// For indirect diffuse GI (demodulated albedo), the target function is:
// hat{p} = luminance(Lo) * max(0, n0 . omega)
// where omega = normalize(xs - x0), provided the secondary surface faces the ray (ns . -omega > 0).
float evalGiTargetPdf(vec3 Lo, vec3 xs, vec3 ns, vec3 x0, vec3 n0) {
    vec3 v = xs - x0;
    float distSq = dot(v, v);
    if (distSq < 1e-8) return 0.0;
    float dist = sqrt(distSq);
    vec3 omega = v / dist;
    float cosTheta0 = max(dot(n0, omega), 0.0);
    float cosThetaS = dot(ns, -omega);
    if (cosTheta0 <= 1e-5 || cosThetaS <= 1e-5) return 0.0;
    float lum = max(dot(Lo, vec3(0.2126, 0.7152, 0.0722)), 0.0);
    return lum * cosTheta0;
}

// Streaming RIS update
bool updateGiReservoir(inout GiReservoir r, vec3 xs, vec3 ns, vec3 Lo, float targetPdf, float weight, uint M, float randomVal) {
    r.wSum += weight;
    r.M += M;
    if (randomVal * r.wSum < weight) {
        r.xs = xs;
        r.ns = ns;
        r.Lo = Lo;
        r.targetPdf = targetPdf;
        r.flags = 1u;
        return true;
    }
    return false;
}

// Finalize unbiased weight W
void finalizeGiReservoir(inout GiReservoir r) {
    if (r.targetPdf > 1e-6 && r.M > 0u) {
        r.W = min(r.wSum / (float(r.M) * r.targetPdf), 10.0);
    } else {
        r.W = 0.0;
    }
}

// Clamp reservoir sample count M to maxM while proportionally scaling wSum
// to preserve the unbiased estimator ratio W = wSum / (M * targetPdf)
void clampGiReservoirM(inout GiReservoir r, uint maxM) {
    if (r.M > maxM && r.M > 0u) {
        r.wSum *= float(maxM) / float(r.M);
        r.M = maxM;
    }
}

#endif // RESTIR_GI_COMMON_GLSL
