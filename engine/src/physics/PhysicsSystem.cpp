#include "physics/PhysicsSystem.hpp"
#include "ecs/Components.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>

namespace engine::physics {
    namespace Layers {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
    }

    JPH::ShapeRefC PhysicsSystem::createShape(const ecs::RigidBodyComponent& rb, const glm::vec3& scale) {
        JPH::ShapeRefC shape;
        if (rb.shapeType == 0) {
            float hx = std::max(0.01f, std::abs(rb.halfExtents.x * scale.x));
            float hy = std::max(0.01f, std::abs(rb.halfExtents.y * scale.y));
            float hz = std::max(0.01f, std::abs(rb.halfExtents.z * scale.z));
            JPH::BoxShapeSettings settings(JPH::Vec3(hx, hy, hz), 0.01f);
            shape = settings.Create().Get();
        } else if (rb.shapeType == 1) {
            float baseR = std::max(0.01f, (rb.halfExtents.x > 0.0f ? rb.halfExtents.x : 0.5f));
            float rx = std::max(0.01f, baseR * std::abs(scale.x));
            float ry = std::max(0.01f, baseR * std::abs(scale.y));
            float rz = std::max(0.01f, baseR * std::abs(scale.z));

            // If scale is uniform across axes, use pure analytical SphereShape
            if (std::abs(rx - ry) < 0.005f && std::abs(rx - rz) < 0.005f) {
                JPH::SphereShapeSettings settings(rx);
                shape = settings.Create().Get();
            } else {
                // Non-uniform scaling (stretched or flattened sphere / ellipsoid):
                // Create an exact convex hull representation matching the scaled visual mesh
                JPH::Array<JPH::Vec3> points;
                points.reserve(42);
                const int rings = 6;
                const int sectors = 8;
                for (int r = 0; r <= rings; ++r) {
                    float theta = glm::pi<float>() * static_cast<float>(r) / static_cast<float>(rings);
                    float sinT = std::sin(theta);
                    float cosT = std::cos(theta);
                    if (r == 0) {
                        points.push_back(JPH::Vec3(0.0f, ry, 0.0f));
                        continue;
                    }
                    if (r == rings) {
                        points.push_back(JPH::Vec3(0.0f, -ry, 0.0f));
                        continue;
                    }
                    for (int s = 0; s < sectors; ++s) {
                        float phi = 2.0f * glm::pi<float>() * static_cast<float>(s) / static_cast<float>(sectors);
                        float x = rx * sinT * std::cos(phi);
                        float y = ry * cosT;
                        float z = rz * sinT * std::sin(phi);
                        points.push_back(JPH::Vec3(x, y, z));
                    }
                }
                JPH::ConvexHullShapeSettings settings(points, 0.01f);
                shape = settings.Create().Get();
            }
        } else if (rb.shapeType == 2) {
            float radScale = std::max(0.01f, std::max(std::abs(scale.x), std::abs(scale.z)));
            float hScale = std::max(0.01f, std::abs(scale.y));
            float baseRad = (rb.capsuleRadius > 0.0f ? rb.capsuleRadius : 0.5f);
            float rad = baseRad * radScale;
            float totalHalfH = (rb.capsuleHalfHeight > 0.0f ? (rb.capsuleHalfHeight + baseRad) : 1.0f) * hScale;
            float halfH = std::max(0.0f, totalHalfH - rad);
            JPH::CapsuleShapeSettings settings(halfH, rad);
            shape = settings.Create().Get();
        } else {
            float hx = std::max(0.01f, std::abs(rb.halfExtents.x * scale.x));
            float hy = std::max(0.01f, std::abs(rb.halfExtents.y * scale.y));
            float hz = std::max(0.01f, std::abs(rb.halfExtents.z * scale.z));
            JPH::BoxShapeSettings settings(JPH::Vec3(hx, hy, hz), 0.01f);
            shape = settings.Create().Get();
        }
        return shape;
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
                rb.lastScale = transform.scale;
                JPH::ShapeRefC shape = createShape(rb, transform.scale);
                
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
                if (glm::distance(rb.lastScale, transform.scale) > 0.001f) {
                    rb.lastScale = transform.scale;
                    JPH::ShapeRefC newShape = createShape(rb, transform.scale);
                    if (newShape) {
                        bodyInterface.SetShape(bodyId, newShape, true, JPH::EActivation::Activate);
                    }
                }
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
