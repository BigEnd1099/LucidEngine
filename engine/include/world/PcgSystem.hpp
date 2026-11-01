#pragma once
#include "ecs/Registry.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace engine::world {

    enum class BiomeType {
        WATER,
        BEACH,
        FOREST,
        MOUNTAIN,
        SNOW
    };

    struct BiomeSample {
        float height{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        float slopeAngle{0.0f}; // Degrees
        BiomeType biome{BiomeType::FOREST};
    };

    class PcgSystem {
    public:
        PcgSystem(uint32_t seed = 1337);

        float sampleHeight(float x, float z, int octaves = 4, float persistence = 0.5f, float scale = 0.02f) const;
        BiomeSample sampleBiome(float x, float z) const;

        // Generates procedural terrain entities and scatters foliage
        void generateBiomeTerrain(ecs::Registry* registry, float areaSize = 120.0f, float resolution = 4.0f);
        void scatterFoliage(ecs::Registry* registry, float areaSize = 120.0f, uint32_t count = 60);

    private:
        float perlinNoise2D(float x, float y) const;
        uint32_t m_seed{1337};
    };

}
