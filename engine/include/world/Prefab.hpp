#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <entt/entt.hpp>
#include "ecs/Registry.hpp"

namespace engine::world {

    struct PrefabAsset {
        uint64_t uuid{0};
        std::string name;
        nlohmann::json entityTree;
    };

    class PrefabSystem {
    public:
        // Serialize an entity and all its descendants into hierarchical JSON
        static nlohmann::json serializeHierarchy(ecs::Registry* registry, entt::entity rootEntity);

        // Instantiate a hierarchical JSON prefab into the ECS registry
        static entt::entity instantiateHierarchy(ecs::Registry* registry,
                                                 const nlohmann::json& prefabData,
                                                 entt::entity parent = entt::null);

        // Apply JSON overrides to an instantiated entity hierarchy
        static void applyOverride(ecs::Registry* registry, entt::entity entity, const nlohmann::json& overrides);

        // Save & load prefab files to disk
        static bool saveToFile(const std::string& filepath, const PrefabAsset& asset);
        static bool loadFromFile(const std::string& filepath, PrefabAsset& outAsset);
    };

} // namespace engine::world
