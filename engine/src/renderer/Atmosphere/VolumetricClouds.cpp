#include "renderer/Atmosphere/VolumetricClouds.hpp"
#include "core/Logger.hpp"
#include <cmath>
#include <algorithm>

namespace engine::renderer {

    void VolumetricClouds::init(VkDevice device) {
        m_device = device;
        core::Logger::info("VolumetricClouds (UE5-adapted Rayleigh/Mie & Ozone Atmospheric Scattering) initialized.");
    }

    void VolumetricClouds::cleanup() {}

    void VolumetricClouds::dispatchCloudRaymarching(VkCommandBuffer cmd) {
        // Compute shader dispatch for Froxel volumetric clouds (Stubbed command buffer)
    }

    // [UE5 SkyAtmosphereCommon.ush] Rayleigh Phase Function
    float VolumetricClouds::getRayleighPhase(float cosTheta) {
        const float k = 3.0f / (16.0f * 3.14159265f);
        return k * (1.0f + cosTheta * cosTheta);
    }

    // [UE5 SkyAtmosphereCommon.ush] Henyey-Greenstein Mie Phase Function
    float VolumetricClouds::getMiePhaseHG(float cosTheta, float g) {
        const float k = 1.0f / (4.0f * 3.14159265f);
        float g2 = g * g;
        float denom = 1.0f + g2 - 2.0f * g * cosTheta;
        return k * (1.0f - g2) / (denom * std::sqrt(std::max(1e-4f, denom)));
    }

    // [UE5 SkyAtmosphereCommon.ush] Analytical Single Scattering Path Traced Sky Luminance
    glm::vec3 VolumetricClouds::computeSkyLuminance(const glm::vec3& cameraPosKm, const glm::vec3& rayDir, const AtmosphereParameters& params) {
        float cosTheta = glm::dot(rayDir, glm::normalize(params.sunDirection));
        float pRayleigh = getRayleighPhase(cosTheta);
        float pMie = getMiePhaseHG(cosTheta, params.miePhaseG);

        // March 16 samples along view ray
        float rayLengthKm = 50.0f;
        int numSteps = 16;
        float stepSizeKm = rayLengthKm / static_cast<float>(numSteps);

        glm::vec3 opticalDepth{0.0f};
        glm::vec3 inScatteredLuminance{0.0f};

        for (int i = 0; i < numSteps; ++i) {
            float sampleDistKm = (i + 0.5f) * stepSizeKm;
            glm::vec3 samplePos = cameraPosKm + rayDir * sampleDistKm;
            float altitudeKm = std::max(0.0f, glm::length(samplePos) - params.bottomRadiusKm);

            if (altitudeKm > (params.topRadiusKm - params.bottomRadiusKm)) break;

            float densityRayleigh = std::exp(-altitudeKm / params.rayleighScaleHeight);
            float densityMie = std::exp(-altitudeKm / params.mieScaleHeight);

            glm::vec3 stepExtinction = params.rayleighScattering * densityRayleigh + params.mieExtinction * densityMie;
            opticalDepth += stepExtinction * stepSizeKm;

            glm::vec3 transmittance = glm::exp(-opticalDepth);
            glm::vec3 scattering = params.rayleighScattering * densityRayleigh * pRayleigh + params.mieScattering * densityMie * pMie;

            inScatteredLuminance += transmittance * scattering * params.sunIlluminance * stepSizeKm;
        }

        return inScatteredLuminance;
    }
}
