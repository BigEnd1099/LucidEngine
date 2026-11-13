#ifndef GI_SURFACE_HISTORY_COMMON_GLSL
#define GI_SURFACE_HISTORY_COMMON_GLSL

// Persistent diffuse surface history shared by temporal lookup and final à-trous writeback.
struct SurfaceHistoryCell {
    vec4 radianceAge;       // RGB = diffuse irradiance, A = valid-frame history length
    ivec4 identity;         // xyz = signed 25 cm cell coordinate, w = packed octahedral normal
    uvec4 meta;             // x = lighting epoch, y = odd/even writer sequence, z = last update frame, w = reserved
};
const uint SURFACE_HISTORY_CELL_COUNT = 262144u;
const float SURFACE_HISTORY_CELL_SIZE = 0.25;
const uint SURFACE_HISTORY_PROBE_COUNT = 16u;
layout(set = 1, binding = 21, std430) coherent buffer SurfaceHistoryCacheBuf {
    SurfaceHistoryCell cells[];
} surfaceHistoryCache;

uint surfaceHistoryHash(ivec3 cell) {
    // Hash only quantized world position. Normal similarity is validated after
    // probing; hashing the exact packed normal made nearby samples of the same
    // smooth surface start in unrelated probe sequences.
    uint h = uint(cell.x) * 73856093u ^ uint(cell.y) * 19349663u ^
             uint(cell.z) * 83492791u;
    return (h ^ (h >> 16u)) & (SURFACE_HISTORY_CELL_COUNT - 1u);
}

// Writers use an odd/even sequence in meta.y. Readers never acquire the
// writer lock; atomic sequence snapshots reject a slot while it is being updated.
bool tryLoadSurfaceHistoryCell(ivec3 cell, vec3 worldNormal, uint lightingEpoch,
                               out vec4 history, out bool empty) {
    uint base = surfaceHistoryHash(cell);
    vec3 n = normalize(worldNormal);
    for (uint probe = 0u; probe < SURFACE_HISTORY_PROBE_COUNT; ++probe) {
        uint index = (base + probe) & (SURFACE_HISTORY_CELL_COUNT - 1u);
        uint seq0 = atomicAdd(surfaceHistoryCache.cells[index].meta.y, 0u);
        if ((seq0 & 1u) != 0u) continue;
        SurfaceHistoryCell entry = surfaceHistoryCache.cells[index];
        memoryBarrierBuffer();
        uint seq1 = atomicAdd(surfaceHistoryCache.cells[index].meta.y, 0u);
        if (seq0 != seq1 || (seq1 & 1u) != 0u) continue;
        empty = entry.meta.x == 0u;
        bool match = !empty && entry.meta.x == lightingEpoch &&
                     all(equal(entry.identity.xyz, cell)) &&
                     dot(unpackNormalOctahedral(uint(entry.identity.w)), n) > 0.50;
        if (match) {
            history = entry.radianceAge;
            return true;
        }
        if (empty) return false;
    }
    return false;
}

bool loadSurfaceHistory(vec3 worldPos, vec3 worldNormal, uint lightingEpoch, out vec4 history) {
    vec3 n = normalize(worldNormal);
    vec3 biasedPos = worldPos + n * (SURFACE_HISTORY_CELL_SIZE * 0.25);
    vec3 gridPos = biasedPos / SURFACE_HISTORY_CELL_SIZE;
    ivec3 baseCell = ivec3(floor(gridPos));

    bool empty = false;
    if (tryLoadSurfaceHistoryCell(baseCell, n, lightingEpoch, history, empty))
        return true;

    // Dual-grid neighborhood fallback: test all seven other corners of the
    // local 2x2x2 cell neighborhood, starting from the closest corner.
    vec3 f = fract(gridPos);
    ivec3 stepDir = ivec3(
        f.x >= 0.5 ? 1 : -1,
        f.y >= 0.5 ? 1 : -1,
        f.z >= 0.5 ? 1 : -1
    );

    if (tryLoadSurfaceHistoryCell(baseCell + ivec3(stepDir.x, 0, 0), n, lightingEpoch, history, empty))
        return true;
    if (tryLoadSurfaceHistoryCell(baseCell + ivec3(0, stepDir.y, 0), n, lightingEpoch, history, empty))
        return true;
    if (tryLoadSurfaceHistoryCell(baseCell + ivec3(0, 0, stepDir.z), n, lightingEpoch, history, empty))
        return true;
    if (tryLoadSurfaceHistoryCell(baseCell + ivec3(stepDir.x, stepDir.y, 0), n, lightingEpoch, history, empty))
        return true;
    if (tryLoadSurfaceHistoryCell(baseCell + ivec3(stepDir.x, 0, stepDir.z), n, lightingEpoch, history, empty))
        return true;
    if (tryLoadSurfaceHistoryCell(baseCell + ivec3(0, stepDir.y, stepDir.z), n, lightingEpoch, history, empty))
        return true;
    if (tryLoadSurfaceHistoryCell(baseCell + stepDir, n, lightingEpoch, history, empty))
        return true;

    return false;
}

void storeSurfaceHistory(vec3 worldPos, vec3 worldNormal, vec3 irradiance,
                         float historyLength, uint lightingEpoch, uint frameIndex,
                         bool overwriteSameFrame) {
    vec3 n = normalize(worldNormal);
    vec3 biasedPos = worldPos + n * (SURFACE_HISTORY_CELL_SIZE * 0.25);
    ivec3 cell = ivec3(floor(biasedPos / SURFACE_HISTORY_CELL_SIZE));
    uint packedN = packNormalOctahedral(n);
    uint base = surfaceHistoryHash(cell);
    for (uint probe = 0u; probe < SURFACE_HISTORY_PROBE_COUNT; ++probe) {
        uint index = (base + probe) & (SURFACE_HISTORY_CELL_COUNT - 1u);

        // Fast atomic snapshot avoids contending on cells already published this frame.
        uint observedSeq = atomicAdd(surfaceHistoryCache.cells[index].meta.y, 0u);
        if ((observedSeq & 1u) != 0u) return;
        SurfaceHistoryCell existing = surfaceHistoryCache.cells[index];
        memoryBarrierBuffer();
        if (observedSeq != atomicAdd(surfaceHistoryCache.cells[index].meta.y, 0u))
            return;
        if (existing.meta.x == lightingEpoch &&
            all(equal(existing.identity.xyz, cell)) &&
            dot(unpackNormalOctahedral(uint(existing.identity.w)), n) > 0.50 &&
            existing.meta.z == frameIndex && !overwriteSameFrame)
            return;

        // One CAS attempt: no spinlock. A contended writer skips this frame;
        // the active checkerboard phase retries on the next frame.
        uint seq = observedSeq;
        if (atomicCompSwap(surfaceHistoryCache.cells[index].meta.y, seq, seq + 1u) != seq)
            return;

        memoryBarrierBuffer();
        SurfaceHistoryCell entry = surfaceHistoryCache.cells[index];
        bool empty = entry.meta.x == 0u;
        bool sameSurface = !empty && entry.meta.x == lightingEpoch &&
                           all(equal(entry.identity.xyz, cell)) &&
                           dot(unpackNormalOctahedral(uint(entry.identity.w)), n) > 0.50;
        bool staleEpoch = !empty && entry.meta.x != lightingEpoch;

        if (sameSurface && entry.meta.z == frameIndex) {
            if (overwriteSameFrame) {
                // The final Atrous pass is later in the same command stream;
                // let its filtered radiance replace the temporal pre-denoise value.
                surfaceHistoryCache.cells[index].radianceAge =
                    vec4(max(irradiance, vec3(0.0)), max(entry.radianceAge.a, historyLength));
            }
            memoryBarrierBuffer();
            atomicExchange(surfaceHistoryCache.cells[index].meta.y, seq + 2u);
            return;
        }
        if (empty || sameSurface || staleEpoch) {
            float oldAge = sameSurface ? max(entry.radianceAge.a, 1.0) : 0.0;
            float nextAge = min(oldAge + 1.0, 64.0);
            float alpha = sameSurface ? max(1.0 / nextAge, 0.025) : 1.0;
            vec3 nextRadiance = sameSurface
                ? mix(entry.radianceAge.rgb, max(irradiance, vec3(0.0)), alpha)
                : max(irradiance, vec3(0.0));
            surfaceHistoryCache.cells[index].radianceAge = vec4(nextRadiance, max(historyLength, nextAge));
            surfaceHistoryCache.cells[index].identity = ivec4(cell, int(packedN));
            surfaceHistoryCache.cells[index].meta.x = lightingEpoch;
            surfaceHistoryCache.cells[index].meta.z = frameIndex;
            memoryBarrierBuffer();
            atomicExchange(surfaceHistoryCache.cells[index].meta.y, seq + 2u);
            return;
        }

        // The slot is occupied by a different key; release and probe the next slot.
        memoryBarrierBuffer();
        atomicExchange(surfaceHistoryCache.cells[index].meta.y, seq + 2u);
    }
}

#endif // GI_SURFACE_HISTORY_COMMON_GLSL
