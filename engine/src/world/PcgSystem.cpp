#include "world/PcgSystem.hpp"
#include "ecs/Components.hpp"
#include "core/Logger.hpp"
#include <cmath>
#include <random>
#include <algorithm>

namespace engine::world {

    // Simple deterministic gradient pseudo-noise
    static float hash2D(int x, int y, uint32_t seed) {
        int n = x + y * 57 + static_cast<int>(seed) * 131;
        n = (n << 13) ^ n;
        return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
    }

    static float smoothNoise(float x, float y, uint32_t seed) {
        int ix = static_cast<int>(std::floor(x));
        int iy = static_cast<int>(std::floor(y));
        float fx = x - ix;
        float fy = y - iy;

        // Smooth cubic hermite interpolation
        float sx = fx * fx * (3.0f - 2.0f * fx);
        float sy = fy * fy * (3.0f - 2.0f * fy);

        float n00 = hash2D(ix, iy, seed);
        float n10 = hash2D(ix + 1, iy, seed);
        float n01 = hash2D(ix, iy + 1, seed);
        float n11 = hash2D(ix + 1, iy + 1, seed);

        float nx0 = n00 + sx * (n10 - n00);
        float nx1 = n01 + sx * (n11 - n01);

        return nx0 + sy * (nx1 - nx0);
    }

    PcgSystem::PcgSystem(uint32_t seed) : m_seed(seed) {}

    float PcgSystem::sampleHeight(float x, float z, int octaves, float persistence, float scale) const {
        float total = 0.0f;
        float frequency = scale;
        float amplitude = 1.0f;
        float maxValue = 0.0f;

        for (int i = 0; i < octaves; ++i) {
            total += smoothNoise(x * frequency, z * frequency, m_seed + i * 17) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= 2.0f;
        }

        float normalized = (total / maxValue + 1.0f) * 0.5f; // 0.0 to 1.0
        return normalized * 35.0f; // Scale up to 35m height variation
    }

    BiomeSample PcgSystem::sampleBiome(float x, float z) const {
        BiomeSample sample;
        sample.height = sampleHeight(x, z);

        // Compute slope gradient
        float delta = 0.5f;
        float hL = sampleHeight(x - delta, z);
        float hR = sampleHeight(x + delta, z);
        float hD = sampleHeight(x, z - delta);
        float hU = sampleHeight(x, z + delta);

        glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, 2.0f * delta, hD - hU));
        sample.normal = normal;
        sample.slopeAngle = glm::degrees(std::acos(std::clamp(normal.y, 0.0f, 1.0f)));

        if (sample.height < 3.0f) {
            sample.biome = BiomeType::BEACH;
        } else if (sample.height > 25.0f) {
            sample.biome = BiomeType::SNOW;
        } else if (sample.slopeAngle > 30.0f || sample.height > 18.0f) {
            sample.biome = BiomeType::MOUNTAIN;
        } else {
            sample.biome = BiomeType::FOREST;
        }

        return sample;
    }

    void PcgSystem::generateBiomeTerrain(ecs::Registry* registry, float areaSize, float resolution) {
        if (!registry) return;
        auto& reg = registry->get();

        int steps = static_cast<int>(areaSize / resolution);
        float halfArea = areaSize * 0.5f;

        for (int iz = 0; iz < steps; ++iz) {
            for (int ix = 0; ix < steps; ++ix) {
                float wx = ix * resolution - halfArea;
                float wz = iz * resolution - halfArea;
                auto sample = sampleBiome(wx, wz);

                auto tile = reg.create();

                ecs::TransformComponent t;
                t.position = glm::vec3(wx, sample.height * 0.5f, wz);
                t.scale = glm::vec3(resolution * 0.5f, std::max(0.2f, sample.height * 0.5f), resolution * 0.5f);
                reg.emplace<ecs::TransformComponent>(tile, t);

                ecs::MaterialComponent mat;
                if (sample.biome == BiomeType::BEACH) {
                    mat.baseColor = glm::vec4(0.85f, 0.78f, 0.55f, 1.0f); // Sand
                    mat.roughness = 0.9f;
                } else if (sample.biome == BiomeType::FOREST) {
                    mat.baseColor = glm::vec4(0.20f, 0.50f, 0.15f, 1.0f); // Rich Grass Green
                    mat.roughness = 0.7f;
                } else if (sample.biome == BiomeType::MOUNTAIN) {
                    mat.baseColor = glm::vec4(0.45f, 0.42f, 0.40f, 1.0f); // Rock Grey
                    mat.roughness = 0.85f;
                } else {
                    mat.baseColor = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f); // Pure Snow White
                    mat.roughness = 0.3f;
                }
                reg.emplace<ecs::MaterialComponent>(tile, mat);

                ecs::RigidBodyComponent rb;
                rb.mass = 0.0f; // Static terrain collider
                rb.shapeType = 0;
                rb.halfExtents = t.scale;
                rb.friction = (sample.biome == BiomeType::SNOW) ? 0.05f : 0.6f;
                reg.emplace<ecs::RigidBodyComponent>(tile, rb);

                ecs::TagComponent tag;
                tag.name = "TerrainTile_" + std::to_string(ix) + "_" + std::to_string(iz);
                reg.emplace<ecs::TagComponent>(tile, tag);
            }
        }

        core::Logger::info("PCG: Generated procedural biome terrain grid (" + std::to_string(steps) + "x" + std::to_string(steps) + ").");
    }

    void PcgSystem::scatterFoliage(ecs::Registry* registry, float areaSize, uint32_t count) {
        if (!registry) return;
        auto& reg = registry->get();

        std::mt19937 rng(m_seed + 999);
        std::uniform_real_distribution<float> distCoord(-areaSize * 0.45f, areaSize * 0.45f);
        std::uniform_real_distribution<float> distScale(0.8f, 1.5f);
        std::uniform_real_distribution<float> distRot(0.0f, 360.0f);

        uint32_t spawned = 0;
        for (uint32_t i = 0; i < count * 2 && spawned < count; ++i) {
            float wx = distCoord(rng);
            float wz = distCoord(rng);
            auto sample = sampleBiome(wx, wz);

            // Trees and foliage only spawn in Forest and Beach with low slope
            if (sample.biome == BiomeType::FOREST && sample.slopeAngle < 25.0f) {
                auto foliage = reg.create();

                float scaleMod = distScale(rng);
                ecs::TransformComponent t;
                t.position = glm::vec3(wx, sample.height + scaleMod * 1.5f, wz);
                t.scale = glm::vec3(0.5f * scaleMod, 2.0f * scaleMod, 0.5f * scaleMod);
                t.rotation = glm::vec3(0.0f, distRot(rng), 0.0f);
                reg.emplace<ecs::TransformComponent>(foliage, t);

                ecs::MaterialComponent mat;
                mat.baseColor = glm::vec4(0.12f, 0.38f, 0.10f, 1.0f); // Deep pine foliage
                mat.roughness = 0.8f;
                reg.emplace<ecs::MaterialComponent>(foliage, mat);

                ecs::RigidBodyComponent rb;
                rb.mass = 0.0f; // Static tree trunk collider
                rb.shapeType = 2; // Capsule
                rb.capsuleRadius = 0.5f * scaleMod;
                rb.capsuleHalfHeight = 1.5f * scaleMod;
                reg.emplace<ecs::RigidBodyComponent>(foliage, rb);

                // Add chemistry: trees are flammable wood!
                ecs::ChemistryComponent chem;
                chem.primaryElement = ecs::ElementType::WOOD;
                chem.flammability = 0.8f;
                chem.conductivity = 0.05f;
                reg.emplace<ecs::ChemistryComponent>(foliage, chem);

                // Add destructible: trees can be cut or broken
                ecs::DestructibleComponent dest;
                dest.health = 40.0f;
                dest.materialType = "wood";
                dest.shardCount = 6;
                reg.emplace<ecs::DestructibleComponent>(foliage, dest);

                ecs::TagComponent tag;
                tag.name = "FoliageTree_" + std::to_string(spawned);
                reg.emplace<ecs::TagComponent>(foliage, tag);

                spawned++;
            }
        }

        core::Logger::info("PCG: Scattered " + std::to_string(spawned) + " procedural foliage entities across biomes.");
    }

}
