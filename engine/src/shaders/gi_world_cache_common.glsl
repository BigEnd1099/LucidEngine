#ifndef GI_WORLD_CACHE_COMMON_GLSL
#define GI_WORLD_CACHE_COMMON_GLSL

// Std430 SSBO layout matching C++ WorldRadianceCell (32 bytes)
struct WorldRadianceCell {
    vec4 irradiance;     // RGB = radiance, A = weight/sampleCount (offset 0..15)
    uint cellKey;        // full 32-bit spatial key, not the table slot (offset 16..19)
    uint packedNormal;   // Octahedral normal 16-bit + flags 16-bit (offset 20..23)
    uint lastSeenFrame;  // TTL / frame counter (offset 24..27)
    uint lockOrFlags;    // atomic writer lock / flags (offset 28..31)
};

const uint CACHE_CELL_COUNT = 524288u;
const float DEFAULT_CELL_SIZE = 0.65;

uint packNormalOctahedral(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z) + 1e-6);
    if (n.z < 0.0) {
        vec2 oldXY = n.xy;
        vec2 signNotZero = vec2(oldXY.x >= 0.0 ? 1.0 : -1.0, oldXY.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(oldXY.yx)) * signNotZero;
    }
    vec2 oct = clamp(n.xy * 0.5 + 0.5, 0.0, 1.0);
    uint ux = uint(oct.x * 65535.0 + 0.5);
    uint uy = uint(oct.y * 65535.0 + 0.5);
    return (uy << 16u) | ux;
}

vec3 unpackNormalOctahedral(uint packedN) {
    vec2 oct = vec2(float(packedN & 0xFFFFu), float((packedN >> 16u) & 0xFFFFu)) / 65535.0;
    oct = oct * 2.0 - 1.0;
    vec3 n = vec3(oct.x, oct.y, 1.0 - abs(oct.x) - abs(oct.y));
    if (n.z < 0.0) {
        vec2 oldXY = n.xy;
        vec2 signNotZero = vec2(oldXY.x >= 0.0 ? 1.0 : -1.0, oldXY.y >= 0.0 ? 1.0 : -1.0);
        n.x = (1.0 - abs(oldXY.y)) * signNotZero.x;
        n.y = (1.0 - abs(oldXY.x)) * signNotZero.y;
    }
    return normalize(n);
}

uint computeWorldCellKeyFromCoord(ivec3 cellCoord, vec3 normal) {
    vec3 n = normalize(normal);
    uint normIdx = 0u;
    if (abs(n.x) > abs(n.y) && abs(n.x) > abs(n.z)) {
        normIdx = n.x > 0.0 ? 0u : 1u;
    } else if (abs(n.y) > abs(n.z)) {
        normIdx = n.y > 0.0 ? 2u : 3u;
    } else {
        normIdx = n.z > 0.0 ? 4u : 5u;
    }
    uint h = uint(cellCoord.x) * 73856093u ^ uint(cellCoord.y) * 19349663u ^ uint(cellCoord.z) * 83492791u ^ normIdx * 2654435761u;
    return h ^ (h >> 16u);
}

uint computeWorldCellKey(vec3 pos, vec3 normal, float cellSize) {
    // Apply the same deterministic normal bias for ray hits and G-buffer lookups.
    vec3 n = normalize(normal);
    vec3 biasedPos = pos + n * (cellSize * 0.25);
    ivec3 cellCoord = ivec3(floor(biasedPos / cellSize));
    uint normIdx = 0u;
    if (abs(normal.x) > abs(normal.y) && abs(normal.x) > abs(normal.z)) {
        normIdx = normal.x > 0.0 ? 0u : 1u;
    } else if (abs(normal.y) > abs(normal.z)) {
        normIdx = normal.y > 0.0 ? 2u : 3u;
    } else {
        normIdx = normal.z > 0.0 ? 4u : 5u;
    }
    uint h = uint(cellCoord.x) * 73856093u ^ uint(cellCoord.y) * 19349663u ^ uint(cellCoord.z) * 83492791u ^ normIdx * 2654435761u;
    return h ^ (h >> 16u);
}

uint hashWorldCell(vec3 pos, vec3 normal, float cellSize) {
    return computeWorldCellKey(pos, normal, cellSize) % CACHE_CELL_COUNT;
}

#endif // GI_WORLD_CACHE_COMMON_GLSL
