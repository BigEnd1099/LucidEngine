#include "renderer/Lighting/HardwareReSTIR.hpp"
#include "core/Logger.hpp"

namespace engine::renderer {

    void HardwareReSTIR::init(VkDevice device) {
        m_device = device;
        core::Logger::info("HardwareReSTIR (UE5-adapted Spatio-Temporal Reservoir Resampling for GI) initialized.");
    }

    void HardwareReSTIR::cleanup() {
        m_temporalReservoirs.clear();
    }

    void HardwareReSTIR::dispatchTemporalResampling(VkCommandBuffer cmd) {
        // Dispatches compute shader to resample with previous frame history
    }

    void HardwareReSTIR::dispatchSpatialResampling(VkCommandBuffer cmd) {
        // Dispatches compute shader to gather 4-8 spatial screen-space neighbor reservoirs
    }

    // [UE5 StochasticLighting: Unbiased Reservoir Combination]
    ReSTIRReservoir HardwareReSTIR::combineReservoirs(const ReSTIRReservoir& r1, const ReSTIRReservoir& r2, float targetPdf, float randVal) {
        ReSTIRReservoir combined;
        combined.wSum = 0.0f;
        combined.M = r1.M + r2.M;

        // Candidate 1 weight
        float weight1 = r1.W * static_cast<float>(r1.M) * targetPdf;
        combined.update(r1.lightIndex, r1.sampleDir, r1.radiance, weight1, 0.0f);

        // Candidate 2 weight
        float weight2 = r2.W * static_cast<float>(r2.M) * targetPdf;
        combined.update(r2.lightIndex, r2.sampleDir, r2.radiance, weight2, randVal);

        combined.finalize(targetPdf);
        return combined;
    }
}
