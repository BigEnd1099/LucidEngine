#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <memory>
#include <vector>

namespace engine::physics {
    struct ContactPoint {
        uint32_t body1{0};
        uint32_t body2{0};
        uint32_t entity1{0};
        uint32_t entity2{0};
        float normalX{0.0f};
        float normalY{0.0f};
        float normalZ{0.0f};
    };

    class PhysicsContext {
    public:
        PhysicsContext();
        ~PhysicsContext();

        void init();
        void step(float dt);
        
        // Expose Jolt internals for advanced usage
        JPH::PhysicsSystem& getSystem() { return *m_physicsSystem; }
        JPH::BodyInterface& getBodyInterface() { return m_physicsSystem->GetBodyInterface(); }

        std::vector<ContactPoint> getAndClearContacts();

    private:
        std::unique_ptr<JPH::PhysicsSystem> m_physicsSystem;
        void* m_tempAllocator{nullptr};
        void* m_jobSystem{nullptr};
        void* m_broadPhaseLayerInterface{nullptr};
        void* m_objectVsBroadPhaseLayerFilter{nullptr};
        void* m_objectLayerPairFilter{nullptr};
        void* m_contactListener{nullptr};
    };
}
