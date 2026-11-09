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
        bool visible{true};
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
        glm::vec3 emissive{0.0f, 0.0f, 0.0f};
        float emissiveIntensity{0.0f};
    };

    struct LightComponent {
        std::string type{"point"}; // "point", "sun", "spot", "area", "directional"
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float range{20.0f};
        // Spot light angles (degrees)
        float innerConeAngle{25.0f};
        float outerConeAngle{40.0f};
        // Area light dimensions (meters)
        float areaSizeX{2.5f};
        float areaSizeY{2.0f};
    };

    struct AudioComponent {
        std::string soundAssetId;
        float volume{1.0f};
        float pitch{1.0f};
        bool is3D{true};
        bool loop{false};
        bool autoPlay{false};
    };

    struct CharacterMovementComponent {
        float maxWalkSpeed{8.0f};
        float jumpZVelocity{12.0f};
        float gravityScale{1.0f};
        float airControl{0.3f};
    };

    struct CameraBoomComponent {
        float targetArmLength{10.0f};
        float socketOffsetY{3.0f};
        bool cameraLag{true};
        float lagSpeed{10.0f};
        bool isFirstPerson{false};
    };

    struct ParticleEmitterComponent {
        std::string type{"fire"}; // "fire", "sparks", "smoke", "portal", "explosion"
        glm::vec3 color{1.0f, 0.4f, 0.0f};
        uint32_t count{200};
        float speed{3.0f};
        float size{0.4f};
        uint32_t attachedToEntityId{0xFFFFFFFF};
    };

    struct TagComponent {
        std::string name;
    };

    struct ScriptableComponent {
        std::string scriptName;
    };

    // [Zelda TotK & Noita Chemistry & Elemental Reactions]
    enum class ElementType : uint8_t {
        NONE = 0,
        FIRE,
        WATER,
        ELECTRICITY,
        ICE,
        WIND,
        WOOD,
        METAL,
        OIL,
        STONE
    };

    struct ChemistryComponent {
        ElementType primaryElement{ElementType::NONE};
        float temperature{20.0f};       // Celsius
        float wetness{0.0f};           // 0.0 to 1.0
        float electricCharge{0.0f};     // Shock voltage
        float flammability{0.0f};      // 0.0 to 1.0 (wood=0.7, oil=1.0, stone=0.0)
        float conductivity{0.0f};      // 0.0 to 1.0 (metal=1.0, water=0.8)
        bool isBurning{false};
        bool isFrozen{false};
        float burnDuration{0.0f};
    };

    // [Frostbite & Teardown 3D Voronoi Fracture & Destruction]
    struct DestructibleComponent {
        float health{100.0f};
        float maxHealth{100.0f};
        float fractureForceThreshold{50.0f}; // Minimum impulse needed to shatter
        uint32_t shardCount{8};              // Number of debris shards to spawn
        bool isBroken{false};
        std::string materialType{"concrete"}; // "concrete", "wood", "glass", "metal"
        float explosionResistance{0.5f};
    };

    // [Snowdrop & Glacier Dynamic 3D NavMesh & AI Agent]
    struct NavAgentComponent {
        glm::vec3 targetPosition{0.0f, 0.0f, 0.0f};
        std::vector<glm::vec3> waypoints;
        size_t currentWaypointIndex{0};
        float moveSpeed{4.0f};
        float stoppingDistance{0.5f};
        bool hasTarget{false};
        bool isMoving{false};
        bool isStuck{false};
    };

    // [Euphoria & RAGE 2-Bone Inverse Kinematics Foot Placement]
    struct IKFootPlacementComponent {
        float hipHeight{1.0f};
        float legLength{0.9f};
        float footSpacing{0.3f};
        float leftFootTargetY{0.0f};
        float rightFootTargetY{0.0f};
        glm::vec3 leftFootNormal{0.0f, 1.0f, 0.0f};
        glm::vec3 rightFootNormal{0.0f, 1.0f, 0.0f};
        bool isGrounded{true};
        float smoothingFactor{12.0f};
    };

    // [Decima & UE5 World Partition Streaming Cell]
    struct WorldCellComponent {
        int cellX{0};
        int cellY{0};
        int cellZ{0};
        bool isStreaming{false};
        float streamDistance{100.0f};
    };

    // [Frostbite & CryEngine Spatial 3D Audio Source]
    struct AudioSource3DComponent {
        std::string soundAssetId;
        float volume{1.0f};
        float pitch{1.0f};
        float minDistance{2.0f};
        float maxDistance{40.0f};
        float rolloffFactor{1.0f};
        bool isPlaying{false};
        bool loop{false};
        glm::vec3 velocity{0.0f, 0.0f, 0.0f};
    };

    // [UE5 PCG & CryEngine Procedural Foliage]
    struct PCGFoliageComponent {
        uint32_t seed{1337};
        float density{1.0f};
        float minAltitude{0.0f};
        float maxAltitude{50.0f};
        float maxSlopeAngle{45.0f};
        std::string foliageType{"pine_tree"};
    };
}
