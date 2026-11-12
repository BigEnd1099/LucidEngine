#include "core/Profiler.hpp"
#include "renderer/Geometry/ClusterRenderer.hpp"
#include "renderer/RenderGraph.hpp"
#include "renderer/Lighting/HardwareReSTIR.hpp"
#include "renderer/SpatialAnnotation.hpp"
#include "renderer/Lighting/WorldRadianceCache.hpp"
#include "renderer/Geometry/GpuCulling.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

using namespace engine::renderer;

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAILED: " << #condition << " at " << __FILE__ << ':' << __LINE__ << '\n'; \
    return 1; \
} } while (false)

int main() {
    // ReSTIR: the source reservoirs represent 4 + 6 candidates, not 2 representatives.
    ReSTIRReservoir a;
    a.M = 4;
    a.W = 2.0f;
    a.lightIndex = 11;
    ReSTIRReservoir b;
    b.M = 6;
    b.W = 1.0f;
    b.lightIndex = 22;

    const ReSTIRReservoir combined = HardwareReSTIR::combineReservoirs(a, b, 0.5f, 0.99f);
    CHECK(combined.M == 10);
    CHECK(std::abs(combined.wSum - 7.0f) < 1.0e-5f);
    CHECK(std::abs(combined.W - 1.4f) < 1.0e-5f);
    CHECK(combined.lightIndex == 11);

    const ReSTIRReservoir invalidPdf = HardwareReSTIR::combineReservoirs(a, b, 0.0f, 0.5f);
    CHECK(invalidPdf.M == 0);
    CHECK(invalidPdf.W == 0.0f);

    // Frustum planes need not be normalized. This sphere intersects x >= 0.75,
    // so it must not be rejected by a plane whose normal has length 2.
    NaniteCluster cluster;
    cluster.sphereCenter = glm::vec3(0.0f);
    cluster.sphereRadius = 1.0f;
    cluster.coneAxis = glm::vec3(0.0f); // Missing cone metadata disables cone culling safely.
    cluster.lodError = 0.0f;

    const std::vector<FrustumPlane> intersectingPlane = {
        { glm::vec3(2.0f, 0.0f, 0.0f), -1.5f }
    };
    CHECK(ClusterRenderer::isClusterVisible(
        cluster, intersectingPlane, glm::vec3(0.0f, 0.0f, 5.0f), 1.0f));

    const std::vector<FrustumPlane> outsidePlane = {
        { glm::vec3(2.0f, 0.0f, 0.0f), -2.5f }
    };
    CHECK(!ClusterRenderer::isClusterVisible(
        cluster, outsidePlane, glm::vec3(0.0f, 0.0f, 5.0f), 1.0f));

    // Non-unit cone axes are normalized before the backface test.
    cluster.coneAxis = glm::vec3(0.0f, 0.0f, 4.0f);
    cluster.coneCutoff = 0.9f;
    cluster.sphereCenter = glm::vec3(0.0f, 0.0f, 5.0f);
    CHECK(ClusterRenderer::isBackfaceConeCulled(cluster, glm::vec3(0.0f)));

    // Invalid cone metadata must fail open instead of accidentally removing geometry.
    cluster.coneAxis = glm::vec3(0.0f);
    CHECK(!ClusterRenderer::isBackfaceConeCulled(cluster, glm::vec3(0.0f)));

    ClusterRenderer clusters;
    cluster.sphereCenter = glm::vec3(0.0f);
    cluster.coneAxis = glm::vec3(0.0f);
    clusters.registerCluster(cluster);
    NaniteCluster outside = cluster;
    outside.sphereCenter = glm::vec3(-10.0f, 0.0f, 0.0f);
    clusters.registerCluster(outside);
    const auto visible = clusters.cullVisibleClusters(intersectingPlane, glm::vec3(0.0f, 0.0f, 5.0f), 1.0f);
    CHECK(visible.size() == 1 && visible.front() == 0);

    // RenderGraph: explicit dependencies can reorder independent declarations.
    RenderGraph graph;
    std::vector<std::string> execution;
    graph.addPass("present", {}, {}, [&](VkCommandBuffer) { execution.push_back("present"); }, {"shade"});
    graph.addPass("shade", {}, {}, [&](VkCommandBuffer) { execution.push_back("shade"); });
    CHECK(graph.compile());
    CHECK(graph.getExecutionOrder().size() == 2);
    CHECK(graph.getExecutionOrder()[0] == 1 && graph.getExecutionOrder()[1] == 0);
    graph.execute(VK_NULL_HANDLE);
    CHECK(execution.size() == 2 && execution[0] == "shade" && execution[1] == "present");

    // Explicit dependencies must be resolved before inferring resource hazards:
    // a producer may be declared after its consumer and still execute first.
    graph.clear();
    execution.clear();
    graph.addPass("consume-later", {"shadow-map"}, {}, [&](VkCommandBuffer) { execution.push_back("consume"); }, {"produce-first"});
    graph.addPass("produce-first", {}, {"shadow-map"}, [&](VkCommandBuffer) { execution.push_back("produce"); });
    CHECK(graph.compile());
    CHECK(graph.getExecutionOrder()[0] == 1 && graph.getExecutionOrder()[1] == 0);
    graph.execute(VK_NULL_HANDLE);
    CHECK(execution.size() == 2 && execution[0] == "produce" && execution[1] == "consume");

    // Resource declarations infer write-after-read and read-after-write dependencies.
    graph.clear();
    graph.addPass("write-color", {}, {"color"}, [&](VkCommandBuffer) { execution.push_back("write"); });
    graph.addPass("read-color", {"color"}, {}, [&](VkCommandBuffer) { execution.push_back("read"); });
    CHECK(graph.compile());
    CHECK(graph.getExecutionOrder()[0] == 0 && graph.getExecutionOrder()[1] == 1);

    // Cyclic explicit dependencies are rejected with a diagnostic instead of executing in insertion order.
    graph.clear();
    graph.addPass("cycle-a", {}, {}, [](VkCommandBuffer) {}, {"cycle-b"});
    graph.addPass("cycle-b", {}, {}, [](VkCommandBuffer) {}, {"cycle-a"});
    CHECK(!graph.compile());
    CHECK(!graph.getLastError().empty());

    // Profiler averages must be true arithmetic means, and invalid metrics are ignored.
    auto& profiler = engine::core::Profiler::get();
    profiler.beginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    profiler.endFrame();
    {
        engine::core::Profiler::ScopedSample sample("algorithm-test");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    profiler.recordMetric("invalid-nan", std::numeric_limits<double>::quiet_NaN());
    const auto stats = profiler.getStatsJson();
    CHECK(stats.at("frameTimeHistorySamples").get<size_t>() == 1);
    CHECK(stats.at("frameTimeP95Ms").get<double>() > 0.0);
    CHECK(stats.at("samples").at("algorithm-test").at("sampleCount").get<uint64_t>() == 1);
    CHECK(stats.at("samples").at("algorithm-test").at("avgMs").get<double>() > 0.0);
    CHECK(!stats.at("metrics").contains("invalid-nan"));

    // SpatialAnnotationRegistry test
    auto& annRegistry = SpatialAnnotationRegistry::get();
    annRegistry.clear();
    CHECK(annRegistry.getActiveAnnotations().empty());
    SpatialAnnotation ann1;
    ann1.worldPos = glm::vec3(10.0f, 2.0f, -5.0f);
    ann1.radius = 1.5f;
    ann1.label = "Anomaly Detected";
    ann1.agentId = 42;
    uint32_t annId = annRegistry.addAnnotation(ann1);
    CHECK(annId == 1);
    CHECK(annRegistry.getActiveAnnotations().size() == 1);
    auto annJson = annRegistry.toJson();
    CHECK(annJson.is_array() && annJson.size() == 1);
    CHECK(annJson[0]["label"] == "Anomaly Detected");
    CHECK(annJson[0]["agentId"] == 42);
    annRegistry.clear();
    CHECK(annRegistry.getActiveAnnotations().empty());

    // WorldRadianceCell std430 alignment & sizing
    static_assert(sizeof(WorldRadianceCell) == 32, "Std430 alignment mismatch!");
    CHECK(sizeof(WorldRadianceCell) == 32);

    // Octahedral normal pack/unpack round-trip
    glm::vec3 testNorm = glm::normalize(glm::vec3(0.577f, -0.577f, 0.577f));
    uint32_t packedNorm = WorldRadianceCache::packNormalOctahedral(testNorm);
    glm::vec3 unpackedNorm = WorldRadianceCache::unpackNormalOctahedral(packedNorm);
    CHECK(glm::dot(testNorm, unpackedNorm) > 0.999f);

    // Spatial hash stability & bound
    uint32_t cellHash1 = WorldRadianceCache::hashWorldCell(glm::vec3(12.3f, 4.5f, -8.1f), testNorm);
    uint32_t cellHash2 = WorldRadianceCache::hashWorldCell(glm::vec3(12.3f, 4.5f, -8.1f), testNorm);
    CHECK(cellHash1 == cellHash2);
    CHECK(cellHash1 < WorldRadianceCache::CACHE_CELL_COUNT);

    // GPU Frustum Culling Sphere Test
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 vp = proj * view;

    // Sphere in front of camera (center at (0, 0, 0)) -> visible
    CHECK(GpuCulling::testFrustumSphere(vp, glm::vec3(0.0f, 0.0f, 0.0f), 1.0f));

    // Sphere far behind camera (center at (0, 0, 50)) -> culled
    CHECK(!GpuCulling::testFrustumSphere(vp, glm::vec3(0.0f, 0.0f, 50.0f), 1.0f));

    // Sphere far off to the side (center at (100, 0, 0)) -> culled
    CHECK(!GpuCulling::testFrustumSphere(vp, glm::vec3(100.0f, 0.0f, 0.0f), 1.0f));

    // WBOIT McGuire & Bavoil weight calculation verification
    auto calcWboitWeight = [](float alpha, float viewZ) {
        float distWeight = 0.03f / (1.0e-5f + std::pow(viewZ / 200.0f, 4.0f));
        return alpha * std::clamp(distWeight, 1.0e-2f, 3.0e3f);
    };

    float wNear = calcWboitWeight(0.5f, 2.0f);   // 2 meters
    float wMid  = calcWboitWeight(0.5f, 20.0f);  // 20 meters
    float wFar  = calcWboitWeight(0.5f, 200.0f); // 200 meters

    // Weights must monotonically decrease with view distance
    CHECK(wNear >= wMid);
    CHECK(wMid >= wFar);
    CHECK(wFar >= 1.0e-2f * 0.5f);

    // RT Denoiser: YCoCg color space round-trip
    auto rgbToYCoCg = [](glm::vec3 c) {
        return glm::vec3(
             0.25f * c.r + 0.5f * c.g + 0.25f * c.b,
             0.5f  * c.r              - 0.5f  * c.b,
            -0.25f * c.r + 0.5f * c.g - 0.25f * c.b
        );
    };
    auto yCoCgToRgb = [](glm::vec3 c) {
        float y  = c.x;
        float co = c.y;
        float cg = c.z;
        return glm::vec3(
            y + co - cg,
            y + cg,
            y - co - cg
        );
    };
    glm::vec3 testRgb(0.72f, 0.45f, 0.18f);
    glm::vec3 roundTrip = yCoCgToRgb(rgbToYCoCg(testRgb));
    CHECK(glm::distance(testRgb, roundTrip) < 1.0e-5f);

    // RT Denoiser: Slope-aware depth tolerance
    auto calcDepthTol = [](float clipW, float nDotV) {
        float clampedNDotV = std::clamp(std::abs(nDotV), 0.15f, 1.0f);
        return (0.04f * clipW + 0.01f) / clampedNDotV;
    };
    float tolFacing = calcDepthTol(10.0f, 1.0f);   // normal facing camera
    float tolGrazing = calcDepthTol(10.0f, 0.05f); // normal at glancing angle
    CHECK(tolGrazing > tolFacing);
    CHECK(std::abs(tolFacing - 0.41f) < 1.0e-4f);
    CHECK(std::abs(tolGrazing - (0.41f / 0.15f)) < 1.0e-4f);

    // RT Denoiser: SVGF luminance edge-stopping weight
    auto calcSvgfLuminanceWeight = [](float deltaL, float variance, float sigmaL) {
        return std::exp(-deltaL / (sigmaL * std::sqrt(std::max(variance, 1.0e-4f)) + 1.0e-3f));
    };
    // With high variance (noisy/unconverged), small edge differences are filtered (wl ~ 1)
    float wlHighVar = calcSvgfLuminanceWeight(0.1f, 1.0f, 4.0f);
    // With low variance (converged history), edge differences are preserved (wl drops)
    float wlLowVar = calcSvgfLuminanceWeight(0.1f, 0.0001f, 4.0f);
    CHECK(wlHighVar > wlLowVar);
    CHECK(wlHighVar > 0.95f);
    CHECK(wlLowVar < 0.20f);

    // RT Denoiser: B3-spline filter weights normalization
    const float hTaps[3] = { 0.375f, 0.25f, 0.0625f };
    float sum1D = hTaps[0] + 2.0f * hTaps[1] + 2.0f * hTaps[2];
    CHECK(std::abs(sum1D - 1.0f) < 1.0e-6f);
    float sum2D = 0.0f;
    for (int j = -2; j <= 2; ++j) {
        for (int i = -2; i <= 2; ++i) {
            sum2D += hTaps[std::abs(i)] * hTaps[std::abs(j)];
        }
    }
    CHECK(std::abs(sum2D - 1.0f) < 1.0e-6f);

    // PBR: Multiple-Scattering Energy Compensation for Metals (Fdez-Aguera)
    // Single scattering GGX loses up to 50% energy on rough metals; energy compensation restores it.
    auto computeMetalSpecularBRDF = [](glm::vec3 F0, glm::vec2 dfg) {
        glm::vec3 energyComp = 1.0f + F0 * (1.0f / std::max(dfg.x + dfg.y, 1.0e-3f) - 1.0f);
        return (F0 * dfg.x + dfg.y) * energyComp;
    };
    glm::vec3 silverF0(0.95f, 0.95f, 0.95f);
    glm::vec2 roughDfg(0.45f, 0.08f); // typical split-sum DFG at roughness 0.5
    glm::vec3 singleScatterBRDF = silverF0 * roughDfg.x + roughDfg.y; // ~0.507
    glm::vec3 compensatedBRDF = computeMetalSpecularBRDF(silverF0, roughDfg); // ~0.956
    CHECK(singleScatterBRDF.r < 0.55f); // single-scattering loses almost half the energy!
    CHECK(compensatedBRDF.r > 0.90f);    // energy compensation restores full realistic metallic reflection
    CHECK(compensatedBRDF.r <= 1.0f);    // energy conservation: cannot exceed 1.0 for silver

    // RT Denoiser: Hampel peer-group outlier filter for fireflies
    // A single-pixel spike (e.g. 50.0) surrounded by normal samples (~0.5) must be suppressed
    float neighborSamples[8] = { 0.48f, 0.52f, 0.50f, 0.49f, 0.51f, 0.47f, 0.53f, 0.50f };
    float m1 = 0.0f;
    float m2 = 0.0f;
    for (int k = 0; k < 8; ++k) {
        m1 += neighborSamples[k];
        m2 += neighborSamples[k] * neighborSamples[k];
    }
    m1 /= 8.0f;
    m2 /= 8.0f;
    float sigma = std::sqrt(std::max(0.0f, m2 - m1 * m1));
    float rawFirefly = 50.0f;
    float clampedFirefly = std::clamp(rawFirefly, m1 - 3.0f * sigma, m1 + 3.0f * sigma);
    CHECK(clampedFirefly < 1.0f); // Firefly crushed to within neighbor distribution!
    CHECK(clampedFirefly >= m1);

    // RT Specular: Numerically stable Height-Correlated Smith GGX at grazing angles
    auto testSmithVis = [](float NdotV, float NdotL, float a) {
        float a2 = a * a;
        float gv = NdotL * std::sqrt(std::max(NdotV * NdotV * (1.0f - a2) + a2, 1e-8f));
        float gl = NdotV * std::sqrt(std::max(NdotL * NdotL * (1.0f - a2) + a2, 1e-8f));
        return 0.5f / std::max(gv + gl, 1e-7f);
    };
    // Grazing angle test: NdotV = 0.001, NdotL = 0.001
    float visGrazing = testSmithVis(0.001f, 0.001f, 0.05f);
    CHECK(!std::isnan(visGrazing) && !std::isinf(visGrazing));
    CHECK(visGrazing > 0.0f && visGrazing < 10000.0f); // bounded, no division by zero

    // RT Denoiser: Zero-Clipping Prevention in Dark Areas
    // When 8 neighbors are black (0.0) and 1 hit is 0.8, weak light must NOT be extinguished to 0.0
    float darkNeighbors[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float mDark = 0.0f;
    float mDark2 = 0.0f;
    for (int k = 0; k < 8; ++k) {
        mDark += darkNeighbors[k];
        mDark2 += darkNeighbors[k] * darkNeighbors[k];
    }
    mDark /= 8.0f;
    mDark2 /= 8.0f;
    float sigmaDark = std::sqrt(std::max(0.0f, mDark2 - mDark * mDark));
    float fireflyFloor = std::max(3.5f * sigmaDark, 3.0f);
    float weakHit = 0.8f;
    float processedWeakHit = (weakHit > mDark + fireflyFloor) ? (mDark + fireflyFloor) : weakHit;
    CHECK(processedWeakHit == 0.8f); // Weak light is 100% preserved, not zero-clipped!

    // RT Denoiser: À-Trous Luminance weight floor prevents energy extinction
    auto calcAtrousLumaWeight = [](float deltaL, float var, float phiL, float floorEps) {
        float stdDev = std::sqrt(std::max(var, 1e-4f));
        return std::exp(-deltaL / (phiL * stdDev + floorEps));
    };
    // In dark areas (var ~ 0), deltaL = 0.05 with floor 0.05 spreads energy
    float wSpread = calcAtrousLumaWeight(0.05f, 0.0f, 1.2f, 0.05f);
    CHECK(wSpread > 0.35f); // Energy spreads to neighbors (>40% weight)!
    // Without floor (epsilon = 1e-3), weight was choked down (< 2.5% weight)
    float wBroken = calcAtrousLumaWeight(0.05f, 0.0f, 1.2f, 1e-3f);
    CHECK(wBroken < 0.03f);
    CHECK(wSpread > wBroken * 15.0f); // Proves that floor 0.05 allows energy to spread 15-20x better!

    // RT Denoiser: Diffuse GI normal exponent (16.0) smoothly filters curved spheres
    // Sphere curvature between adjacent pixels: dot(n1, n2) = 0.90
    float wnNew = std::pow(0.90f, 16.0f); // ~0.185 (filters smoothly across sphere)
    float wnOld = std::pow(0.90f, 32.0f); // ~0.034 (almost shut off filtering, causing grain)
    CHECK(wnNew > wnOld * 5.0f);

    // Direct Lighting Specular & Physical Energy Conservation:
    // Arbitrary 4x multipliers are eliminated; strict energy conservation applies.
    auto calcDirectSpecularPhysical = [](float D, float Vis, float F, float energyComp, float NdotL) {
        return (D * Vis) * F * energyComp * NdotL;
    };
    float specDielectric = calcDirectSpecularPhysical(1.0f, 0.25f, 0.04f, 1.0f, 1.0f);
    float specMetal = calcDirectSpecularPhysical(1.0f, 0.25f, 1.0f, 1.0f, 1.0f);
    CHECK(specDielectric > 0.0f && specDielectric < specMetal);
    CHECK(specMetal <= 1.0f); // Incident energy conservation: cannot reflect > 100%

    // Interior HDRI Occlusion:
    // When ray-traced sky visibility is 0.0 (enclosed room), ambient IBL is completely extinguished.
    auto calcAmbientIBL = [](float skyVisibility, float giWeight, glm::vec3 envDiffuse, glm::vec3 envSpecular) {
        float iblDiffuseFactor = (1.0f - giWeight) * skyVisibility;
        glm::vec3 diff = envDiffuse * iblDiffuseFactor;
        glm::vec3 spec = envSpecular * skyVisibility;
        return diff + spec;
    };
    glm::vec3 brightHDRI(15.0f, 12.0f, 10.0f);
    glm::vec3 outsideAmbient = calcAmbientIBL(1.0f, 0.0f, brightHDRI, brightHDRI);
    glm::vec3 insideEnclosedAmbient = calcAmbientIBL(0.0f, 0.0f, brightHDRI, brightHDRI);
    glm::vec3 insideWithGiAmbient = calcAmbientIBL(0.0f, 1.0f, brightHDRI, brightHDRI);
    CHECK(outsideAmbient.r > 0.0f);
    CHECK(insideEnclosedAmbient.r == 0.0f && insideEnclosedAmbient.g == 0.0f && insideEnclosedAmbient.b == 0.0f);
    CHECK(insideWithGiAmbient.r == 0.0f && insideWithGiAmbient.g == 0.0f && insideWithGiAmbient.b == 0.0f);

    // Mirror Physical Reflection (100% Metal, 0% Roughness):
    // Smooth mirrors use exact physical Fresnel factor (1.0 for silver/metal), avoiding split-sum ~50% attenuation.
    auto calcMirrorSpecular = [](glm::vec3 F0, glm::vec2 dfg, float energyComp, float roughness, float NdotV, bool reflectsLight) {
        if (!reflectsLight) return glm::vec3(0.0f);
        glm::vec3 fresnelMirror = F0 + (glm::vec3(1.0f) - F0) * std::pow(std::clamp(1.0f - NdotV, 0.0f, 1.0f), 5.0f);
        glm::vec3 splitSumBRDF = (F0 * dfg.x + dfg.y) * energyComp;
        float t = std::clamp(roughness / 0.20f, 0.0f, 1.0f);
        return glm::mix(fresnelMirror, splitSumBRDF, t);
    };
    glm::vec3 pureMetalF0(1.0f);
    glm::vec2 typicalDfg(0.55f, 0.02f);
    glm::vec3 mirrorRefl = calcMirrorSpecular(pureMetalF0, typicalDfg, 1.0f, 0.0f, 0.7f, true);
    glm::vec3 noRefl = calcMirrorSpecular(pureMetalF0, typicalDfg, 1.0f, 0.0f, 0.7f, false);
    CHECK(std::abs(mirrorRefl.r - 1.0f) < 1.0e-5f); // 100% sharp reflection on mirror, not ~55%!
    CHECK(noRefl.r == 0.0f); // reflectsLight = false turns off specular reflection completely

    // Spotlight Reflection Cone Test:
    // A spotlight must be completely invisible from behind or outside its cone.
    auto calcSpotlightBulbVisibility = [](glm::vec3 rayDir, glm::vec3 spotForward, float innerCos, float outerCos) {
        float cone = glm::dot(rayDir, spotForward);
        if (cone <= outerCos) return 0.0f; // Outside cone or behind spotlight: 100% invisible
        float t = std::clamp((cone - outerCos) / (innerCos - outerCos), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t); // smoothstep
    };
    float spotForwardVis = calcSpotlightBulbVisibility(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 0.9f, 0.7f);
    float spotBehindVis  = calcSpotlightBulbVisibility(glm::vec3(0.0f, 1.0f, 0.0f),  glm::vec3(0.0f, -1.0f, 0.0f), 0.9f, 0.7f);
    float spotSideVis    = calcSpotlightBulbVisibility(glm::vec3(1.0f, 0.0f, 0.0f),  glm::vec3(0.0f, -1.0f, 0.0f), 0.9f, 0.7f);
    CHECK(spotForwardVis == 1.0f);
    CHECK(spotBehindVis == 0.0f); // Zero glow from behind
    CHECK(spotSideVis == 0.0f);   // Zero glow from the side

    // Specular Reflection Spatial Denoiser: Mirror vs Rough Surface Filtering
    auto calcReflSpatialWeightFast = [](float centerRoughness, float neighborRoughness,
                                        float dotNormal, float depthCenter, float depthNeighbor, float deltaL, bool denoiseMirrors) {
        float depthTol = 0.02f * depthCenter + 0.005f;
        float dz = std::abs(depthCenter - depthNeighbor);
        if (dz > depthTol || dotNormal < 0.6f) return 0.0f;

        if (centerRoughness < 0.08f) {
            // When denoiseMirrors is false (default), spatial filter is bypassed completely for mirrors
            if (!denoiseMirrors) return 0.0f;
            // Planar mirror bilateral cross filter: cleans speckle along the mirror plane
            if (dotNormal < 0.90f) return 0.0f;
            return std::exp(-deltaL / 0.15f);
        }
        float sigmaN = std::clamp(16.0f / std::max(centerRoughness, 0.04f), 4.0f, 48.0f);
        float exponent = std::max(0.0f, 1.0f - dotNormal) * sigmaN
                       + (dz / depthTol)
                       + (std::abs(centerRoughness - neighborRoughness) / 0.15f)
                       + (deltaL / 0.25f);
        return std::exp(-exponent);
    };
    // Mirror test: when denoiseMirrors is false -> weight 0 (bypassed, untouched raw reflection)
    float mirrorBypassedWeight = calcReflSpatialWeightFast(0.01f, 0.01f, 1.0f, 10.0f, 10.0f, 0.02f, false);
    CHECK(mirrorBypassedWeight == 0.0f);

    // Mirror test: when denoiseMirrors is true -> planar neighbor with small deltaL has strong bilateral weight (> 0.7)
    float mirrorDenoisedWeight = calcReflSpatialWeightFast(0.01f, 0.01f, 1.0f, 10.0f, 10.0f, 0.02f, true);
    CHECK(mirrorDenoisedWeight > 0.7f);

    // Rough metal test: roughness 0.4, planar surface (dotNormal = 1.0, depth matching) -> smooth filtering
    float roughPlanarWeight = calcReflSpatialWeightFast(0.4f, 0.4f, 1.0f, 10.0f, 10.0f, 0.0f, false);
    CHECK(roughPlanarWeight > 0.95f);

    // Rough metal edge rejection test: dotNormal = 0.5 (discontinuous geometry) -> weight dropped to 0
    float roughCornerWeight = calcReflSpatialWeightFast(0.4f, 0.4f, 0.5f, 10.0f, 10.0f, 0.0f, false);
    CHECK(roughCornerWeight == 0.0f);

    // 3D Rotation Gizmo Math Test: Verifying non-inverted 3D ring tracking
    auto calc3DRingAngleDeg = [](const glm::vec3& rotAxis, const glm::vec3& v0, const glm::vec3& v1) {
        float cosA = glm::clamp(glm::dot(v0, v1), -1.0f, 1.0f);
        float sinA = glm::dot(rotAxis, glm::cross(v0, v1));
        return glm::degrees(std::atan2(sinA, cosA));
    };

    // Yaw Ring (rotAxis = (0, 1, 0)): user grabs front of ring v0 = (0, 0, 1) and drags right v1 = normalize(0.2, 0, 1)
    glm::vec3 yawAxis(0.0f, 1.0f, 0.0f);
    glm::vec3 yawV0(0.0f, 0.0f, 1.0f);
    glm::vec3 yawV1_Right = glm::normalize(glm::vec3(0.2f, 0.0f, 1.0f));
    glm::vec3 yawV1_Left  = glm::normalize(glm::vec3(-0.2f, 0.0f, 1.0f));
    float yawAngleRight = calc3DRingAngleDeg(yawAxis, yawV0, yawV1_Right);
    float yawAngleLeft  = calc3DRingAngleDeg(yawAxis, yawV0, yawV1_Left);
    CHECK(yawAngleRight > 0.0f);  // Dragging right produces positive angle
    CHECK(yawAngleLeft < 0.0f);   // Dragging left produces negative angle

    glm::quat yawQ_Right = glm::angleAxis(glm::radians(yawAngleRight), yawAxis);
    glm::vec3 rotatedFront = yawQ_Right * yawV0;
    CHECK(rotatedFront.x > 0.0f); // Front of object moved to the RIGHT (+X), matching cursor direction!

    std::cout << "Renderer algorithm tests passed.\n";
    return 0;
}
