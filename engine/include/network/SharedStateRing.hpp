#pragma once

#include "network/DualTransportContracts.hpp"
#include <memory>
#include <string>

namespace engine::network {

class SharedStateRingBuffer {
public:
    explicit SharedStateRingBuffer(const std::string& sharedMemoryName = "Local\\LucidEngine_SharedRing_v1");
    ~SharedStateRingBuffer();

    // Non-copyable
    SharedStateRingBuffer(const SharedStateRingBuffer&) = delete;
    SharedStateRingBuffer& operator=(const SharedStateRingBuffer&) = delete;

    // Writes a new TelemetrySnapshot to the 32-slot seqlock ring buffer
    void publish(float cpuFrameMs, float outputLevelDb, uint32_t stateFlags, uint32_t activeEntities);

    // Reads the latest consistent TelemetrySnapshot into private buffer (tail -> memcpy -> head)
    bool readLatest(TelemetrySnapshot& outSnapshot, uint64_t& inOutLastSeq) const;

    // Direct access to the shared ring buffer
    SharedStateRing* getRing() { return m_ring; }
    const SharedStateRing* getRing() const { return m_ring; }

    const void* getRawBuffer() const { return m_ring; }
    size_t getBufferSize() const { return sizeof(SharedStateRing); }

    bool isSharedMemoryMapped() const { return m_isMapped; }

private:
    std::string m_memoryName;
    SharedStateRing* m_ring{nullptr};
    void* m_fileMappingHandle{nullptr};
    bool m_isMapped{false};
    uint64_t m_sequenceCounter{0};
};

} // namespace engine::network
