#pragma once

#include "network/DualTransportContracts.hpp"
#include "network/SharedStateRing.hpp"
#include "network/MpscCommandQueue.hpp"
#include "network/JsonRpcDispatcher.hpp"
#include <ixwebsocket/IXHttpServer.h>
#include <atomic>
#include <thread>
#include <memory>
#include <string>
#include <mutex>
#include <unordered_set>

namespace engine::network {

class SinglePortUnifiedServer {
public:
    SinglePortUnifiedServer(
        int port,
        SharedStateRingBuffer& ringBuffer,
        MpscCommandQueue& commandQueue,
        core::ThreadQueue<network::JsonRpcDispatcher::Command>& legacyQueue
    );
    ~SinglePortUnifiedServer();

    // Starts the unified HTTP + WebSocket server and telemetry streaming thread
    bool start();

    // Stops the server and joins the background streaming thread
    void stop();

    // Broadcasts a binary frame to all connected clients respecting 512B backpressure
    void broadcastBinaryFrame(BinaryFrameType type, const void* payload, size_t payloadLen);

    // Broadcasts text message (legacy JSON-RPC compatibility)
    void broadcastTextMessage(const std::string& text);

    int getPort() const { return m_port; }
    bool isRunning() const { return m_running.load(std::memory_order_relaxed); }

private:
    void handleHttpMessage(
        const ix::HttpRequestPtr& request,
        const std::shared_ptr<ix::ConnectionState>& connectionState,
        ix::HttpResponsePtr& response
    );

    void handleWebSocketMessage(
        const std::shared_ptr<ix::ConnectionState>& connectionState,
        ix::WebSocket& webSocket,
        const ix::WebSocketMessagePtr& msg
    );

    void telemetryStreamingLoop();

    int m_port;
    SharedStateRingBuffer& m_ringBuffer;
    MpscCommandQueue& m_commandQueue;
    core::ThreadQueue<network::JsonRpcDispatcher::Command>& m_legacyQueue;

    ix::HttpServer m_httpServer;
    std::atomic<bool> m_running{false};
    std::thread m_streamingThread;
    uint64_t m_lastBroadcastSeq{0};

    mutable std::mutex m_clientsMutex;
    std::unordered_set<ix::WebSocket*> m_binaryClients;
};

} // namespace engine::network
