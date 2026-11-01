#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <cstdint>
#include <vector>

namespace engine::ecs {
    struct TransformComponent {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 rotation{0.0f, 0.0f, 0.0f}; // Euler angles (degrees) for editor & scripts
        glm::vec3 scale{1.0f, 1.0f, 1.0f};
        glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f}; // Accurate 6-DOF quaternion (w, x, y, z)
    };

    struct MeshComponent {
        uint32_t meshId{0};
        uint32_t materialId{0};
        uint32_t albedoIdx{0};
        uint32_t normalIdx{0};
        uint32_t ormIdx{0};
    };

    struct LODComponent {
        std::vector<uint32_t> lodMeshIds{0}; // Mesh IDs for LOD0, LOD1, LOD2...
        std::vector<float> lodDistances{15.0f, 40.0f}; // Distance thresholds
        uint32_t currentLod{0};
    };

    struct VelocityComponent {
        glm::vec3 velocity{0.0f, 0.0f, 0.0f};
    };

    struct RigidBodyComponent {
        uint32_t bodyId{0xFFFFFFFF}; // Invalid by default
        float mass{1.0f};
        int shapeType{0}; // 0 = Box, 1 = Sphere, 2 = Capsule
        glm::vec3 halfExtents{0.5f, 0.5f, 0.5f};
        float capsuleRadius{0.5f};
        float capsuleHalfHeight{0.8f};
        float restitution{0.6f}; // Bounciness: 0.0 = completely inelastic, 1.0 = full bounce
        float friction{0.3f};    // Friction coefficient
        bool lockRotationX{false};
        bool lockRotationY{false};
        bool lockRotationZ{false};
        bool isSensor{false}; // Trigger / sensor volume (no collision response, generates contact events)
    };

    struct MaterialComponent {
        glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
        float metallic{0.1f};
        float roughness{0.5f};
    };

    struct TagComponent {
        std::string name;
    };

    struct ScriptableComponent {
        std::string scriptName;
    };
}
