#include "renderer/Geometry/ClusterRenderer.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::renderer {
    namespace {
        constexpr float kEpsilon = 1.0e-6f;

        bool isFiniteVec3(const glm::vec3& value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    }

    void ClusterRenderer::init(VulkanContext* context) {
        m_context = context;
        core::Logger::info("ClusterRenderer CPU meshlet culling initialized; GPU indirect dispatch is not wired yet.");
    }

    void ClusterRenderer::cleanup() {
        m_clusters.clear();
        m_context = nullptr;
    }

    void ClusterRenderer::dispatchClusterCulling(VkCommandBuffer cmd) {
        // Deliberately no-op: no compute pipeline or indirect command buffers are owned by this class yet.
        (void)cmd;
    }

    void ClusterRenderer::renderClusters(VkCommandBuffer cmd) {
        // Deliberately no-op: meshlet vertex/index streams and a compatible graphics pipeline are not wired yet.
        (void)cmd;
    }

    bool ClusterRenderer::isClusterVisible(
        const NaniteCluster& cluster,
        const std::vector<FrustumPlane>& frustum,
        const glm::vec3& viewOrigin,
        float lodThreshold) {
        if (!isFiniteVec3(cluster.sphereCenter) || !isFiniteVec3(viewOrigin) ||
            !std::isfinite(cluster.sphereRadius) || cluster.sphereRadius < 0.0f ||
            !std::isfinite(cluster.lodError) || cluster.lodError < 0.0f ||
            !std::isfinite(lodThreshold) || lodThreshold < 0.0f) {
            return false;
        }

        // Plane normals are not required to be normalized: scale the sphere radius
        // by |normal| so the signed-distance comparison remains geometrically correct.
        for (const auto& plane : frustum) {
            if (!isFiniteVec3(plane.normal) || !std::isfinite(plane.distance)) continue;
            const float normalLength = glm::length(plane.normal);
            if (!std::isfinite(normalLength) || normalLength <= kEpsilon) continue;

            const float signedDistance = glm::dot(plane.normal, cluster.sphereCenter) + plane.distance;
            if (signedDistance < -(cluster.sphereRadius * normalLength)) {
                return false;
            }
        }

        if (isBackfaceConeCulled(cluster, viewOrigin)) return false;

        const float distanceToEye = std::max(
            0.1f,
            glm::length(cluster.sphereCenter - viewOrigin) - cluster.sphereRadius);
        constexpr float kReferenceProjectionScale = 800.0f;
        const float projectedError = (cluster.lodError * kReferenceProjectionScale) / distanceToEye;
        return std::isfinite(projectedError) && projectedError <= lodThreshold;
    }

    bool ClusterRenderer::isBackfaceConeCulled(
        const NaniteCluster& cluster,
        const glm::vec3& viewOrigin) {
        if (!isFiniteVec3(cluster.sphereCenter) || !isFiniteVec3(viewOrigin) ||
            !isFiniteVec3(cluster.coneAxis) || !std::isfinite(cluster.sphereRadius) ||
            cluster.sphereRadius < 0.0f || !std::isfinite(cluster.coneCutoff)) {
            return false; // Invalid cone metadata must never incorrectly hide geometry.
        }

        const glm::vec3 toCluster = cluster.sphereCenter - viewOrigin;
        const float distSq = glm::dot(toCluster, toCluster);
        const float radiusSq = cluster.sphereRadius * cluster.sphereRadius;
        if (!std::isfinite(distSq) || distSq <= radiusSq + kEpsilon) return false;

        const float axisLength = glm::length(cluster.coneAxis);
        if (!std::isfinite(axisLength) || axisLength <= kEpsilon) return false;

        const glm::vec3 viewDir = toCluster / std::sqrt(distSq);
        const glm::vec3 coneAxis = cluster.coneAxis / axisLength;
        const float sinSubtended = std::clamp(cluster.sphereRadius / std::sqrt(distSq), 0.0f, 1.0f);
        const float cosSubtended = std::sqrt(std::max(0.0f, 1.0f - sinSubtended * sinSubtended));

        const float dotCone = std::clamp(glm::dot(coneAxis, viewDir), -1.0f, 1.0f);
        const float coneCutoff = std::clamp(cluster.coneCutoff, -1.0f, 1.0f);
        const float minFacingDot =
            (dotCone * cosSubtended) -
            (std::sqrt(std::max(0.0f, 1.0f - dotCone * dotCone)) * sinSubtended);

        return minFacingDot > coneCutoff;
    }

    std::vector<uint32_t> ClusterRenderer::cullVisibleClusters(
        const std::vector<FrustumPlane>& frustum,
        const glm::vec3& viewOrigin,
        float lodThreshold) const {
        std::vector<uint32_t> visible;
        visible.reserve(m_clusters.size());
        for (size_t index = 0; index < m_clusters.size(); ++index) {
            if (isClusterVisible(m_clusters[index], frustum, viewOrigin, lodThreshold)) {
                visible.push_back(static_cast<uint32_t>(index));
            }
        }
        return visible;
    }
}
