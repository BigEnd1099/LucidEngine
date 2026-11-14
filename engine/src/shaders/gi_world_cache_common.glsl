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

uint getCascadeIndex(vec3 worldPos, vec3 cameraPos, float dither) {
    float dist = max(0.0, length(worldPos - cameraPos) + dither);
    // Cascade 0: [0.0, 2.5m) -> 0.20m cells
    // Cascade 1: [2.5, 5.0m) -> 0.40m cells
    // Cascade 2: [5.0, 10.0m) -> 0.80m cells
    // Cascade 3: [10.0m, inf) -> 1.60m cells
    if (dist < 2.5) return 0u;
    return uint(clamp(floor(log2(dist / 2.5)) + 1.0, 0.0, 3.0));
}

float getCascadeCellSize(uint cascade) {
    return 0.20 * exp2(float(cascade)); // 0.20m, 0.40m, 0.80m, 1.60m
}

uint computeWorldCellKeyFromCoord(ivec3 cellCoord, vec3 normal, uint cascade) {
    vec3 n = normalize(normal);
    uint normIdx = 0u;
    if (abs(n.x) > abs(n.y) && abs(n.x) > abs(n.z)) {
        normIdx = n.x > 0.0 ? 0u : 1u;
    } else if (abs(n.y) > abs(n.z)) {
        normIdx = n.y > 0.0 ? 2u : 3u;
    } else {
        normIdx = n.z > 0.0 ? 4u : 5u;
    }
    uint h = uint(cellCoord.x) * 73856093u ^ uint(cellCoord.y) * 19349663u ^ uint(cellCoord.z) * 83492791u ^ normIdx * 2654435761u ^ (cascade * 1640531527u);
    return h ^ (h >> 16u);
}

uint computeWorldCellKeyFromCoord(ivec3 cellCoord, vec3 normal) {
    return computeWorldCellKeyFromCoord(cellCoord, normal, 0u);
}

uint computeWorldCellKey(vec3 pos, vec3 normal, float cellSize, uint cascade) {
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
    uint h = uint(cellCoord.x) * 73856093u ^ uint(cellCoord.y) * 19349663u ^ uint(cellCoord.z) * 83492791u ^ normIdx * 2654435761u ^ (cascade * 1640531527u);
    return h ^ (h >> 16u);
}

uint computeWorldCellKey(vec3 pos, vec3 normal, float cellSize) {
    return computeWorldCellKey(pos, normal, cellSize, 0u);
}

uint computeWorldCellKeyCascaded(vec3 pos, vec3 normal, vec3 cameraPos, float dither, out uint outCascade, out float outCellSize) {
    outCascade = getCascadeIndex(pos, cameraPos, dither);
    outCellSize = getCascadeCellSize(outCascade);
    return computeWorldCellKey(pos, normal, outCellSize, outCascade);
}

uint computeWorldCellKeyCascaded(vec3 pos, vec3 normal, vec3 cameraPos) {
    uint cascade = getCascadeIndex(pos, cameraPos, 0.0);
    float cellSize = getCascadeCellSize(cascade);
    return computeWorldCellKey(pos, normal, cellSize, cascade);
}

uint hashWorldCell(vec3 pos, vec3 normal, float cellSize, uint cascade) {
    return computeWorldCellKey(pos, normal, cellSize, cascade) % CACHE_CELL_COUNT;
}

uint hashWorldCell(vec3 pos, vec3 normal, float cellSize) {
    return hashWorldCell(pos, normal, cellSize, 0u);
}

// Jimenez animated Interleaved Gradient Noise (IGN) for temporal jittering
float ignTemporal(vec2 p, uint frame) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)) + float(frame & 63u) * 0.06711056));
}

// Computes the center in world space of a cell given its coordinate, normal bias, and cell size
vec3 computeWorldCellCenter(ivec3 cellCoord, vec3 normal, float cellSize) {
    vec3 n = normalize(normal);
    return (vec3(cellCoord) + 0.5) * cellSize - n * (cellSize * 0.25);
}

// Half-extent of a cubic voxel of size cellSize projected onto unit normal
float computeWorldCellBoxRadius(vec3 normal, float cellSize) {
    vec3 n = normalize(normal);
    return 0.5 * cellSize * (abs(n.x) + abs(n.y) + abs(n.z));
}

// Plane distance from sample position P to cell plane (defined by cellCenter and surface normal)
float computeWorldCellPlaneDistance(vec3 cellCenter, vec3 worldPos, vec3 normal) {
    return abs(dot(cellCenter - worldPos, normalize(normal)));
}

// Excess plane distance beyond the voxel's projected bounding box extent along normal
float computeWorldCellPlaneDistanceExcess(vec3 cellCenter, vec3 worldPos, vec3 normal, float cellSize) {
    float planeDist = computeWorldCellPlaneDistance(cellCenter, worldPos, normal);
    float boxRadius = computeWorldCellBoxRadius(normal, cellSize);
    return max(0.0, planeDist - boxRadius);
}

// Exponential geometry weight to prevent edge bleeding across corners (System #5)
float computePlaneDistanceWeight(float excessDist, float cellSize) {
    return exp(-excessDist / (cellSize * 0.15));
}

#ifndef BUILD_BASIS_DEFINED
#define BUILD_BASIS_DEFINED
// Orthonormal basis without singularity (Duff et al. 2017).
void buildBasis(vec3 n, out vec3 t, out vec3 b) {
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = vec3(c, s + n.y * n.y * a, -n.y);
}
#endif

// Pack cell coordinate and cascade index into 31 bits (leaving bit 0 for writer lock)
uint packCellCoord(ivec3 coord, uint cascade) {
    uint ux = uint(clamp(coord.x + 512, 0, 1023));
    uint uz = uint(clamp(coord.z + 512, 0, 1023));
    uint uy = uint(clamp(coord.y + 256, 0, 511));
    uint uc = cascade & 3u;
    return ux | (uz << 10u) | (uy << 20u) | (uc << 29u);
}

void unpackCellCoord(uint packed, out ivec3 coord, out uint cascade) {
    int x = int(packed & 1023u) - 512;
    int z = int((packed >> 10u) & 1023u) - 512;
    int y = int((packed >> 20u) & 511u) - 256;
    cascade = (packed >> 29u) & 3u;
    coord = ivec3(x, y, z);
}

// System #1: 8x4 stratified hemispherical direction generation (1 warp = 1 cell)
vec3 getStratifiedHemisphereDirection(vec3 normal, uint laneIdx, vec2 jitter) {
    uint col = laneIdx & 7u;  // 8 azimuth columns (0..7)
    uint row = laneIdx >> 3u; // 4 elevation rings (0..3)
    float uElevation = (float(row) + jitter.y) / 4.0;
    float uAzimuth = (float(col) + jitter.x) / 8.0;

    float r = sqrt(clamp(uElevation, 0.0, 1.0));
    float phi = 6.28318530718 * uAzimuth;

    vec3 n = normalize(normal);
    vec3 t, b;
    buildBasis(n, t, b);
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(1.0 - uElevation, 0.0)));
}

#endif // GI_WORLD_CACHE_COMMON_GLSL
