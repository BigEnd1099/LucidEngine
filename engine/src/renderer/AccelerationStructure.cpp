#include "renderer/AccelerationStructure.hpp"
#include "core/AssetManager.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace engine::renderer {
namespace {
    VkDeviceAddress bufferAddress(VkDevice device, VkBuffer buffer) {
        VkBufferDeviceAddressInfo info{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        info.buffer = buffer;
        return vkGetBufferDeviceAddress(device, &info);
    }
}

void AccelerationStructure::init(VulkanContext* context) {
    m_context = context;
    if (!context || !context->isRayTracingSupported()) {
        core::Logger::info("Acceleration structures disabled: Vulkan ray query is not supported.");
        return;
    }
    VkDevice dev = context->getDevice();
    m_createAS = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(dev, "vkCreateAccelerationStructureKHR"));
    m_destroyAS = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(dev, "vkDestroyAccelerationStructureKHR"));
    m_getBuildSizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(dev, "vkGetAccelerationStructureBuildSizesKHR"));
    m_cmdBuildAS = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(dev, "vkCmdBuildAccelerationStructuresKHR"));
    m_getASAddress = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(dev, "vkGetAccelerationStructureDeviceAddressKHR"));
    m_ready = m_createAS && m_destroyAS && m_getBuildSizes && m_cmdBuildAS && m_getASAddress;
    if (!m_ready) core::Logger::error("Ray tracing extensions were enabled but required KHR entry points are missing.");
    else core::Logger::info("AccelerationStructure manager initialized.");
}

bool AccelerationStructure::createAccelerationBuffer(VkDeviceSize size, VkBuffer& buffer, VmaAllocation& allocation) {
    VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = std::max<VkDeviceSize>(size, 256);
    info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    return vmaCreateBuffer(m_context->getAllocator(), &info, &alloc, &buffer, &allocation, nullptr) == VK_SUCCESS;
}

VkDeviceAddress AccelerationStructure::getBufferAddress(VkBuffer buffer) const {
    return bufferAddress(m_context->getDevice(), buffer);
}

void AccelerationStructure::destroyResource(AccelResource& resource) {
    if (!m_context) return;
    if (resource.handle && m_destroyAS) m_destroyAS(m_context->getDevice(), resource.handle, nullptr);
    if (resource.buffer) vmaDestroyBuffer(m_context->getAllocator(), resource.buffer, resource.allocation);
    resource = {};
}

bool AccelerationStructure::ensureBLAS(VkCommandBuffer cmd, uint32_t meshId, const core::MeshAsset& mesh) {
    if (!m_ready || meshId == 0 || !mesh.vertexBuffer || !mesh.indexBuffer ||
        mesh.vertexCount == 0 || mesh.indexCount < 3) return false;
    if (m_blas.find(meshId) != m_blas.end()) return true;

    VkDevice dev = m_context->getDevice();
    VkAccelerationStructureGeometryTrianglesDataKHR triangles{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = getBufferAddress(mesh.vertexBuffer);
    triangles.vertexStride = sizeof(Vertex);
    triangles.maxVertex = mesh.vertexCount - 1;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = getBufferAddress(mesh.indexBuffer);

    VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles = triangles;

    VkAccelerationStructureBuildGeometryInfoKHR build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;

    const uint32_t primitiveCount = mesh.indexCount / 3;
    VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    m_getBuildSizes(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &primitiveCount, &sizes);

    BlasRecord record{};
    if (!createAccelerationBuffer(sizes.accelerationStructureSize, record.resource.buffer, record.resource.allocation)) return false;
    VkAccelerationStructureCreateInfoKHR create{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    create.buffer = record.resource.buffer;
    create.size = sizes.accelerationStructureSize;
    create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (m_createAS(dev, &create, nullptr, &record.resource.handle) != VK_SUCCESS) {
        destroyResource(record.resource); return false;
    }

    VkBuffer scratch = VK_NULL_HANDLE; VmaAllocation scratchAlloc = VK_NULL_HANDLE;
    VkBufferCreateInfo scratchInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    scratchInfo.size = sizes.buildScratchSize + 4096;
    scratchInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VmaAllocationCreateInfo scratchMem{}; scratchMem.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateBuffer(m_context->getAllocator(), &scratchInfo, &scratchMem, &scratch, &scratchAlloc, nullptr) != VK_SUCCESS) {
        destroyResource(record.resource); return false;
    }
    m_retainedScratch.emplace_back(scratch, scratchAlloc);
    build.dstAccelerationStructure = record.resource.handle;
    {
        VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
        VkPhysicalDeviceProperties2 props{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        props.pNext = &asProps;
        vkGetPhysicalDeviceProperties2(m_context->getPhysicalDevice(), &props);
        const VkDeviceAddress raw = getBufferAddress(scratch);
        const VkDeviceAddress align = std::max<VkDeviceAddress>(1, asProps.minAccelerationStructureScratchOffsetAlignment);
        build.scratchData.deviceAddress = (raw + align - 1) & ~(align - 1);
    }
    VkAccelerationStructureBuildRangeInfoKHR range{ primitiveCount, 0, 0, 0 };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = { &range };
    m_cmdBuildAS(cmd, 1, &build, ranges);

    // Build writes must be visible to later TLAS builds and ray queries.
    VkMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.memoryBarrierCount = 1; dep.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);

    m_blas.emplace(meshId, std::move(record));
    return true;
}

bool AccelerationStructure::buildTLAS(VkCommandBuffer cmd, const std::vector<Instance>& instances) {
    if (!m_ready) return false;
    // The renderer serializes frames (queue idle / in-flight fence) before this
    // method is reached again, so scratch from the preceding build is now safe to release.
    if (m_hasBuiltTlas) {
        for (auto& scratch : m_retainedScratch)
            if (scratch.first) vmaDestroyBuffer(m_context->getAllocator(), scratch.first, scratch.second);
        m_retainedScratch.clear();
    }
    VkDevice dev = m_context->getDevice();

    std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
    vkInstances.reserve(instances.size());
    for (const auto& source : instances) {
        auto it = m_blas.find(source.meshId);
        if (it == m_blas.end()) continue;
        VkAccelerationStructureInstanceKHR inst{};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 4; ++col)
                inst.transform.matrix[row][col] = source.transform[col][row];
        inst.instanceCustomIndex = source.customIndex & 0x00FFFFFFu;
        inst.mask = 0xFF;
        inst.instanceShaderBindingTableRecordOffset = 0;
        inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        inst.accelerationStructureReference = it->second.resource.address;
        if (inst.accelerationStructureReference == 0) {
            VkAccelerationStructureDeviceAddressInfoKHR addr{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
            addr.accelerationStructure = it->second.resource.handle;
            inst.accelerationStructureReference = m_getASAddress(dev, &addr);
            it->second.resource.address = inst.accelerationStructureReference;
        }
        vkInstances.push_back(inst);
    }

    if (vkInstances.empty()) return false;

    // The renderer waits for the previous frame's queue work before reusing this resource.
    if (m_tlas.handle) destroyResource(m_tlas);
    if (m_instanceBuffer) { vmaDestroyBuffer(m_context->getAllocator(), m_instanceBuffer, m_instanceAllocation); m_instanceBuffer = VK_NULL_HANDLE; }
    VkBufferCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    instanceInfo.size = sizeof(VkAccelerationStructureInstanceKHR) * vkInstances.size();
    instanceInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VmaAllocationCreateInfo instanceAllocInfo{};
    instanceAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    instanceAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo mappedInfo{};
    if (vmaCreateBuffer(m_context->getAllocator(), &instanceInfo, &instanceAllocInfo,
                        &m_instanceBuffer, &m_instanceAllocation, &mappedInfo) != VK_SUCCESS) return false;
    std::memcpy(mappedInfo.pMappedData, vkInstances.data(), static_cast<size_t>(instanceInfo.size));
    vmaFlushAllocation(m_context->getAllocator(), m_instanceAllocation, 0, instanceInfo.size);

    VkDeviceOrHostAddressConstKHR instanceAddress{};
    instanceAddress.deviceAddress = getBufferAddress(m_instanceBuffer);
    VkAccelerationStructureGeometryInstancesDataKHR instanceData{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
    instanceData.data = instanceAddress;
    VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instanceData;

    VkAccelerationStructureBuildGeometryInfoKHR build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    uint32_t count = static_cast<uint32_t>(vkInstances.size());
    VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    m_getBuildSizes(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &count, &sizes);
    if (!createAccelerationBuffer(sizes.accelerationStructureSize, m_tlas.buffer, m_tlas.allocation)) return false;
    VkAccelerationStructureCreateInfoKHR create{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    create.buffer = m_tlas.buffer; create.size = sizes.accelerationStructureSize;
    create.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if (m_createAS(dev, &create, nullptr, &m_tlas.handle) != VK_SUCCESS) { destroyResource(m_tlas); return false; }

    VkBuffer scratch = VK_NULL_HANDLE; VmaAllocation scratchAlloc = VK_NULL_HANDLE;
    VkBufferCreateInfo scratchInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    scratchInfo.size = sizes.buildScratchSize + 4096;
    scratchInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VmaAllocationCreateInfo scratchMem{}; scratchMem.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateBuffer(m_context->getAllocator(), &scratchInfo, &scratchMem, &scratch, &scratchAlloc, nullptr) != VK_SUCCESS) {
        destroyResource(m_tlas); return false;
    }
    m_retainedScratch.emplace_back(scratch, scratchAlloc);
    build.dstAccelerationStructure = m_tlas.handle;
    {
        VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
        VkPhysicalDeviceProperties2 props{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        props.pNext = &asProps;
        vkGetPhysicalDeviceProperties2(m_context->getPhysicalDevice(), &props);
        const VkDeviceAddress raw = getBufferAddress(scratch);
        const VkDeviceAddress align = std::max<VkDeviceAddress>(1, asProps.minAccelerationStructureScratchOffsetAlignment);
        build.scratchData.deviceAddress = (raw + align - 1) & ~(align - 1);
    }
    VkAccelerationStructureBuildRangeInfoKHR range{ count, 0, 0, 0 };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = { &range };
    m_cmdBuildAS(cmd, 1, &build, ranges);
    {
        VkAccelerationStructureDeviceAddressInfoKHR addressInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
        addressInfo.accelerationStructure = m_tlas.handle;
        m_tlas.address = m_getASAddress(dev, &addressInfo);
    }

    VkMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.memoryBarrierCount = 1; dep.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
    return true;
}

void AccelerationStructure::cleanup() {
    if (!m_context || !m_context->getDevice()) return;
    vkDeviceWaitIdle(m_context->getDevice());
    for (auto& entry : m_blas) destroyResource(entry.second.resource);
    m_blas.clear();
    destroyResource(m_tlas);
    if (m_instanceBuffer) vmaDestroyBuffer(m_context->getAllocator(), m_instanceBuffer, m_instanceAllocation);
    m_instanceBuffer = VK_NULL_HANDLE; m_instanceAllocation = VK_NULL_HANDLE;
    for (auto& scratch : m_retainedScratch)
        if (scratch.first) vmaDestroyBuffer(m_context->getAllocator(), scratch.first, scratch.second);
    m_retainedScratch.clear();
    m_ready = false;\n    m_hasBuiltTlas = false;
}
}
