#pragma once
#include "ecs/Registry.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace engine::audio {

    struct SpatialSoundPacket {
        std::string soundAssetId;
        float finalVolume{1.0f};
        float finalPitch{1.0f};
        float pan{-1.0f}; // -1.0 = full left, +1.0 = full right
        bool isOccluded{false};
    };

    class SpatialAudioEngine {
    public:
        SpatialAudioEngine() = default;

        void update(
            ecs::Registry* registry,
            const glm::vec3& listenerPos,
            const glm::vec3& listenerForward,
            const glm::vec3& listenerVelocity,
            float dt
        );

        SpatialSoundPacket computeSpatialParameters(
            const glm::vec3& emitterPos,
            const glm::vec3& emitterVel,
            float baseVolume,
            float basePitch,
            float minDistance,
            float maxDistance,
            const glm::vec3& listenerPos,
            const glm::vec3& listenerForward,
            const glm::vec3& listenerVel
        ) const;

        const std::vector<SpatialSoundPacket>& getActiveAudioPackets() const { return m_activePackets; }

    private:
        static constexpr float SPEED_OF_SOUND = 343.0f; // m/s
        std::vector<SpatialSoundPacket> m_activePackets;
    };

}
