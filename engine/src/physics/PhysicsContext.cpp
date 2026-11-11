#include "physics/PhysicsContext.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <thread>
#include <mutex>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/Body.h>

namespace engine::physics {

    namespace Layers {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
        static constexpr JPH::uint NUM_LAYERS = 2;
    }

    class EngineContactListener final : public JPH::ContactListener {
    public:
        virtual void OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &, JPH::ContactSettings &ioSettings) override {
            // Use maximum restitution so bouncy objects bounce properly on any surface
            ioSettings.mCombinedRestitution = std::max(inBody1.GetRestitution(), inBody2.GetRestitution());
            ioSettings.mCombinedFriction = std::sqrt(std::max(0.0f, inBody1.GetFriction()) * std::max(0.0f, inBody2.GetFriction()));

            std::lock_guard<std::mutex> lock(m_mutex);
            ContactPoint cp;
            cp.body1 = inBody1.GetID().GetIndexAndSequenceNumber();
            cp.body2 = inBody2.GetID().GetIndexAndSequenceNumber();
            cp.entity1 = static_cast<uint32_t>(inBody1.GetUserData());
            cp.entity2 = static_cast<uint32_t>(inBody2.GetUserData());
            auto norm = inManifold.mWorldSpaceNormal;
            cp.normalX = norm.GetX();
            cp.normalY = norm.GetY();
            cp.normalZ = norm.GetZ();
            m_contacts.push_back(cp);
        }

        virtual void OnContactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings) override {
            ioSettings.mCombinedRestitution = std::max(inBody1.GetRestitution(), inBody2.GetRestitution());
            ioSettings.mCombinedFriction = std::sqrt(std::max(0.0f, inBody1.GetFriction()) * std::max(0.0f, inBody2.GetFriction()));
        }

        std::vector<ContactPoint> getAndClearContacts() {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::vector<ContactPoint> res = std::move(m_contacts);
            m_contacts.clear();
            return res;
        }

    private:
        std::mutex m_mutex;
        std::vector<ContactPoint> m_contacts;
    };

    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
    public:
        BPLayerInterfaceImpl() {
            mObjectToBroadPhase[Layers::NON_MOVING] = JPH::BroadPhaseLayer(0);
            mObjectToBroadPhase[Layers::MOVING] = JPH::BroadPhaseLayer(1);
        }
        virtual JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
        virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override { return mObjectToBroadPhase[inLayer]; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
            return inLayer.GetValue() == 0 ? "NON_MOVING" : "MOVING";
        }
#endif
    private:
        JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
    };

    class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
            if (inLayer1 == Layers::NON_MOVING) return inLayer2 == JPH::BroadPhaseLayer(1);
            return true;
        }
    };

    class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
            if (inObject1 == Layers::NON_MOVING && inObject2 == Layers::NON_MOVING) return false;
            return true;
        }
    };

    PhysicsContext::PhysicsContext() {}

    PhysicsContext::~PhysicsContext() {
        if (m_tempAllocator) {
            delete static_cast<JPH::TempAllocatorImpl*>(m_tempAllocator);
        }
        if (m_jobSystem) {
            delete static_cast<JPH::JobSystemThreadPool*>(m_jobSystem);
        }
        if (m_broadPhaseLayerInterface) delete static_cast<BPLayerInterfaceImpl*>(m_broadPhaseLayerInterface);
        if (m_objectVsBroadPhaseLayerFilter) delete static_cast<ObjectVsBroadPhaseLayerFilterImpl*>(m_objectVsBroadPhaseLayerFilter);
        if (m_objectLayerPairFilter) delete static_cast<ObjectLayerPairFilterImpl*>(m_objectLayerPairFilter);
        if (m_contactListener) delete static_cast<EngineContactListener*>(m_contactListener);
        
        JPH::UnregisterTypes();
        if (JPH::Factory::sInstance) {
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }

    void PhysicsContext::init() {
        JPH::RegisterDefaultAllocator();
        
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        m_tempAllocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
        int numThreads = std::max<int>(1, std::thread::hardware_concurrency() - 1);
        m_jobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numThreads);
        
        m_physicsSystem = std::make_unique<JPH::PhysicsSystem>();
        
        m_broadPhaseLayerInterface = new BPLayerInterfaceImpl();
        m_objectVsBroadPhaseLayerFilter = new ObjectVsBroadPhaseLayerFilterImpl();
        m_objectLayerPairFilter = new ObjectLayerPairFilterImpl();

        m_physicsSystem->Init(1024, 0, 1024, 1024, 
            *static_cast<BPLayerInterfaceImpl*>(m_broadPhaseLayerInterface), 
            *static_cast<ObjectVsBroadPhaseLayerFilterImpl*>(m_objectVsBroadPhaseLayerFilter), 
            *static_cast<ObjectLayerPairFilterImpl*>(m_objectLayerPairFilter));

        auto* cl = new EngineContactListener();
        m_contactListener = cl;
        m_physicsSystem->SetContactListener(cl);

        core::Logger::info("Jolt Physics initialized successfully with BroadPhase Layers and ContactListener.");
    }

    void PhysicsContext::step(float dt) {
        if (!m_physicsSystem) return;
        m_physicsSystem->Update(dt, 1, static_cast<JPH::TempAllocatorImpl*>(m_tempAllocator), static_cast<JPH::JobSystemThreadPool*>(m_jobSystem));
    }

    std::vector<ContactPoint> PhysicsContext::getAndClearContacts() {
        if (m_contactListener) {
            return static_cast<EngineContactListener*>(m_contactListener)->getAndClearContacts();
        }
        return {};
    }
}
