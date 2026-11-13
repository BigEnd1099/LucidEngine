#ifndef GI_SURFACE_HISTORY_COMMON_GLSL
#define GI_SURFACE_HISTORY_COMMON_GLSL

// Persistent diffuse surface history shared by temporal lookup and final à-trous writeback.
struct SurfaceHistoryCell {
    vec4 radianceAge;       // RGB = diffuse irradiance, A = valid-frame history length
    ivec4 identity;         // xyz = signed 25 cm cell coordinate, w = packed octahedral normal
    uvec4 meta;             // x = lighting epoch, y = spin lock, z = last update frame, w = reserved
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

bool lockSurfaceHistory(uint index) {
    for (uint spin = 0u; spin < 32u; ++spin) {
        if (atomicCompSwap(surfaceHistoryCache.cells[index].meta.y, 0u, 1u) == 0u)
            return true;
    }
    return false;
}

bool loadSurfaceHistory(vec3 worldPos, vec3 worldNormal, uint lightingEpoch, out vec4 history) {
    ivec3 cell = ivec3(floor(worldPos / SURFACE_HISTORY_CELL_SIZE));
    uint packedN = packNormalOctahedral(normalize(worldNormal));
    uint base = surfaceHistoryHash(cell);
    for (uint probe = 0u; probe < SURFACE_HISTORY_PROBE_COUNT; ++probe) {
        uint index = (base + probe) & (SURFACE_HISTORY_CELL_COUNT - 1u);
        if (!lockSurfaceHistory(index)) return false;
        SurfaceHistoryCell entry = surfaceHistoryCache.cells[index];
        bool empty = entry.meta.x == 0u;
        bool match = !empty && entry.meta.x == lightingEpoch &&
                     all(equal(entry.identity.xyz, cell)) &&
                     dot(unpackNormalOctahedral(uint(entry.identity.w)), normalize(worldNormal)) > 0.92;
        if (match) history = entry.radianceAge;
        atomicExchange(surfaceHistoryCache.cells[index].meta.y, 0u);
        if (match) return true;
        if (empty) return false;
    }
    return false;
}

void storeSurfaceHistory(vec3 worldPos, vec3 worldNormal, vec3 irradiance,
                         float historyLength, uint lightingEpoch, uint frameIndex) {
    ivec3 cell = ivec3(floor(worldPos / SURFACE_HISTORY_CELL_SIZE));
    uint packedN = packNormalOctahedral(normalize(worldNormal));
    uint base = surfaceHistoryHash(cell);
    for (uint probe = 0u; probe < SURFACE_HISTORY_PROBE_COUNT; ++probe) {
        uint index = (base + probe) & (SURFACE_HISTORY_CELL_COUNT - 1u);
        if (!lockSurfaceHistory(index)) return;
        SurfaceHistoryCell entry = surfaceHistoryCache.cells[index];
        bool empty = entry.meta.x == 0u;
        bool sameSurface = !empty && entry.meta.x == lightingEpoch &&
                           all(equal(entry.identity.xyz, cell)) &&
                           dot(unpackNormalOctahedral(uint(entry.identity.w)), normalize(worldNormal)) > 0.92;
        bool staleEpoch = !empty && entry.meta.x != lightingEpoch;
        // One representative pixel per quantized surface cell is enough per frame.
        if (sameSurface && entry.meta.z == frameIndex) {
            atomicExchange(surfaceHistoryCache.cells[index].meta.y, 0u);
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
            atomicExchange(surfaceHistoryCache.cells[index].meta.y, 0u);
            return;
        }
        atomicExchange(surfaceHistoryCache.cells[index].meta.y, 0u);
    }
}

#endif // GI_SURFACE_HISTORY_COMMON_GLSL
