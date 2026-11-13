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

// Readers are deliberately lock-free. Same-surface writers never modify the
// identity/epoch fields, so readers can keep matching a published key while
// radianceAge is refreshed by another workgroup.
bool tryLoadSurfaceHistoryCell(ivec3 cell, vec3 worldNormal, uint lightingEpoch,
                               out vec4 history, out bool empty) {
    uint base = surfaceHistoryHash(cell);
    vec3 n = normalize(worldNormal);
    for (uint probe = 0u; probe < SURFACE_HISTORY_PROBE_COUNT; ++probe) {
        uint index = (base + probe) & (SURFACE_HISTORY_CELL_COUNT - 1u);
        SurfaceHistoryCell entry = surfaceHistoryCache.cells[index];
        memoryBarrierBuffer();
        empty = entry.meta.x == 0u;
        bool match = !empty && entry.meta.x == lightingEpoch &&
                     all(equal(entry.identity.xyz, cell)) &&
                     dot(unpackNormalOctahedral(uint(entry.identity.w)), n) > 0.50;
        if (match) {
            history = entry.radianceAge;
            return true;
        }
    }
    return false;
}

bool loadSurfaceHistory(vec3 worldPos, vec3 worldNormal, uint lightingEpoch, out vec4 history) {
    vec3 n = normalize(worldNormal);
    vec3 biasedPos = worldPos + n * (SURFACE_HISTORY_CELL_SIZE * 0.25);
    vec3 gridPos = biasedPos / SURFACE_HISTORY_CELL_SIZE - 0.5;
    ivec3 baseCell = ivec3(floor(gridPos));
    vec3 f = fract(gridPos);

    vec4 accumHistory = vec4(0.0);
    float weightSum = 0.0;
    bool empty = false;

    // Trilinear interpolation across the eight neighboring cell centers.
    // Each lookup uses the bounded hash probe sequence and never takes a lock.
    for (int dz = 0; dz <= 1; ++dz) {
        float wz = (dz == 0) ? (1.0 - f.z) : f.z;
        for (int dy = 0; dy <= 1; ++dy) {
            float wy = (dy == 0) ? (1.0 - f.y) : f.y;
            for (int dx = 0; dx <= 1; ++dx) {
                float wx = (dx == 0) ? (1.0 - f.x) : f.x;
                float w = wx * wy * wz;
                if (w <= 0.0001) continue;
                ivec3 c = baseCell + ivec3(dx, dy, dz);
                vec4 cellVal = vec4(0.0);
                if (tryLoadSurfaceHistoryCell(c, n, lightingEpoch, cellVal, empty)) {
                    accumHistory += w * cellVal;
                    weightSum += w;
                }
            }
        }
    }

    if (weightSum > 0.001) {
        history = accumHistory / weightSum;
        return true;
    }
    return false;
}

void storeSurfaceHistory(vec3 worldPos, vec3 worldNormal, vec3 irradiance,
                         float historyLength, uint lightingEpoch, uint frameIndex,
                         bool overwriteSameFrame) {
    vec3 n = normalize(worldNormal);
    vec3 biasedPos = worldPos + n * (SURFACE_HISTORY_CELL_SIZE * 0.25);
    ivec3 cell = ivec3(floor(biasedPos / SURFACE_HISTORY_CELL_SIZE));
    uint packedN = packNormalOctahedral(n);
    vec3 safeIrradiance = sanitizeHDR(clamp(irradiance, vec3(0.0), vec3(16.0)));
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
        // One CAS attempt: no spinlock. A contended writer skips this sample;
        // another active pixel can publish the same surface during this dispatch.
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

        if (sameSurface) {
            float oldAge = max(entry.radianceAge.a, 1.0);
            float nextAge = min(max(oldAge, historyLength) + 1.0, 64.0);
            float alpha = clamp(1.0 / nextAge, 0.05, 0.25);
            vec3 nextRadiance = mix(entry.radianceAge.rgb, safeIrradiance, alpha);
            surfaceHistoryCache.cells[index].radianceAge =
                vec4(nextRadiance, max(historyLength, nextAge));
            // Existing key and lighting epoch remain stable for lock-free readers.
            surfaceHistoryCache.cells[index].meta.z = frameIndex;
            memoryBarrierBuffer();
            atomicExchange(surfaceHistoryCache.cells[index].meta.y, seq + 2u);
            return;
        }
        if (empty || staleEpoch) {
            surfaceHistoryCache.cells[index].radianceAge =
                vec4(safeIrradiance, max(historyLength, 1.0));
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
