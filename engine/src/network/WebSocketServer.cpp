#include "network/JsonRpcDispatcher.hpp"
#include "network/WebSocketServer.hpp"
#include "core/Logger.hpp"

namespace engine::network {
    WebSocketServer::WebSocketServer(int port, core::ThreadQueue<Command>& queue)
        : m_server(port, "0.0.0.0"), m_queue(queue) {
        
        m_server.setOnClientMessageCallback([this](std::shared_ptr<ix::ConnectionState> connectionState, ix::WebSocket& webSocket, const ix::WebSocketMessagePtr& msg) {
            handleMessage(connectionState, webSocket, msg);
        });
    }

    WebSocketServer::~WebSocketServer() {
        stop();
    }

    void WebSocketServer::start() {
        auto res = m_server.listen();
        if (!res.first) {
            core::Logger::error("Failed to start WebSocket server: " + res.second);
            return;
        }
        m_server.start();
        core::Logger::info("WebSocket server started on port " + std::to_string(m_server.getPort()));
    }

    void WebSocketServer::stop() {
        m_server.stop();
    }

    void WebSocketServer::sendToAll(const std::string& msg) {
        try {
            for (auto client : m_server.getClients()) {
                if (client) client->sendText(msg);
            }
        } catch (...) {}
    }

    void WebSocketServer::sendToClient(ix::WebSocket* targetClient, const std::string& msg) {
        if (!targetClient) return;
        try {
            for (const auto& client : m_server.getClients()) {
                if (client && client.get() == targetClient) {
                    if (client->getReadyState() == ix::ReadyState::Open) {
                        client->sendText(msg);
                    }
                    return;
                }
            }
        } catch (...) {}
    }

    void WebSocketServer::handleMessage(std::shared_ptr<ix::ConnectionState> connectionState, ix::WebSocket& webSocket, const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Message) {
            JsonRpcDispatcher::dispatch(msg->str, &webSocket, m_queue);
        } else if (msg->type == ix::WebSocketMessageType::Open) {
            core::Logger::info("New WebSocket connection: " + connectionState->getId());
        } else if (msg->type == ix::WebSocketMessageType::Close) {
            core::Logger::info("WebSocket closed: " + connectionState->getId());
        } else if (msg->type == ix::WebSocketMessageType::Error) {
            core::Logger::error("WebSocket error: " + msg->errorInfo.reason);
        }
    }
}
