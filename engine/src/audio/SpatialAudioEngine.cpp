#include "audio/SpatialAudioEngine.hpp"
#include "ecs/Components.hpp"
#include <cmath>
#include <algorithm>

namespace engine::audio {

    SpatialSoundPacket SpatialAudioEngine::computeSpatialParameters(
        const glm::vec3& emitterPos,
        const glm::vec3& emitterVel,
        float baseVolume,
        float basePitch,
        float minDistance,
        float maxDistance,
        const glm::vec3& listenerPos,
        const glm::vec3& listenerForward,
        const glm::vec3& listenerVel
    ) const {
        SpatialSoundPacket packet;
        glm::vec3 toEmitter = emitterPos - listenerPos;
        float distance = glm::length(toEmitter);

        if (distance < 0.001f) {
            packet.finalVolume = baseVolume;
            packet.finalPitch = basePitch;
            packet.pan = 0.0f;
            return packet;
        }

        glm::vec3 dir = glm::normalize(toEmitter);

        // 1. Distance Attenuation (Inverse Square with Min/Max clamp)
        float clampedDist = std::clamp(distance, minDistance, maxDistance);
        float attenuation = minDistance / (minDistance + (clampedDist - minDistance) * 1.5f);
        if (distance > maxDistance) attenuation = 0.0f;
        packet.finalVolume = std::clamp(baseVolume * attenuation, 0.0f, 1.0f);

        // 2. Stereo Panning (-1.0 Left to +1.0 Right)
        glm::vec3 listenerUp(0.0f, 1.0f, 0.0f);
        glm::vec3 listenerRight = glm::normalize(glm::cross(listenerForward, listenerUp));
        packet.pan = std::clamp(glm::dot(dir, listenerRight), -1.0f, 1.0f);

        // 3. Doppler Pitch Shift: f' = f * (c - v_l) / (c - v_s)
        float vListener = glm::dot(listenerVel, dir);
        float vEmitter = glm::dot(emitterVel, dir);

        float denom = SPEED_OF_SOUND - vEmitter;
        if (std::abs(denom) > 1.0f) {
            float dopplerFactor = (SPEED_OF_SOUND - vListener) / denom;
            packet.finalPitch = std::clamp(basePitch * dopplerFactor, 0.2f, 3.0f);
        } else {
            packet.finalPitch = basePitch;
        }

        return packet;
    }

    void SpatialAudioEngine::update(
        ecs::Registry* registry,
        const glm::vec3& listenerPos,
        const glm::vec3& listenerForward,
        const glm::vec3& listenerVelocity,
        float
    ) {
        m_activePackets.clear();
        if (!registry) return;

        auto& reg = registry->get();
        auto view = reg.view<ecs::AudioSource3DComponent, ecs::TransformComponent>();

        for (auto entity : view) {
            auto& audio = view.get<ecs::AudioSource3DComponent>(entity);
            const auto& t = view.get<ecs::TransformComponent>(entity);

            if (!audio.isPlaying && !audio.loop) continue;

            glm::vec3 vel = audio.velocity;
            if (reg.all_of<ecs::VelocityComponent>(entity)) {
                vel = reg.get<ecs::VelocityComponent>(entity).velocity;
            }

            auto packet = computeSpatialParameters(
                t.position,
                vel,
                audio.volume,
                audio.pitch,
                audio.minDistance,
                audio.maxDistance,
                listenerPos,
                listenerForward,
                listenerVelocity
            );
            packet.soundAssetId = audio.soundAssetId;

            if (packet.finalVolume > 0.001f) {
                m_activePackets.push_back(packet);
            }
        }
    }

}
