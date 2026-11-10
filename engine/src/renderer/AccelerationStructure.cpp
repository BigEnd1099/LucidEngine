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
    if (!m_ready) {
        core::Logger::error("Ray tracing extensions were enabled but required KHR entry points are missing.");
    } else {
        VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
        VkPhysicalDeviceProperties2 props{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        props.pNext = &asProps;
        vkGetPhysicalDeviceProperties2(m_context->getPhysicalDevice(), &props);
        m_minScratchAlignment = std::max<VkDeviceAddress>(1, asProps.minAccelerationStructureScratchOffsetAlignment);
        core::Logger::info("AccelerationStructure manager initialized (scratch alignment: " + std::to_string(m_minScratchAlignment) + " bytes).");
    }
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

bool AccelerationStructure::ensureBLAS(VkCommandBuffer cmd, uint64_t blasId, const core::MeshAsset& mesh,
                                               VkBuffer vertexBuffer, bool dynamicGeometry) {
    if (!m_ready || blasId == 0 || !vertexBuffer || !mesh.indexBuffer ||
        mesh.vertexCount == 0 || mesh.indexCount < 3) return false;

    auto existing = m_blas.find(blasId);
    if (existing != m_blas.end() && (!dynamicGeometry || !existing->second.dynamicGeometry))
        return true;

    VkDevice dev = m_context->getDevice();
    VkAccelerationStructureGeometryTrianglesDataKHR triangles{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = getBufferAddress(vertexBuffer);
    triangles.vertexStride = sizeof(Vertex);
    triangles.maxVertex = mesh.vertexCount - 1;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = getBufferAddress(mesh.indexBuffer);
    if (triangles.vertexData.deviceAddress == 0 || triangles.indexData.deviceAddress == 0) return false;

    VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles = triangles;

    VkAccelerationStructureBuildGeometryInfoKHR build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                  VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;

    const uint32_t primitiveCount = mesh.indexCount / 3;
    VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    m_getBuildSizes(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &primitiveCount, &sizes);

    const bool update = existing != m_blas.end();
    BlasRecord record{};
    if (update) {
        record = existing->second;
    } else {
        if (!createAccelerationBuffer(sizes.accelerationStructureSize, record.resource.buffer, record.resource.allocation))
            return false;
        VkAccelerationStructureCreateInfoKHR create{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        create.buffer = record.resource.buffer;
        create.size = sizes.accelerationStructureSize;
        create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        if (m_createAS(dev, &create, nullptr, &record.resource.handle) != VK_SUCCESS) {
            destroyResource(record.resource);
            return false;
        }
    }

    const VkDeviceSize scratchRequired = update ? sizes.updateScratchSize : sizes.buildScratchSize;
    VkBuffer scratch = VK_NULL_HANDLE;
    VmaAllocation scratchAlloc = VK_NULL_HANDLE;
    VkBufferCreateInfo scratchInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    scratchInfo.size = scratchRequired + m_minScratchAlignment + 4096;
    scratchInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VmaAllocationCreateInfo scratchMem{};
    scratchMem.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateBuffer(m_context->getAllocator(), &scratchInfo, &scratchMem, &scratch, &scratchAlloc, nullptr) != VK_SUCCESS) {
        if (!update) destroyResource(record.resource);
        return false;
    }
    m_retainedScratch.emplace_back(scratch, scratchAlloc);

    build.mode = update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.srcAccelerationStructure = update ? record.resource.handle : VK_NULL_HANDLE;
    build.dstAccelerationStructure = record.resource.handle;
    const VkDeviceAddress rawScratch = getBufferAddress(scratch);
    const VkDeviceAddress align = m_minScratchAlignment;
    build.scratchData.deviceAddress = (rawScratch + align - 1) & ~(align - 1);

    // Make CPU-written, flushed deformation data visible to the acceleration-structure build.
    if (dynamicGeometry) {
        VkMemoryBarrier2 hostBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        hostBarrier.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        hostBarrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
        hostBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        hostBarrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        VkDependencyInfo hostDep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        hostDep.memoryBarrierCount = 1;
        hostDep.pMemoryBarriers = &hostBarrier;
        vkCmdPipelineBarrier2(cmd, &hostDep);
    }

    VkAccelerationStructureBuildRangeInfoKHR range{ primitiveCount, 0, 0, 0 };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = { &range };
    m_cmdBuildAS(cmd, 1, &build, ranges);

    VkMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);

    record.dynamicGeometry = dynamicGeometry;
    if (!update) m_blas.emplace(blasId, std::move(record));
    else existing->second = std::move(record);
    return true;
}

bool AccelerationStructure::buildTLAS(VkCommandBuffer cmd, const std::vector<Instance>& instances, bool* outHandleChanged) {
    if (outHandleChanged) *outHandleChanged = false;
    if (!m_ready) return false;
    VkDevice dev = m_context->getDevice();

    std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
    vkInstances.reserve(instances.size());
    for (const auto& source : instances) {
        auto it = m_blas.find(source.blasId);
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

    const uint32_t count = static_cast<uint32_t>(vkInstances.size());
    const size_t byteCount = count * sizeof(VkAccelerationStructureInstanceKHR);

    // FNV-1a hash instancji do detekcji czy cokolwiek w scenie RT uległo zmianie
    uint64_t currentHash = 1469598103934665603ull;
    const auto* bytePtr = reinterpret_cast<const uint8_t*>(vkInstances.data());
    for (size_t i = 0; i < byteCount; ++i) {
        currentHash ^= bytePtr[i];
        currentHash *= 1099511628211ull;
    }

    // Jeśli instancje i ich transformacje nie uległy zmianie i TLAS został już wybudowany,
    // całkowicie pomijamy pracę na GPU i aktualizację deskryptorów.
    if (m_tlasBuiltOnce && m_tlas.handle != VK_NULL_HANDLE && currentHash == m_lastInstanceHash) {
        return true;
    }

    bool handleChanged = false;

    // Sprawdzamy czy musimy zaalokować lub powiększyć trwałe bufory TLAS
    if (count > m_maxInstanceCapacity || m_tlas.handle == VK_NULL_HANDLE) {
        uint32_t newCapacity = std::max<uint32_t>(count * 2, 64);

        VkAccelerationStructureGeometryInstancesDataKHR queryInstances{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
        queryInstances.data.deviceAddress = 0;

        VkAccelerationStructureGeometryKHR queryGeom{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        queryGeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        queryGeom.geometry.instances = queryInstances;

        VkAccelerationStructureBuildGeometryInfoKHR queryBuild{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        queryBuild.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        queryBuild.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        queryBuild.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        queryBuild.geometryCount = 1;
        queryBuild.pGeometries = &queryGeom;

        VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        m_getBuildSizes(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &queryBuild, &newCapacity, &sizes);

        // Zwolnienie starych zasobów TLAS jeśli istniały
        if (m_tlas.handle) destroyResource(m_tlas);
        if (m_instanceBuffer) {
            vmaDestroyBuffer(m_context->getAllocator(), m_instanceBuffer, m_instanceAllocation);
            m_instanceBuffer = VK_NULL_HANDLE;
            m_instanceAllocation = VK_NULL_HANDLE;
            m_instanceMappedPtr = nullptr;
        }
        if (m_scratchBuffer) {
            vmaDestroyBuffer(m_context->getAllocator(), m_scratchBuffer, m_scratchAlloc);
            m_scratchBuffer = VK_NULL_HANDLE;
            m_scratchAlloc = VK_NULL_HANDLE;
        }

        // Alokacja trwałego bufora instancji
        m_instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * newCapacity;
        VkBufferCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        instanceInfo.size = m_instanceBufferSize;
        instanceInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VmaAllocationCreateInfo instanceAllocInfo{};
        instanceAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        instanceAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo mappedInfo{};
        if (vmaCreateBuffer(m_context->getAllocator(), &instanceInfo, &instanceAllocInfo,
                            &m_instanceBuffer, &m_instanceAllocation, &mappedInfo) != VK_SUCCESS) return false;
        m_instanceMappedPtr = mappedInfo.pMappedData;

        // Alokacja bufora TLAS
        m_tlas.size = sizes.accelerationStructureSize;
        if (!createAccelerationBuffer(m_tlas.size, m_tlas.buffer, m_tlas.allocation)) return false;

        VkAccelerationStructureCreateInfoKHR create{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        create.buffer = m_tlas.buffer;
        create.size = m_tlas.size;
        create.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        if (m_createAS(dev, &create, nullptr, &m_tlas.handle) != VK_SUCCESS) {
            destroyResource(m_tlas);
            return false;
        }

        // Alokacja trwałego bufora scratch
        VkDeviceSize maxScratch = std::max(sizes.buildScratchSize, sizes.updateScratchSize) + m_minScratchAlignment + 4096;
        m_scratchBufferSize = maxScratch;
        VkBufferCreateInfo scratchInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        scratchInfo.size = m_scratchBufferSize;
        scratchInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VmaAllocationCreateInfo scratchMem{};
        scratchMem.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (vmaCreateBuffer(m_context->getAllocator(), &scratchInfo, &scratchMem, &m_scratchBuffer, &m_scratchAlloc, nullptr) != VK_SUCCESS) {
            destroyResource(m_tlas);
            return false;
        }

        m_maxInstanceCapacity = newCapacity;
        handleChanged = true;
        m_tlasBuiltOnce = false;
    }

    // Kopiujemy dane instancji do trwałego bufora GPU
    std::memcpy(m_instanceMappedPtr, vkInstances.data(), byteCount);
    vmaFlushAllocation(m_context->getAllocator(), m_instanceAllocation, 0, byteCount);

    VkDeviceOrHostAddressConstKHR instanceAddress{};
    instanceAddress.deviceAddress = getBufferAddress(m_instanceBuffer);
    VkAccelerationStructureGeometryInstancesDataKHR instanceData{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
    instanceData.data = instanceAddress;

    VkAccelerationStructureGeometryKHR geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instanceData;

    VkAccelerationStructureBuildGeometryInfoKHR build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

    // Jeśli TLAS był już wybudowany i liczba instancji jest identyczna, wykonujemy szybki UPDATE zamiast pełnego BUILD:
    const bool canUpdate = m_tlasBuiltOnce && (count == m_lastInstanceCount);
    build.mode = canUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.srcAccelerationStructure = canUpdate ? m_tlas.handle : VK_NULL_HANDLE;
    build.dstAccelerationStructure = m_tlas.handle;
    build.geometryCount = 1;
    build.pGeometries = &geometry;

    const VkDeviceAddress rawScratch = getBufferAddress(m_scratchBuffer);
    build.scratchData.deviceAddress = (rawScratch + m_minScratchAlignment - 1) & ~(m_minScratchAlignment - 1);

    VkAccelerationStructureBuildRangeInfoKHR range{ count, 0, 0, 0 };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = { &range };
    m_cmdBuildAS(cmd, 1, &build, ranges);

    if (!m_tlasBuiltOnce) {
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
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);

    m_lastInstanceHash = currentHash;
    m_lastInstanceCount = count;
    m_tlasBuiltOnce = true;

    if (outHandleChanged) *outHandleChanged = handleChanged;
    return true;
}

void AccelerationStructure::releaseCompletedScratch() {
    if (!m_context || !m_context->getAllocator()) return;
    for (auto& scratch : m_retainedScratch) {
        if (scratch.first) vmaDestroyBuffer(m_context->getAllocator(), scratch.first, scratch.second);
    }
    m_retainedScratch.clear();
}

void AccelerationStructure::cleanup() {
    if (!m_context || !m_context->getDevice()) return;
    vkDeviceWaitIdle(m_context->getDevice());
    for (auto& entry : m_blas) destroyResource(entry.second.resource);
    m_blas.clear();
    destroyResource(m_tlas);
    if (m_instanceBuffer) {
        vmaDestroyBuffer(m_context->getAllocator(), m_instanceBuffer, m_instanceAllocation);
        m_instanceBuffer = VK_NULL_HANDLE;
        m_instanceAllocation = VK_NULL_HANDLE;
        m_instanceMappedPtr = nullptr;
    }
    if (m_scratchBuffer) {
        vmaDestroyBuffer(m_context->getAllocator(), m_scratchBuffer, m_scratchAlloc);
        m_scratchBuffer = VK_NULL_HANDLE;
        m_scratchAlloc = VK_NULL_HANDLE;
    }
    releaseCompletedScratch();
    m_ready = false;
}
}
