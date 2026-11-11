#include "network/SinglePortUnifiedServer.hpp"
#include "network/EmbeddedAssets.hpp"
#include "core/Logger.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace engine::network {

SinglePortUnifiedServer::SinglePortUnifiedServer(
    int port,
    SharedStateRingBuffer& ringBuffer,
    MpscCommandQueue& commandQueue,
    core::ThreadQueue<network::JsonRpcDispatcher::Command>& legacyQueue
)
    : m_port(port)
    , m_ringBuffer(ringBuffer)
    , m_commandQueue(commandQueue)
    , m_legacyQueue(legacyQueue)
    , m_httpServer(port, "0.0.0.0")
{
    // 1. Handle HTTP Requests (Embedded web assets with disk hot-reload on single port)
    m_httpServer.setOnConnectionCallback([this](const ix::HttpRequestPtr& req, std::shared_ptr<ix::ConnectionState>) -> ix::HttpResponsePtr {
        // A. Priority 1: Check live 'dist/' folder on disk for instant hot-reload when src/App.tsx is edited
        std::string reqPath = req->uri;
        size_t queryPos = reqPath.find('?');
        if (queryPos != std::string::npos) {
            reqPath = reqPath.substr(0, queryPos);
        }
        if (reqPath.empty() || reqPath == "/") {
            reqPath = "/index.html";
        }

        std::vector<std::filesystem::path> diskCandidates = {
            std::filesystem::path("dist") / reqPath.substr(1),
            std::filesystem::path("../dist") / reqPath.substr(1),
            std::filesystem::path("../../dist") / reqPath.substr(1)
        };

        for (const auto& diskPath : diskCandidates) {
            if (std::filesystem::exists(diskPath) && !std::filesystem::is_directory(diskPath)) {
                std::ifstream f(diskPath, std::ios::binary);
                if (f) {
                    std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    ix::WebSocketHttpHeaders headers;
                    std::string ext = diskPath.extension().string();
                    if (ext == ".html") headers["Content-Type"] = "text/html; charset=utf-8";
                    else if (ext == ".js" || ext == ".mjs") headers["Content-Type"] = "application/javascript; charset=utf-8";
                    else if (ext == ".css") headers["Content-Type"] = "text/css; charset=utf-8";
                    else if (ext == ".json") headers["Content-Type"] = "application/json";
                    else if (ext == ".svg") headers["Content-Type"] = "image/svg+xml";
                    else if (ext == ".png") headers["Content-Type"] = "image/png";
                    else if (ext == ".jpg" || ext == ".jpeg") headers["Content-Type"] = "image/jpeg";
                    else if (ext == ".ico") headers["Content-Type"] = "image/x-icon";
                    else if (ext == ".woff2") headers["Content-Type"] = "font/woff2";
                    else headers["Content-Type"] = "application/octet-stream";

                    headers["Access-Control-Allow-Origin"] = "*";
                    headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";
                    headers["Cache-Control"] = "no-cache";
                    return std::make_shared<ix::HttpResponse>(200, "OK", ix::HttpErrorCode::Ok, headers, body);
                }
            }
        }

        // B. Priority 2: Fallback to baked-in embedded assets (EmbeddedAssets.hpp)
        const uint8_t* assetData = nullptr;
        size_t assetSize = 0;
        const char* assetMime = nullptr;

        if (getEmbeddedAsset(req->uri, assetData, assetSize, assetMime)) {
            ix::WebSocketHttpHeaders headers;
            headers["Content-Type"] = assetMime;
            headers["Access-Control-Allow-Origin"] = "*";
            headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";
            headers["Cache-Control"] = "public, max-age=3600";
            std::string body(reinterpret_cast<const char*>(assetData), assetSize);
            return std::make_shared<ix::HttpResponse>(200, "OK", ix::HttpErrorCode::Ok, headers, body);
        }

        // 404 fallback
        ix::WebSocketHttpHeaders headers;
        headers["Content-Type"] = "text/plain";
        return std::make_shared<ix::HttpResponse>(404, "Not Found", ix::HttpErrorCode::Ok, headers, "404 Not Found");
    });

    // 2. Handle WebSocket Connections and Messages
    m_httpServer.setOnClientMessageCallback([this](std::shared_ptr<ix::ConnectionState> connectionState, ix::WebSocket& webSocket, const ix::WebSocketMessagePtr& msg) {
        handleWebSocketMessage(connectionState, webSocket, msg);
    });
}

SinglePortUnifiedServer::~SinglePortUnifiedServer() {
    stop();
}

bool SinglePortUnifiedServer::start() {
    if (m_running.load(std::memory_order_relaxed)) return true;

    auto res = m_httpServer.listen();
    if (!res.first) {
        core::Logger::error("[UnifiedServer] Failed to listen on port " + std::to_string(m_port) + ": " + res.second);
        return false;
    }

    m_httpServer.start();
    m_running.store(true, std::memory_order_release);
    core::Logger::info("[UnifiedServer] Unified HTTP + WebSocket server listening on port " + std::to_string(m_port));

    // Spawn dedicated 60 Hz telemetry streaming thread
    m_streamingThread = std::thread(&SinglePortUnifiedServer::telemetryStreamingLoop, this);
    return true;
}

void SinglePortUnifiedServer::stop() {
    if (!m_running.load(std::memory_order_relaxed)) return;

    m_running.store(false, std::memory_order_release);
    m_httpServer.stop();

    if (m_streamingThread.joinable()) {
        m_streamingThread.join();
    }
    core::Logger::info("[UnifiedServer] Unified server stopped.");
}

void SinglePortUnifiedServer::handleWebSocketMessage(
    const std::shared_ptr<ix::ConnectionState>& connectionState,
    ix::WebSocket& webSocket,
    const ix::WebSocketMessagePtr& msg
) {
    if (msg->type == ix::WebSocketMessageType::Message) {
        if (msg->binary) {
            {
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                m_binaryClients.insert(&webSocket);
            }
            // Binary Dual-Transport Protocol
            if (msg->str.size() >= sizeof(NetworkFrameHeader)) {
                const auto* hdr = reinterpret_cast<const NetworkFrameHeader*>(msg->str.data());
                if (hdr->type == FRAME_TYPE_RPC_COMMAND && hdr->payload_len >= 8) {
                    const uint8_t* payloadPtr = reinterpret_cast<const uint8_t*>(msg->str.data()) + sizeof(NetworkFrameHeader);
                    uint32_t cmdId = *reinterpret_cast<const uint32_t*>(payloadPtr);
                    uint32_t reqId = *reinterpret_cast<const uint32_t*>(payloadPtr + 4);
                    const void* cmdData = payloadPtr + 8;
                    size_t cmdLen = hdr->payload_len > 8 ? (hdr->payload_len - 8) : 0;

                    m_commandQueue.enqueue(cmdId, reqId, cmdData, cmdLen);

                    // Send immediate binary ACK (0x11)
                    char ackPacket[sizeof(NetworkFrameHeader) + 8];
                    auto* ackHdr = reinterpret_cast<NetworkFrameHeader*>(ackPacket);
                    ackHdr->type = FRAME_TYPE_RPC_ACK;
                    ackHdr->flags = 0;
                    ackHdr->stream_id = hdr->stream_id;
                    ackHdr->payload_len = 8;
                    *reinterpret_cast<uint32_t*>(ackPacket + sizeof(NetworkFrameHeader)) = cmdId;
                    *reinterpret_cast<uint32_t*>(ackPacket + sizeof(NetworkFrameHeader) + 4) = reqId;

                    try {
                        webSocket.sendBinary(std::string(ackPacket, sizeof(ackPacket)));
                    } catch (...) {}
                }
            }
        } else {
            // Text JSON-RPC message (Backward-compatibility with existing tests and scripts)
            JsonRpcDispatcher::dispatch(msg->str, &webSocket, m_legacyQueue);
        }
    } else if (msg->type == ix::WebSocketMessageType::Open) {
        if (msg->openInfo.uri.find("binary") != std::string::npos || msg->openInfo.protocol == "binary") {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            m_binaryClients.insert(&webSocket);
            core::Logger::info("[UnifiedServer] Client registered for binary stream: " + connectionState->getId());
        }
        core::Logger::info("[UnifiedServer] New WebSocket connection: " + connectionState->getId());
    } else if (msg->type == ix::WebSocketMessageType::Close) {
        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            m_binaryClients.erase(&webSocket);
        }
        core::Logger::info("[UnifiedServer] WebSocket closed: " + connectionState->getId());
    } else if (msg->type == ix::WebSocketMessageType::Error) {
        core::Logger::error("[UnifiedServer] WebSocket error: " + msg->errorInfo.reason);
    }
}

void SinglePortUnifiedServer::telemetryStreamingLoop() {
    while (m_running.load(std::memory_order_relaxed)) {
        TelemetrySnapshot snapshot{};
        if (m_ringBuffer.readLatest(snapshot, m_lastBroadcastSeq)) {
            // Encode binary packet: 8 bytes NetworkFrameHeader + 40 bytes TelemetrySnapshot = 48 bytes
            char packet[sizeof(NetworkFrameHeader) + sizeof(TelemetrySnapshot)];
            auto* hdr = reinterpret_cast<NetworkFrameHeader*>(packet);
            hdr->type = FRAME_TYPE_FULL_SNAPSHOT;
            hdr->flags = 0;
            hdr->stream_id = 0;
            hdr->payload_len = sizeof(TelemetrySnapshot);
            std::memcpy(packet + sizeof(NetworkFrameHeader), &snapshot, sizeof(TelemetrySnapshot));

            std::string binaryPayload(packet, sizeof(packet));

            std::unordered_set<ix::WebSocket*> targets;
            {
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                targets = m_binaryClients;
            }

            for (auto client : m_httpServer.getClients()) {
                if (!client) continue;
                bool isBinary = (client->getUrl().find("binary") != std::string::npos) || (targets.find(client.get()) != targets.end());
                if (!isBinary) continue;

                if (client->getReadyState() == ix::ReadyState::Open) {
                    // Strict 512B threshold backpressure check requested by user
                    if (client->bufferedAmount() > kMaxBufferedAmount) {
                        continue; // Drop frame for slow client, prevent main thread blocking
                    }
                    try {
                        client->sendBinary(binaryPayload);
                    } catch (...) {}
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 Hz
    }
}

void SinglePortUnifiedServer::broadcastBinaryFrame(BinaryFrameType type, const void* payload, size_t payloadLen) {
    size_t totalLen = sizeof(NetworkFrameHeader) + payloadLen;
    std::string packet(totalLen, '\0');
    auto* hdr = reinterpret_cast<NetworkFrameHeader*>(packet.data());
    hdr->type = type;
    hdr->flags = 0;
    hdr->stream_id = 0;
    hdr->payload_len = static_cast<uint32_t>(payloadLen);
    if (payload && payloadLen > 0) {
        std::memcpy(packet.data() + sizeof(NetworkFrameHeader), payload, payloadLen);
    }

    for (auto client : m_httpServer.getClients()) {
        if (client && client->getReadyState() == ix::ReadyState::Open) {
            if (client->bufferedAmount() > kMaxBufferedAmount) continue;
            try {
                client->sendBinary(packet);
            } catch (...) {}
        }
    }
}

void SinglePortUnifiedServer::broadcastTextMessage(const std::string& text) {
    try {
        for (auto client : m_httpServer.getClients()) {
            if (client && client->getReadyState() == ix::ReadyState::Open) {
                client->sendText(text);
            }
        }
    } catch (...) {}
}

} // namespace engine::network
