#pragma once
#include "ecs/Registry.hpp"
#include "physics/PhysicsContext.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace engine::anim {

    struct MotionPoseFeature {
        glm::vec3 rootVelocity{0.0f};
        float rootAngularVelocity{0.0f};
        glm::vec3 trajectorySample02s{0.0f};
        glm::vec3 trajectorySample04s{0.0f};
        glm::vec3 leftFootPos{0.0f};
        glm::vec3 rightFootPos{0.0f};
        std::string clipName;
        float timestamp{0.0f};
    };

    class MotionMatching {
    public:
        MotionMatching();

        void update(ecs::Registry& registry, physics::PhysicsContext* physics, float dt);

        // Analytical Two-Bone Inverse Kinematics Solver (UE5 / Euphoria)
        static bool solveTwoBoneIK(
            const glm::vec3& rootPos,
            const glm::vec3& targetPos,
            float upperLength,
            float lowerLength,
            const glm::vec3& poleVector,
            glm::vec3& outJointPos
        );

        // Motion Matching pose scoring
        int searchBestPose(const MotionPoseFeature& query) const;
        void addDatabasePose(const MotionPoseFeature& pose) { m_database.push_back(pose); }

    private:
        std::vector<MotionPoseFeature> m_database;
    };

}
