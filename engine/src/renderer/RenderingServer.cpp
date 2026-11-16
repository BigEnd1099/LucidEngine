#include "renderer/RenderingServer.hpp"
#include "ecs/Components.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace engine::renderer {

    RenderingServer::RenderingServer() {
        for (size_t i = 0; i < BUFFER_COUNT; ++i) {
            m_frameRing[i].instances.reserve(4096);
            m_frameRing[i].lights.reserve(64);
        }
    }

    RenderFrame RenderingServer::extractRenderFrame(ecs::Registry* registry,
                                                    const RenderCamera& camera,
                                                    uint64_t frameIndex,
                                                    float dt) {
        RenderFrame frame;
        frame.frameIndex = frameIndex;
        frame.deltaTime = dt;
        frame.camera = camera;

        if (!registry) return frame;

        const auto& reg = registry->get();

        // 1. Extract Mesh Instances
        auto meshView = reg.view<ecs::TransformComponent, ecs::MeshComponent>();
        frame.instances.reserve(meshView.size_hint());

        for (const entt::entity entity : meshView) {
            const auto& transform = meshView.get<ecs::TransformComponent>(entity);
            const auto& mesh = meshView.get<ecs::MeshComponent>(entity);

            if (!mesh.visible) continue;

            glm::quat orientation = transform.orientation;
            if (glm::dot(orientation, orientation) < 1e-6f || !std::isfinite(orientation.w)) {
                orientation = glm::quat(glm::radians(transform.rotation));
            } else {
                orientation = glm::normalize(orientation);
            }

            glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.position) *
                              glm::mat4_cast(orientation) *
                              glm::scale(glm::mat4(1.0f), transform.scale);

            RenderInstance inst{};
            inst.transform = model;
            inst.meshId = mesh.meshId;
            inst.materialId = mesh.materialId;
            inst.albedoIdx = mesh.albedoIdx;
            inst.normalIdx = mesh.normalIdx;
            inst.ormIdx = mesh.ormIdx;
            inst.entityId = static_cast<uint32_t>(entity);
            inst.visible = mesh.visible;
            inst.castShadow = true;
            inst.rtEnabled = true;

            frame.instances.push_back(inst);
        }

        // 2. Extract Lights
        auto lightView = reg.view<ecs::TransformComponent, ecs::LightComponent>();
        frame.lights.reserve(lightView.size_hint());

        for (const entt::entity entity : lightView) {
            const auto& transform = lightView.get<ecs::TransformComponent>(entity);
            const auto& light = lightView.get<ecs::LightComponent>(entity);

            RenderLight rl{};
            rl.position = transform.position;
            rl.color = light.color;
            rl.intensity = light.intensity;
            rl.range = light.range;
            rl.type = light.type;
            rl.innerConeAngle = light.innerConeAngle;
            rl.outerConeAngle = light.outerConeAngle;

            frame.lights.push_back(rl);
        }

        return frame;
    }

    void RenderingServer::pushRenderFrame(RenderFrame&& frame) {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        uint64_t w = m_writeIndex.load();
        size_t slot = static_cast<size_t>(w % BUFFER_COUNT);
        m_frameRing[slot] = std::move(frame);
        m_lastInstanceCount.store(m_frameRing[slot].instances.size());
        m_lastLightCount.store(m_frameRing[slot].lights.size());
        m_writeIndex.store(w + 1);
    }

    bool RenderingServer::popRenderFrame(RenderFrame& outFrame) {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        uint64_t r = m_readIndex.load();
        uint64_t w = m_writeIndex.load();
        if (r >= w) return false;

        size_t slot = static_cast<size_t>(r % BUFFER_COUNT);
        outFrame = m_frameRing[slot];
        m_readIndex.store(r + 1);
        return true;
    }

    bool RenderingServer::hasPendingFrame() const {
        return m_readIndex.load() < m_writeIndex.load();
    }

    void RenderingServer::enqueueCommand(std::function<void(VulkanRenderer&)> cmd) {
        std::lock_guard<std::mutex> lock(m_commandMutex);
        m_commandQueue.push_back(std::move(cmd));
    }

    void RenderingServer::executeCommands(VulkanRenderer& renderer) {
        std::vector<std::function<void(VulkanRenderer&)>> toExecute;
        {
            std::lock_guard<std::mutex> lock(m_commandMutex);
            toExecute.swap(m_commandQueue);
        }
        for (auto& cmd : toExecute) {
            if (cmd) {
                cmd(renderer);
            }
        }
    }

} // namespace engine::renderer
