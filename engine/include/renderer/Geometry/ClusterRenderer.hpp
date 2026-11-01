#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include "renderer/VulkanContext.hpp"

namespace engine::renderer {

    // [Nanite Cluster Specification adapted from Unreal Engine 5.8 (NaniteDataDecode.ush)]
    struct NaniteCluster {
        glm::vec3 sphereCenter{0.0f};
        float sphereRadius{1.0f};

        glm::vec3 coneAxis{0.0f, 1.0f, 0.0f};
        float coneCutoff{0.0f}; // cos(angle)

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

        // Nanite culling functions adapted from UE5 (NaniteCulling.ush)
        static bool isClusterVisible(const NaniteCluster& cluster, const std::vector<FrustumPlane>& frustum, const glm::vec3& viewOrigin, float lodThreshold);
        static bool isBackfaceConeCulled(const NaniteCluster& cluster, const glm::vec3& viewOrigin);

        void registerCluster(const NaniteCluster& cluster) { m_clusters.push_back(cluster); }
        size_t getClusterCount() const { return m_clusters.size(); }

    private:
        VulkanContext* m_context{nullptr};
        std::vector<NaniteCluster> m_clusters;
    };
}
