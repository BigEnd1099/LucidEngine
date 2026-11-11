#include "core/Profiler.hpp"
#include "renderer/Geometry/ClusterRenderer.hpp"
#include "renderer/Lighting/HardwareReSTIR.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

using namespace engine::renderer;

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
    assert(combined.M == 10);
    assert(std::abs(combined.wSum - 7.0f) < 1.0e-5f);
    assert(std::abs(combined.W - 1.4f) < 1.0e-5f);
    assert(combined.lightIndex == 11);

    const ReSTIRReservoir invalidPdf = HardwareReSTIR::combineReservoirs(a, b, 0.0f, 0.5f);
    assert(invalidPdf.M == 0);
    assert(invalidPdf.W == 0.0f);

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
    assert(ClusterRenderer::isClusterVisible(
        cluster, intersectingPlane, glm::vec3(0.0f, 0.0f, 5.0f), 1.0f));

    const std::vector<FrustumPlane> outsidePlane = {
        { glm::vec3(2.0f, 0.0f, 0.0f), -2.5f }
    };
    assert(!ClusterRenderer::isClusterVisible(
        cluster, outsidePlane, glm::vec3(0.0f, 0.0f, 5.0f), 1.0f));

    // Non-unit cone axes are normalized before the backface test.
    cluster.coneAxis = glm::vec3(0.0f, 0.0f, 4.0f);
    cluster.coneCutoff = 0.9f;
    cluster.sphereCenter = glm::vec3(0.0f, 0.0f, 5.0f);
    assert(ClusterRenderer::isBackfaceConeCulled(cluster, glm::vec3(0.0f)));

    // Invalid cone metadata must fail open instead of accidentally removing geometry.
    cluster.coneAxis = glm::vec3(0.0f);
    assert(!ClusterRenderer::isBackfaceConeCulled(cluster, glm::vec3(0.0f)));

    ClusterRenderer clusters;
    cluster.sphereCenter = glm::vec3(0.0f);
    cluster.coneAxis = glm::vec3(0.0f);
    clusters.registerCluster(cluster);
    NaniteCluster outside = cluster;
    outside.sphereCenter = glm::vec3(-10.0f, 0.0f, 0.0f);
    clusters.registerCluster(outside);
    const auto visible = clusters.cullVisibleClusters(outsidePlane, glm::vec3(0.0f, 0.0f, 5.0f), 1.0f);
    assert(visible.size() == 1 && visible.front() == 0);

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
    assert(stats.at("frameTimeHistorySamples").get<size_t>() == 1);
    assert(stats.at("frameTimeP95Ms").get<double>() > 0.0);
    assert(stats.at("samples").at("algorithm-test").at("sampleCount").get<uint64_t>() == 1);
    assert(stats.at("samples").at("algorithm-test").at("avgMs").get<double>() > 0.0);
    assert(!stats.at("metrics").contains("invalid-nan"));

    std::cout << "Renderer algorithm tests passed.\n";
    return 0;
}
