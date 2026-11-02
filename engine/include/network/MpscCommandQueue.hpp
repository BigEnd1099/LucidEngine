#pragma once

#include "network/DualTransportContracts.hpp"
#include <atomic>
#include <vector>
#include <cstdint>

namespace engine::network {

class MpscCommandQueue {
public:
    struct alignas(64) Node {
        std::atomic<Node*> next{nullptr};
        RpcCommandMessage msg{};
    };

    explicit MpscCommandQueue(size_t preallocatedNodes = 1024);
    ~MpscCommandQueue();

    // Non-copyable
    MpscCommandQueue(const MpscCommandQueue&) = delete;
    MpscCommandQueue& operator=(const MpscCommandQueue&) = delete;

    // Multi-producer push: safe to call concurrently from any thread without locks
    void enqueue(const RpcCommandMessage& msg);
    void enqueue(uint32_t commandId, uint32_t requestId, const void* payloadData, size_t payloadLen);

    // Single-consumer pop: pops all currently pending commands in FIFO order in O(1)
    void dequeueAll(std::vector<RpcCommandMessage>& outCommands);

    // Check if queue has any pending commands
    bool empty() const {
        return m_head.load(std::memory_order_relaxed) == nullptr;
    }

private:
    Node* allocateNode();
    void recycleNodes(Node* chainHead);

    std::atomic<Node*> m_head{nullptr};

    // Lock-free free-list for node recycling (eliminates dynamic allocations)
    std::atomic<Node*> m_freeList{nullptr};
    std::vector<void*> m_slabs;
};

} // namespace engine::network
