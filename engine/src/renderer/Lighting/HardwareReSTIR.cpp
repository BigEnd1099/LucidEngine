#include "renderer/Lighting/HardwareReSTIR.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::renderer {

    void HardwareReSTIR::init(VkDevice device) {
        m_device = device;
        core::Logger::info("HardwareReSTIR reservoir utilities initialized; GPU resampling dispatch is not wired yet.");
    }

    void HardwareReSTIR::cleanup() {
        m_temporalReservoirs.clear();
        m_device = VK_NULL_HANDLE;
    }

    void HardwareReSTIR::dispatchTemporalResampling(VkCommandBuffer cmd) {
        (void)cmd;
        // Intentionally left inactive until reservoir buffers, descriptors and temporal validation are wired.
    }

    void HardwareReSTIR::dispatchSpatialResampling(VkCommandBuffer cmd) {
        (void)cmd;
        // Intentionally left inactive until reservoir buffers, descriptors and spatial validation are wired.
    }

    ReSTIRReservoir HardwareReSTIR::combineReservoirs(
        const ReSTIRReservoir& r1,
        const ReSTIRReservoir& r2,
        float targetPdf,
        float randVal) {
        ReSTIRReservoir combined;
        if (!std::isfinite(targetPdf) || targetPdf <= 0.0f) return combined;

        // update() increments M for each inserted representative. Restore the actual
        // number of source candidates afterwards; pre-seeding M here double-counted
        // the two representatives and biased the final normalization.
        const uint64_t representedCandidates =
            static_cast<uint64_t>(r1.M) + static_cast<uint64_t>(r2.M);

        if (r1.M > 0 && std::isfinite(r1.W) && r1.W > 0.0f) {
            const double weight = static_cast<double>(r1.W) *
                static_cast<double>(r1.M) * static_cast<double>(targetPdf);
            if (std::isfinite(weight) && weight <= std::numeric_limits<float>::max()) {
                combined.update(r1.lightIndex, r1.sampleDir, r1.radiance,
                    static_cast<float>(weight), 0.0f);
            }
        }

        if (r2.M > 0 && std::isfinite(r2.W) && r2.W > 0.0f) {
            const double weight = static_cast<double>(r2.W) *
                static_cast<double>(r2.M) * static_cast<double>(targetPdf);
            if (std::isfinite(weight) && weight <= std::numeric_limits<float>::max()) {
                combined.update(r2.lightIndex, r2.sampleDir, r2.radiance,
                    static_cast<float>(weight), glm::clamp(randVal, 0.0f, 1.0f));
            }
        }

        combined.M = static_cast<uint32_t>(std::min<uint64_t>(
            representedCandidates, std::numeric_limits<uint32_t>::max()));
        combined.finalize(targetPdf);
        return combined;
    }
}
