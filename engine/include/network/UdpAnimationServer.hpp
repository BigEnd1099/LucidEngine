#pragma once
#include <asio.hpp>
#include <array>
#include <thread>
#include "core/ThreadQueue.hpp"
#include "network/JsonRpcDispatcher.hpp"

namespace engine::network {

    class UdpAnimationServer {
    public:
        UdpAnimationServer(int port, core::ThreadQueue<JsonRpcDispatcher::Command>& queue);
        ~UdpAnimationServer();

        void start();
        void stop();

    private:
        void doReceive();
        
        asio::io_context m_ioContext;
        asio::ip::udp::socket m_socket;
        asio::ip::udp::endpoint m_senderEndpoint;
        std::array<uint8_t, 8192> m_recvBuffer;
        
        core::ThreadQueue<JsonRpcDispatcher::Command>& m_queue;
        std::jthread m_thread;
    };

}
