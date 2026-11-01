#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <span>
#include <vector>
#include <cstdint>

namespace engine::anim {

#pragma pack(push, 1)
    struct BoneTransformRaw {
        uint16_t boneId;
        glm::vec3 translation;  // 12 bajtów
        glm::quat rotation;     // 16 bajtów
        glm::vec3 scale;        // 12 bajtów
    };

    struct MotionFramePacketHeader {
        uint32_t magic;         // 0x414E494D ("ANIM")
        uint32_t entityId;      // Identyfikator encji docelowej
        uint32_t frameIndex;
        uint16_t boneCount;
        uint16_t reserved;
    };
#pragma pack(pop)

    // Komponent przypinany do encji w ECS
    struct SkeletalMeshComponent {
        std::vector<glm::mat4> boneMatrices; // Przesyłane do SSBO na GPU
        uint32_t skinningBufferIndex;        // Slot w Bindless SSBO
    };

} // namespace engine::anim
