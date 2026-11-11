#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include "ecs/Registry.hpp"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

namespace engine::physics { class PhysicsContext; }

namespace engine::ecs {

    struct alignas(16) EntityBinaryRecord {
        uint32_t entityId{0};
        bool hasTransform{false};
        TransformComponent transform{};
        bool hasLocalTransform{false};
        LocalTransformComponent localTransform{};
        bool hasHierarchy{false};
        uint32_t hierarchyParent{0xFFFFFFFF};
        bool hasTag{false};
        char tag[64]{0};
        bool hasMesh{false};
        MeshComponent mesh{};
        bool hasRigidBody{false};
        RigidBodyComponent rigidBody{};
        bool hasVelocity{false};
        VelocityComponent velocity{};
    };

    class SceneSnapshot {
    public:
        static std::vector<EntityBinaryRecord> capture(Registry& registry);
        static void restore(Registry& registry, physics::PhysicsContext* physicsContext, const std::vector<EntityBinaryRecord>& snapshot);
    };

} // namespace engine::ecs

#ifdef _MSC_VER
#pragma warning(pop)
#endif
