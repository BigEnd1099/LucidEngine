#include "network/MpscCommandQueue.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>

namespace engine::network {

MpscCommandQueue::MpscCommandQueue(size_t preallocatedNodes) {
    if (preallocatedNodes == 0) preallocatedNodes = 64;

    size_t slabBytes = preallocatedNodes * sizeof(Node);
#ifdef _WIN32
    void* slab = _aligned_malloc(slabBytes, 64);
#else
    void* slab = nullptr;
    if (posix_memalign(&slab, 64, slabBytes) != 0) slab = nullptr;
#endif

    if (slab) {
        m_slabs.push_back(slab);
        auto* nodes = static_cast<Node*>(slab);
        for (size_t i = 0; i < preallocatedNodes; ++i) {
            new (&nodes[i]) Node();
            Node* oldFree = m_freeList.load(std::memory_order_relaxed);
            nodes[i].next.store(oldFree, std::memory_order_relaxed);
            m_freeList.store(&nodes[i], std::memory_order_relaxed);
        }
    }
}

MpscCommandQueue::~MpscCommandQueue() {
    for (void* slab : m_slabs) {
#ifdef _WIN32
        _aligned_free(slab);
#else
        std::free(slab);
#endif
    }
    m_slabs.clear();
}

MpscCommandQueue::Node* MpscCommandQueue::allocateNode() {
    Node* oldHead = m_freeList.load(std::memory_order_acquire);
    while (oldHead != nullptr) {
        Node* next = oldHead->next.load(std::memory_order_relaxed);
        if (m_freeList.compare_exchange_weak(oldHead, next, std::memory_order_release, std::memory_order_acquire)) {
            oldHead->next.store(nullptr, std::memory_order_relaxed);
            return oldHead;
        }
    }

    // Free list exhausted, dynamically allocate emergency node (aligned)
#ifdef _WIN32
    auto* node = static_cast<Node*>(_aligned_malloc(sizeof(Node), 64));
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 64, sizeof(Node)) != 0) ptr = nullptr;
    auto* node = static_cast<Node*>(ptr);
#endif
    if (node) {
        new (node) Node();
    }
    return node;
}

void MpscCommandQueue::recycleNodes(Node* chainHead) {
    if (!chainHead) return;

    // Find the tail of the chain
    Node* chainTail = chainHead;
    while (true) {
        Node* next = chainTail->next.load(std::memory_order_relaxed);
        if (!next) break;
        chainTail = next;
    }

    // Atomically prepend the entire chain to m_freeList
    Node* oldFree = m_freeList.load(std::memory_order_relaxed);
    do {
        chainTail->next.store(oldFree, std::memory_order_relaxed);
    } while (!m_freeList.compare_exchange_weak(oldFree, chainHead, std::memory_order_release, std::memory_order_relaxed));
}

void MpscCommandQueue::enqueue(const RpcCommandMessage& msg) {
    Node* node = allocateNode();
    if (!node) return;

    node->msg = msg;

    Node* oldHead = m_head.load(std::memory_order_relaxed);
    do {
        node->next.store(oldHead, std::memory_order_relaxed);
    } while (!m_head.compare_exchange_weak(oldHead, node, std::memory_order_release, std::memory_order_relaxed));
}

void MpscCommandQueue::enqueue(uint32_t commandId, uint32_t requestId, const void* payloadData, size_t payloadLen) {
    RpcCommandMessage msg{};
    msg.command_id = commandId;
    msg.request_id = requestId;
    msg.payload_len = static_cast<uint32_t>(std::min(payloadLen, sizeof(msg.payload)));
    if (payloadData && msg.payload_len > 0) {
        std::memcpy(msg.payload, payloadData, msg.payload_len);
    }
    enqueue(msg);
}

void MpscCommandQueue::dequeueAll(std::vector<RpcCommandMessage>& outCommands) {
    Node* head = m_head.exchange(nullptr, std::memory_order_acq_rel);
    if (!head) return;

    // Reverse list from LIFO to FIFO order
    Node* prev = nullptr;
    Node* curr = head;
    while (curr) {
        Node* next = curr->next.load(std::memory_order_relaxed);
        curr->next.store(prev, std::memory_order_relaxed);
        prev = curr;
        curr = next;
    }

    // Now 'prev' is the FIFO head
    Node* node = prev;
    while (node) {
        outCommands.push_back(node->msg);
        node = node->next.load(std::memory_order_relaxed);
    }

    // Recycle entire node chain back to free list in O(1)
    recycleNodes(prev);
}

} // namespace engine::network
