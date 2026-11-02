#include "network/SharedStateRing.hpp"
#include "core/Logger.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cstdlib>
#endif

namespace engine::network {

SharedStateRingBuffer::SharedStateRingBuffer(const std::string& sharedMemoryName)
    : m_memoryName(sharedMemoryName) {

#ifdef _WIN32
    m_fileMappingHandle = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(sizeof(SharedStateRing)),
        m_memoryName.c_str()
    );

    if (m_fileMappingHandle != nullptr) {
        void* mappedView = MapViewOfFile(
            m_fileMappingHandle,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            sizeof(SharedStateRing)
        );

        if (mappedView != nullptr) {
            m_ring = static_cast<SharedStateRing*>(mappedView);
            m_isMapped = true;
            std::memset(m_ring, 0, sizeof(SharedStateRing));
            m_ring->active_slot.store(0, std::memory_order_release);
            core::Logger::info("[SharedStateRing] Win32 Shared Memory mapped: " + m_memoryName + " (" + std::to_string(sizeof(SharedStateRing)) + " bytes)");
            return;
        } else {
            CloseHandle(m_fileMappingHandle);
            m_fileMappingHandle = nullptr;
        }
    }
    core::Logger::warn("[SharedStateRing] Could not create Win32 shared memory, falling back to aligned heap memory.");
#endif

    // Fallback to aligned heap allocation
#ifdef _WIN32
    m_ring = static_cast<SharedStateRing*>(_aligned_malloc(sizeof(SharedStateRing), 64));
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 64, sizeof(SharedStateRing)) == 0) {
        m_ring = static_cast<SharedStateRing*>(ptr);
    }
#endif

    if (m_ring) {
        std::memset(m_ring, 0, sizeof(SharedStateRing));
        m_ring->active_slot.store(0, std::memory_order_release);
    }
}

SharedStateRingBuffer::~SharedStateRingBuffer() {
    if (m_isMapped && m_ring) {
#ifdef _WIN32
        UnmapViewOfFile(m_ring);
        if (m_fileMappingHandle) {
            CloseHandle(m_fileMappingHandle);
            m_fileMappingHandle = nullptr;
        }
#endif
        m_ring = nullptr;
        m_isMapped = false;
    } else if (m_ring) {
#ifdef _WIN32
        _aligned_free(m_ring);
#else
        std::free(m_ring);
#endif
        m_ring = nullptr;
    }
}

void SharedStateRingBuffer::publish(float cpuFrameMs, float outputLevelDb, uint32_t stateFlags, uint32_t activeEntities) {
    if (!m_ring) return;
    writeTelemetrySnapshot(*m_ring, m_sequenceCounter, cpuFrameMs, outputLevelDb, stateFlags, activeEntities);
}

bool SharedStateRingBuffer::readLatest(TelemetrySnapshot& outSnapshot, uint64_t& inOutLastSeq) const {
    if (!m_ring) return false;
    return readTelemetrySnapshotSeqlock(*m_ring, outSnapshot, inOutLastSeq);
}

} // namespace engine::network
