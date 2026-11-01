#pragma once
#include <ixwebsocket/IXWebSocketServer.h>
#include "core/ThreadQueue.hpp"
#include <functional>

namespace engine::core { class Engine; }

namespace engine::network {
    using Command = std::function<void(core::Engine*)>;

    class WebSocketServer {
    public:
        WebSocketServer(int port, core::ThreadQueue<Command>& queue);
        ~WebSocketServer();

        void start();
        void stop();
        void sendToAll(const std::string& msg);
        void sendToClient(ix::WebSocket* client, const std::string& msg);

    private:
        void handleMessage(std::shared_ptr<ix::ConnectionState> connectionState, ix::WebSocket& webSocket, const ix::WebSocketMessagePtr& msg);

        ix::WebSocketServer m_server;
        core::ThreadQueue<Command>& m_queue;
    };
}
