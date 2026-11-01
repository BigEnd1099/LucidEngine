#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace engine::renderer {

    // [ReSTIR GI Reservoir adapted from Unreal Engine 5.8 (StochasticLighting)]
    struct ReSTIRReservoir {
        float wSum{0.0f};          // Total accumulated weight
        float W{0.0f};             // Final unbiased contribution weight
        uint32_t M{0};             // Total candidate count
        uint32_t lightIndex{0};    // Selected light or surface emitter ID
        glm::vec3 sampleDir{0.0f}; // Sample ray direction
        glm::vec3 radiance{0.0f};  // Incoming sample radiance

        bool update(uint32_t candidateLightIdx, const glm::vec3& dir, const glm::vec3& rad, float weight, float randVal) {
            wSum += weight;
            M += 1;
            if (randVal * wSum <= weight) {
                lightIndex = candidateLightIdx;
                sampleDir = dir;
                radiance = rad;
                return true;
            }
            return false;
        }

        void finalize(float targetPdf) {
            if (M > 0 && targetPdf > 0.0f) {
                W = (wSum / (static_cast<float>(M) * targetPdf));
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

        // Algorithm methods adapted from UE5 ReSTIR GI
        static ReSTIRReservoir combineReservoirs(const ReSTIRReservoir& r1, const ReSTIRReservoir& r2, float targetPdf, float randVal);

    private:
        VkDevice m_device{VK_NULL_HANDLE};
        std::vector<ReSTIRReservoir> m_temporalReservoirs;
    };
}
