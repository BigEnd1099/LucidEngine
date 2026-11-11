#include "core/Profiler.hpp"
#include "renderer/Geometry/ClusterRenderer.hpp"
#include "renderer/RenderGraph.hpp"
#include "renderer/Lighting/HardwareReSTIR.hpp"
#include "renderer/SpatialAnnotation.hpp"
#include "renderer/Lighting/WorldRadianceCache.hpp"
#include "renderer/Geometry/GpuCulling.hpp"
#include <glm/gtc/matrix_transform.hpp>

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

    std::cout << "Renderer algorithm tests passed.\n";
    return 0;
}
