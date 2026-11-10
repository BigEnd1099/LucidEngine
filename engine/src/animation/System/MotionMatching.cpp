#include "animation/System/MotionMatching.hpp"
#include "ecs/Components.hpp"
#include "core/Logger.hpp"
#include <cmath>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace engine::anim {

    MotionMatching::MotionMatching() {
        // Pre-populate standard motion matching database with locomotion clips
        MotionPoseFeature idlePose;
        idlePose.clipName = "Idle";
        m_database.push_back(idlePose);

        MotionPoseFeature walkFwd;
        walkFwd.clipName = "Walk_Forward";
        walkFwd.rootVelocity = glm::vec3(0.0f, 0.0f, 4.0f);
        walkFwd.trajectorySample02s = glm::vec3(0.0f, 0.0f, 0.8f);
        walkFwd.trajectorySample04s = glm::vec3(0.0f, 0.0f, 1.6f);
        m_database.push_back(walkFwd);

        MotionPoseFeature runFwd;
        runFwd.clipName = "Run_Forward";
        runFwd.rootVelocity = glm::vec3(0.0f, 0.0f, 8.0f);
        runFwd.trajectorySample02s = glm::vec3(0.0f, 0.0f, 1.6f);
        runFwd.trajectorySample04s = glm::vec3(0.0f, 0.0f, 3.2f);
        m_database.push_back(runFwd);
    }

    bool MotionMatching::solveTwoBoneIK(
        const glm::vec3& rootPos,
        const glm::vec3& targetPos,
        float upperLength,
        float lowerLength,
        const glm::vec3& poleVector,
        glm::vec3& outJointPos
    ) {
        glm::vec3 toTarget = targetPos - rootPos;
        float dist = glm::length(toTarget);
        if (dist < 0.0001f) {
            toTarget = glm::vec3(0.0f, -0.0001f, 0.0f);
            dist = 0.0001f;
        }

        // Clamp distance to avoid triangle inequality violations
        float maxReach = upperLength + lowerLength - 0.001f;
        float minReach = std::abs(upperLength - lowerLength) + 0.001f;
        dist = std::clamp(dist, minReach, maxReach);

        glm::vec3 targetDir = glm::normalize(toTarget);

        // Law of Cosines: a^2 + c^2 - 2ac*cos(alpha) = b^2 => cos(alpha) = (a^2 + c^2 - b^2) / (2ac)
        float a = upperLength;
        float b = lowerLength;
        float c = dist;

        float cosAlpha = (a * a + c * c - b * b) / (2.0f * a * c);
        cosAlpha = std::clamp(cosAlpha, -1.0f, 1.0f);
        float alpha = std::acos(cosAlpha);

        // Orthogonalize pole vector against target direction to determine bend plane
        glm::vec3 bendAxis = glm::cross(targetDir, poleVector);
        if (glm::length(bendAxis) < 0.001f) {
            bendAxis = glm::cross(targetDir, glm::vec3(0.0f, 0.0f, 1.0f));
            if (glm::length(bendAxis) < 0.001f) {
                bendAxis = glm::cross(targetDir, glm::vec3(1.0f, 0.0f, 0.0f));
            }
        }
        bendAxis = glm::normalize(bendAxis);

        glm::vec3 bendNormal = glm::normalize(glm::cross(bendAxis, targetDir));

        // Joint position is root + vector along targetDir rotated by alpha around bendAxis
        float cosA = std::cos(alpha);
        float sinA = std::sin(alpha);

        outJointPos = rootPos + (targetDir * cosA + bendNormal * sinA) * a;
        return true;
    }

    int MotionMatching::searchBestPose(const MotionPoseFeature& query) const {
        if (m_database.empty()) return -1;
        float bestCost = 1e9f;
        int bestIdx = 0;

        for (size_t i = 0; i < m_database.size(); ++i) {
            const auto& candidate = m_database[i];
            float velDiff = glm::distance(query.rootVelocity, candidate.rootVelocity);
            float trajDiff = glm::distance(query.trajectorySample04s, candidate.trajectorySample04s);

            float totalCost = (velDiff * 2.0f) + (trajDiff * 1.5f);
            if (totalCost < bestCost) {
                bestCost = totalCost;
                bestIdx = static_cast<int>(i);
            }
        }
        return bestIdx;
    }

    void MotionMatching::update(ecs::Registry& registry, physics::PhysicsContext* physics, float dt) {
        auto& reg = registry.get();
        auto view = reg.view<ecs::IKFootPlacementComponent, ecs::TransformComponent>();

        for (auto entity : view) {
            auto& ik = view.get<ecs::IKFootPlacementComponent>(entity);
            const auto& t = view.get<ecs::TransformComponent>(entity);

            // Compute ideal left and right foot targets in world space
            glm::vec3 leftHip = t.position + glm::vec3(-ik.footSpacing, 0.0f, 0.0f);
            glm::vec3 rightHip = t.position + glm::vec3(ik.footSpacing, 0.0f, 0.0f);

            // Sample terrain height (ground elevation at y = 0.0f baseline plus any floor)
            float groundY = 0.0f;
            float leftGround = groundY;
            float rightGround = groundY;

            // Smooth foot target adjustment towards sampled terrain
            float alpha = std::clamp(dt * ik.smoothingFactor, 0.0f, 1.0f);
            ik.leftFootTargetY += (leftGround - ik.leftFootTargetY) * alpha;
            ik.rightFootTargetY += (rightGround - ik.rightFootTargetY) * alpha;

            // Solve Two-Bone IK for left and right legs
            glm::vec3 leftKneePos, rightKneePos;
            glm::vec3 leftFootWorld(leftHip.x, ik.leftFootTargetY, leftHip.z);
            glm::vec3 rightFootWorld(rightHip.x, ik.rightFootTargetY, rightHip.z);

            solveTwoBoneIK(leftHip, leftFootWorld, ik.legLength * 0.5f, ik.legLength * 0.5f, glm::vec3(0.0f, 0.0f, 1.0f), leftKneePos);
            solveTwoBoneIK(rightHip, rightFootWorld, ik.legLength * 0.5f, ik.legLength * 0.5f, glm::vec3(0.0f, 0.0f, 1.0f), rightKneePos);
        }
    }

}
