#include "renderer/Geometry/ClusterRenderer.hpp"
#include "core/Logger.hpp"
#include <cmath>

namespace engine::renderer {

    void ClusterRenderer::init(VulkanContext* context) {
        m_context = context;
        core::Logger::info("ClusterRenderer (UE5-adapted Nanite 64-128 tri Meshlets & Cone Culling) initialized.");
    }

    void ClusterRenderer::cleanup() {
        m_clusters.clear();
    }

    void ClusterRenderer::dispatchClusterCulling(VkCommandBuffer cmd) {
        // GPU Persistent Threads Culling compute pass
    }

    void ClusterRenderer::renderClusters(VkCommandBuffer cmd) {
        // MultiDrawIndirect cluster rasterization
    }

    // [UE5 NaniteCulling.ush: Frustum test & Screen Error LOD]
    bool ClusterRenderer::isClusterVisible(const NaniteCluster& cluster, const std::vector<FrustumPlane>& frustum, const glm::vec3& viewOrigin, float lodThreshold) {
        // 1. Frustum Sphere Culling
        for (const auto& plane : frustum) {
            float dist = glm::dot(plane.normal, cluster.sphereCenter) + plane.distance;
            if (dist < -cluster.sphereRadius) {
                return false; // Outside frustum
            }
        }

        // 2. Normal Cone Backface Culling
        if (isBackfaceConeCulled(cluster, viewOrigin)) {
            return false;
        }

        // 3. Screen-Projected Continuous LOD Error
        float distanceToEye = std::max(0.1f, glm::length(cluster.sphereCenter - viewOrigin) - cluster.sphereRadius);
        float projectedError = (cluster.lodError * 800.0f) / distanceToEye; // 800 = projection factor (viewport height)

        if (projectedError > lodThreshold) {
            return false; // Requires higher level of detail
        }

        return true;
    }

    // [UE5 NaniteCulling.ush: Normal Cone Cluster Backface Culling]
    bool ClusterRenderer::isBackfaceConeCulled(const NaniteCluster& cluster, const glm::vec3& viewOrigin) {
        glm::vec3 toCluster = cluster.sphereCenter - viewOrigin;
        float distSq = glm::dot(toCluster, toCluster);
        if (distSq <= cluster.sphereRadius * cluster.sphereRadius) return false; // Inside cluster sphere

        glm::vec3 viewDir = glm::normalize(toCluster);
        // If angle between cone axis and view direction exceeds cone cutoff + angle subtended by sphere
        float cosSubtended = std::sqrt(std::max(0.0f, 1.0f - (cluster.sphereRadius * cluster.sphereRadius) / distSq));
        float sinSubtended = cluster.sphereRadius / std::sqrt(distSq);

        float dotCone = glm::dot(cluster.coneAxis, viewDir);
        // Trigonometric cone overlap test from UE5
        return (dotCone * cosSubtended - std::sqrt(std::max(0.0f, 1.0f - dotCone * dotCone)) * sinSubtended) > cluster.coneCutoff;
    }
}
