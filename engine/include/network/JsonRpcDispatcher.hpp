#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <ixwebsocket/IXWebSocket.h>
#include "core/ThreadQueue.hpp"
#include <functional>

namespace engine::core { class Engine; }

namespace engine::network {
    class JsonRpcDispatcher {
    public:
        using Command = std::function<void(core::Engine*)>;
        
        static void dispatch(const std::string& payload, ix::WebSocket* client, core::ThreadQueue<Command>& queue);
    };
}
