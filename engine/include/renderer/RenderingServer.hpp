#pragma once
#include "renderer/RenderFrame.hpp"
#include "ecs/Registry.hpp"
#include <mutex>
#include <array>
#include <atomic>
#include <memory>
#include <functional>
#include <vector>

namespace engine::renderer {

    class VulkanRenderer;

    class RenderingServer {
    public:
        static constexpr size_t BUFFER_COUNT = 3;

        RenderingServer();
        ~RenderingServer() = default;

        // Extracts a thread-safe snapshot of renderable scene data from ECS registry
        static RenderFrame extractRenderFrame(ecs::Registry* registry,
                                              const RenderCamera& camera,
                                              uint64_t frameIndex,
                                              float dt);

        // Frame queue for decoupled Game Thread -> Render Thread handoff
        void pushRenderFrame(RenderFrame&& frame);
        bool popRenderFrame(RenderFrame& outFrame);
        bool hasPendingFrame() const;

        // Command Queue API for decoupled render commands
        void enqueueCommand(std::function<void(VulkanRenderer&)> cmd);
        void executeCommands(VulkanRenderer& renderer);

        // Instance stats
        size_t getLastInstanceCount() const { return m_lastInstanceCount.load(); }
        size_t getLastLightCount() const { return m_lastLightCount.load(); }

    private:
        std::array<RenderFrame, BUFFER_COUNT> m_frameRing;
        std::atomic<uint64_t> m_writeIndex{0};
        std::atomic<uint64_t> m_readIndex{0};
        mutable std::mutex m_frameMutex;

        std::vector<std::function<void(VulkanRenderer&)>> m_commandQueue;
        std::mutex m_commandMutex;

        std::atomic<size_t> m_lastInstanceCount{0};
        std::atomic<size_t> m_lastLightCount{0};
    };

} // namespace engine::renderer
