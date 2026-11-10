#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <utility>
#include "renderer/VulkanContext.hpp"

namespace engine::core { struct MeshAsset; }

namespace engine::renderer {
    class AccelerationStructure {
    public:
        struct Instance {
            uint64_t blasId{0};
            uint32_t customIndex{0};
            glm::mat4 transform{1.0f};
        };

        void init(VulkanContext* context);
        void cleanup();
        bool isReady() const { return m_ready; }
        VkAccelerationStructureKHR getTLAS() const { return m_tlas.handle; }

        bool ensureBLAS(VkCommandBuffer cmd, uint64_t blasId, const core::MeshAsset& mesh, VkBuffer vertexBuffer, bool dynamicGeometry, bool geometryChanged);
        bool buildTLAS(VkCommandBuffer cmd, const std::vector<Instance>& instances, bool* outHandleChanged = nullptr);
        void releaseCompletedScratch();

    private:
        struct AccelResource {
            VkAccelerationStructureKHR handle{VK_NULL_HANDLE};
            VkBuffer buffer{VK_NULL_HANDLE};
            VmaAllocation allocation{VK_NULL_HANDLE};
            VkDeviceAddress address{0};
            VkDeviceSize size{0};
        };
        struct BlasRecord { AccelResource resource; bool dynamicGeometry{false}; };

        bool createAccelerationBuffer(VkDeviceSize size, VkBuffer& buffer, VmaAllocation& allocation);
        VkDeviceAddress getBufferAddress(VkBuffer buffer) const;
        void destroyResource(AccelResource& resource);

        VulkanContext* m_context{nullptr};
        bool m_ready{false};
        PFN_vkCreateAccelerationStructureKHR m_createAS{nullptr};
        PFN_vkDestroyAccelerationStructureKHR m_destroyAS{nullptr};
        PFN_vkGetAccelerationStructureBuildSizesKHR m_getBuildSizes{nullptr};
        PFN_vkCmdBuildAccelerationStructuresKHR m_cmdBuildAS{nullptr};
        PFN_vkGetAccelerationStructureDeviceAddressKHR m_getASAddress{nullptr};
        std::unordered_map<uint64_t, BlasRecord> m_blas;
        AccelResource m_tlas;

        VkBuffer m_instanceBuffer{VK_NULL_HANDLE};
        VmaAllocation m_instanceAllocation{VK_NULL_HANDLE};
        VkDeviceSize m_instanceBufferSize{0};
        void* m_instanceMappedPtr{nullptr};

        VkBuffer m_scratchBuffer{VK_NULL_HANDLE};
        VmaAllocation m_scratchAlloc{VK_NULL_HANDLE};
        VkDeviceSize m_scratchBufferSize{0};

        uint32_t m_maxInstanceCapacity{0};
        uint32_t m_lastInstanceCount{0};
        uint64_t m_lastInstanceHash{0};
        VkDeviceAddress m_minScratchAlignment{64};
        bool m_tlasBuiltOnce{false};
        bool m_blasChangedSinceTlas{false};

        std::vector<std::pair<VkBuffer, VmaAllocation>> m_retainedScratch;
    };
}
