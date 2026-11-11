#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>

namespace engine::renderer {

    // Reservoir data for ReSTIR-style weighted candidate reuse.
    struct ReSTIRReservoir {
        float wSum{0.0f};          // Sum of candidate weights
        float W{0.0f};             // Normalized estimator weight
        uint32_t M{0};             // Number of represented candidates
        uint32_t lightIndex{0};    // Selected light or surface emitter ID
        glm::vec3 sampleDir{0.0f}; // Incoming sample direction
        glm::vec3 radiance{0.0f};  // Incoming sample radiance

        bool update(uint32_t candidateLightIdx, const glm::vec3& dir, const glm::vec3& rad, float weight, float randVal) {
            if (M < std::numeric_limits<uint32_t>::max()) ++M;
            if (!std::isfinite(weight) || weight <= 0.0f || !std::isfinite(randVal)) return false;

            const double accumulated = static_cast<double>(wSum) + static_cast<double>(weight);
            if (!std::isfinite(accumulated) || accumulated > std::numeric_limits<float>::max()) return false;

            const float newSum = static_cast<float>(accumulated);
            const float clampedRandom = glm::clamp(randVal, 0.0f, 1.0f);
            const bool selected = (clampedRandom * newSum) < weight;
            wSum = newSum;
            if (selected) {
                lightIndex = candidateLightIdx;
                sampleDir = dir;
                radiance = rad;
            }
            return selected;
        }

        void finalize(float targetPdf) {
            if (M > 0 && std::isfinite(wSum) && wSum > 0.0f &&
                std::isfinite(targetPdf) && targetPdf > 0.0f) {
                W = wSum / (static_cast<float>(M) * targetPdf);
                if (!std::isfinite(W)) W = 0.0f;
            } else {
                W = 0.0f;
            }
        }
    };

    class HardwareReSTIR {
    public:
        void init(VkDevice device);
        void cleanup();
        void dispatchTemporalResampling(VkCommandBuffer cmd);
        void dispatchSpatialResampling(VkCommandBuffer cmd);

        static ReSTIRReservoir combineReservoirs(const ReSTIRReservoir& r1, const ReSTIRReservoir& r2, float targetPdf, float randVal);

    private:
        VkDevice m_device{VK_NULL_HANDLE};
        std::vector<ReSTIRReservoir> m_temporalReservoirs;
    };
}
