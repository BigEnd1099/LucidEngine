#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace engine::renderer {

    // Physical Atmosphere Parameters adapted from Unreal Engine 5.8 (SkyAtmosphereCommon.ush)
    struct AtmosphereParameters {
        float bottomRadiusKm{6360.0f}; // Planet ground radius
        float topRadiusKm{6420.0f};    // Atmosphere outer boundary (60km height)
        
        // Rayleigh Scattering (air molecules)
        glm::vec3 rayleighScattering{5.802e-3f, 13.558e-3f, 33.100e-3f};
        float rayleighScaleHeight{8.0f};

        // Mie Scattering (aerosols / haze)
        glm::vec3 mieScattering{3.996e-3f, 3.996e-3f, 3.996e-3f};
        glm::vec3 mieExtinction{4.400e-3f, 4.400e-3f, 4.400e-3f};
        float mieScaleHeight{1.2f};
        float miePhaseG{0.8f}; // Henyey-Greenstein asymmetry parameter

        // Absorption (Ozone layer)
        glm::vec3 absorptionExtinction{0.650e-3f, 1.881e-3f, 0.085e-3f};
        float absorptionCenterHeight{25.0f};
        float absorptionWidth{15.0f};

        // Sun
        glm::vec3 sunDirection{0.5f, 0.707f, 0.5f};
        glm::vec3 sunIlluminance{1.0f, 0.98f, 0.92f};
    };

    class VolumetricClouds {
    public:
        void init(VkDevice device);
        void cleanup();
        void dispatchCloudRaymarching(VkCommandBuffer cmd);

        // Analytical phase functions from UE5
        static float getRayleighPhase(float cosTheta);
        static float getMiePhaseHG(float cosTheta, float g);
        static glm::vec3 computeSkyLuminance(const glm::vec3& cameraPosKm, const glm::vec3& rayDir, const AtmosphereParameters& params);

    private:
        VkDevice m_device{VK_NULL_HANDLE};
        AtmosphereParameters m_atmosphere;
        VkImage m_weatherMap{VK_NULL_HANDLE};
        VkImage m_shapeNoiseMap{VK_NULL_HANDLE};
        VkImage m_detailNoiseMap{VK_NULL_HANDLE};
    };
}
