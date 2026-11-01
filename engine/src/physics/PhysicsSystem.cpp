#include "physics/PhysicsSystem.hpp"
#include "ecs/Components.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <glm/gtc/quaternion.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>

namespace engine::physics {
    namespace Layers {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
    }

    PhysicsSystem::PhysicsSystem(PhysicsContext* context) : m_context(context) {}
    
    void PhysicsSystem::update(ecs::Registry* registry, float dt) {
        if (!m_context || !registry) return;
        auto& bodyInterface = m_context->getBodyInterface();
        auto view = registry->get().view<ecs::TransformComponent, ecs::RigidBodyComponent>();
        
        // 1. Create newly added bodies
        for (auto entity : view) {
            auto& transform = view.get<ecs::TransformComponent>(entity);
            auto& rb = view.get<ecs::RigidBodyComponent>(entity);
            
            JPH::BodyID bodyId(rb.bodyId);
            
            if (bodyId.IsInvalid()) {
                JPH::ShapeRefC shape;
                if (rb.shapeType == 0) {
                    JPH::BoxShapeSettings settings(JPH::Vec3(
                        std::max(0.05f, rb.halfExtents.x),
                        std::max(0.05f, rb.halfExtents.y),
                        std::max(0.05f, rb.halfExtents.z)
                    ), 0.01f); // 1cm convex radius instead of default 5cm so corners don't visually sink into ground
                    shape = settings.Create().Get();
                } else if (rb.shapeType == 1) {
                    JPH::SphereShapeSettings settings(std::max(0.05f, rb.halfExtents.x));
                    shape = settings.Create().Get();
                } else if (rb.shapeType == 2) {
                    float halfH = std::max(0.05f, rb.capsuleHalfHeight > 0.0f ? rb.capsuleHalfHeight : rb.halfExtents.y);
                    float rad = std::max(0.05f, rb.capsuleRadius > 0.0f ? rb.capsuleRadius : rb.halfExtents.x);
                    JPH::CapsuleShapeSettings settings(halfH, rad);
                    shape = settings.Create().Get();
                } else {
                    JPH::BoxShapeSettings settings(JPH::Vec3(
                        std::max(0.05f, rb.halfExtents.x),
                        std::max(0.05f, rb.halfExtents.y),
                        std::max(0.05f, rb.halfExtents.z)
                    ), 0.01f);
                    shape = settings.Create().Get();
                }
                
                JPH::Quat initRot = JPH::Quat::sEulerAngles(JPH::Vec3(
                    glm::radians(transform.rotation.x),
                    glm::radians(transform.rotation.y),
                    glm::radians(transform.rotation.z)
                ));

                JPH::BodyCreationSettings bcs(
                    shape, 
                    JPH::RVec3(transform.position.x, transform.position.y, transform.position.z), 
                    initRot, 
                    rb.mass > 0.0f ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static, 
                    rb.mass > 0.0f ? Layers::MOVING : Layers::NON_MOVING
                );

                // Enable Continuous Collision Detection (CCD) to eliminate corner tunneling on high-speed falls
                if (rb.mass > 0.0f) {
                    bcs.mMotionQuality = JPH::EMotionQuality::LinearCast;
                }

                bcs.mIsSensor = rb.isSensor;

                // Support locking rotation DOFs (e.g. for character controllers or rolling pins)
                if (rb.lockRotationX || rb.lockRotationY || rb.lockRotationZ) {
                    JPH::EAllowedDOFs dofs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
                    if (!rb.lockRotationX) dofs = dofs | JPH::EAllowedDOFs::RotationX;
                    if (!rb.lockRotationY) dofs = dofs | JPH::EAllowedDOFs::RotationY;
                    if (!rb.lockRotationZ) dofs = dofs | JPH::EAllowedDOFs::RotationZ;
                    bcs.mAllowedDOFs = dofs;
                }

                bcs.mRestitution = std::clamp(rb.restitution, 0.0f, 1.0f);
                bcs.mFriction = std::max(0.0f, rb.friction);
                bcs.mUserData = static_cast<uint64_t>(entity);

                if (rb.mass > 0.0f && registry->get().all_of<ecs::VelocityComponent>(entity)) {
                    auto& vel = registry->get().get<ecs::VelocityComponent>(entity);
                    bcs.mLinearVelocity = JPH::Vec3(vel.velocity.x, vel.velocity.y, vel.velocity.z);
                }
                
                JPH::Body* body = bodyInterface.CreateBody(bcs);
                if (body) {
                    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
                    rb.bodyId = body->GetID().GetIndexAndSequenceNumber();
                }
            } else {
                bodyInterface.SetRestitution(bodyId, std::clamp(rb.restitution, 0.0f, 1.0f));
                bodyInterface.SetFriction(bodyId, std::max(0.0f, rb.friction));
            }
        }

        // 2. Step physics simulation!
        m_context->step(dt);

        // 3. Synchronize simulated positions and velocities back to ECS
        for (auto entity : view) {
            auto& transform = view.get<ecs::TransformComponent>(entity);
            auto& rb = view.get<ecs::RigidBodyComponent>(entity);
            
            JPH::BodyID bodyId(rb.bodyId);
            if (!bodyId.IsInvalid() && rb.mass > 0.0f) {
                JPH::RVec3 pos = bodyInterface.GetPosition(bodyId);
                transform.position = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
                
                JPH::Quat rot = bodyInterface.GetRotation(bodyId);
                glm::quat q(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
                transform.orientation = q;
                transform.rotation = glm::degrees(glm::eulerAngles(q));
                
                JPH::Vec3 linVel = bodyInterface.GetLinearVelocity(bodyId);
                if (registry->get().all_of<ecs::VelocityComponent>(entity)) {
                    auto& vel = registry->get().get<ecs::VelocityComponent>(entity);
                    vel.velocity = glm::vec3(linVel.GetX(), linVel.GetY(), linVel.GetZ());
                }
            }
        }
    }
}
