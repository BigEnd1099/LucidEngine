#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include "renderer/VulkanContext.hpp"

namespace engine::renderer {

    // CPU-side meshlet metadata. GPU dispatch/rasterization is a separate, not-yet-wired path.
    struct NaniteCluster {
        glm::vec3 sphereCenter{0.0f};
        float sphereRadius{1.0f};

        glm::vec3 coneAxis{0.0f, 1.0f, 0.0f};
        float coneCutoff{0.0f}; // cos(maximum normal-cone angle)

        float lodError{0.0f};
        float maxParentError{1e6f};

        uint32_t triangleOffset{0};
        uint32_t triangleCount{128};
        uint32_t vertexOffset{0};
        uint32_t vertexCount{64};
    };

    struct FrustumPlane {
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        float distance{0.0f};
    };

    class ClusterRenderer {
    public:
        void init(VulkanContext* context);
        void cleanup();
        void dispatchClusterCulling(VkCommandBuffer cmd);
        void renderClusters(VkCommandBuffer cmd);

        static bool isClusterVisible(
            const NaniteCluster& cluster,
            const std::vector<FrustumPlane>& frustum,
            const glm::vec3& viewOrigin,
            float lodThreshold);

        static bool isBackfaceConeCulled(
            const NaniteCluster& cluster,
            const glm::vec3& viewOrigin);

        // CPU fallback used by tools/tests until the GPU indirect path is connected.
        // Returns indices into the registered cluster array, preserving source order.
        std::vector<uint32_t> cullVisibleClusters(
            const std::vector<FrustumPlane>& frustum,
            const glm::vec3& viewOrigin,
            float lodThreshold) const;

        void registerCluster(const NaniteCluster& cluster) { m_clusters.push_back(cluster); }
        void clearClusters() { m_clusters.clear(); }
        size_t getClusterCount() const { return m_clusters.size(); }

    private:
        VulkanContext* m_context{nullptr};
        std::vector<NaniteCluster> m_clusters;
    };
}
