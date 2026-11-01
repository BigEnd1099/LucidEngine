#include "network/UdpAnimationServer.hpp"
#include "animation/SkeletonStream.hpp"
#include "core/Engine.hpp"
#include "core/Logger.hpp"

namespace engine::network {

    UdpAnimationServer::UdpAnimationServer(int port, core::ThreadQueue<JsonRpcDispatcher::Command>& queue)
        : m_socket(m_ioContext), m_queue(queue) {
        try {
            m_socket.open(asio::ip::udp::v4());
            m_socket.set_option(asio::socket_base::reuse_address(true));
            m_socket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), port));
        } catch (const std::exception& e) {
            core::Logger::warn(std::string("UDP Animation Server bind warning (port ") + std::to_string(port) + "): " + e.what());
        }
    }

    UdpAnimationServer::~UdpAnimationServer() {
        stop();
    }

    void UdpAnimationServer::start() {
        if (!m_socket.is_open()) return;
        doReceive();
        m_thread = std::jthread([this]() { 
            try {
                core::Logger::info("UDP Animation server started on port " + std::to_string(m_socket.local_endpoint().port()));
                m_ioContext.run(); 
            } catch (...) {}
        });
    }

    void UdpAnimationServer::stop() {
        m_ioContext.stop();
    }

    void UdpAnimationServer::doReceive() {
        m_socket.async_receive_from(
            asio::buffer(m_recvBuffer), m_senderEndpoint,
            [this](std::error_code ec, std::size_t bytes_recvd) {
                if (!ec && bytes_recvd >= sizeof(anim::MotionFramePacketHeader)) {
                    auto* header = reinterpret_cast<const anim::MotionFramePacketHeader*>(m_recvBuffer.data());
                    if (header->magic == 0x414E494D) { // "ANIM"
                        size_t expectedSize = sizeof(anim::MotionFramePacketHeader) + header->boneCount * sizeof(anim::BoneTransformRaw);
                        if (bytes_recvd >= expectedSize) {
                            std::vector<anim::BoneTransformRaw> bones(header->boneCount);
                            std::memcpy(bones.data(), m_recvBuffer.data() + sizeof(anim::MotionFramePacketHeader), bones.size() * sizeof(anim::BoneTransformRaw));
                            
                            m_queue.push([entityId = header->entityId, bones = std::move(bones)](core::Engine* eng) {
                                eng->updateEntityBones(entityId, bones);
                            });
                        }
                    }
                }
                doReceive();
            });
    }

}
