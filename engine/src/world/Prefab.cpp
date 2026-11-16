#include "world/Prefab.hpp"
#include "ecs/Components.hpp"
#include <fstream>
#include <chrono>
#include <random>

namespace engine::world {

    static uint64_t generateRandomUUID() {
        static std::mt19937_64 rng(static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        return rng();
    }

    nlohmann::json PrefabSystem::serializeHierarchy(ecs::Registry* registry, entt::entity rootEntity) {
        if (!registry || !registry->get().valid(rootEntity)) return nlohmann::json::object();

        const auto& reg = registry->get();
        nlohmann::json node = nlohmann::json::object();

        // Tag
        if (reg.all_of<ecs::TagComponent>(rootEntity)) {
            node["name"] = reg.get<ecs::TagComponent>(rootEntity).name;
        }

        // UUID
        if (reg.all_of<ecs::UUIDComponent>(rootEntity)) {
            node["uuid"] = reg.get<ecs::UUIDComponent>(rootEntity).uuid;
        }

        // Local Transform
        if (reg.all_of<ecs::LocalTransformComponent>(rootEntity)) {
            const auto& lt = reg.get<ecs::LocalTransformComponent>(rootEntity);
            node["localTransform"] = {
                {"pos", {lt.position.x, lt.position.y, lt.position.z}},
                {"rot", {lt.rotation.x, lt.rotation.y, lt.rotation.z}},
                {"scale", {lt.scale.x, lt.scale.y, lt.scale.z}}
            };
        }

        // World Transform
        if (reg.all_of<ecs::TransformComponent>(rootEntity)) {
            const auto& t = reg.get<ecs::TransformComponent>(rootEntity);
            node["transform"] = {
                {"pos", {t.position.x, t.position.y, t.position.z}},
                {"rot", {t.rotation.x, t.rotation.y, t.rotation.z}},
                {"scale", {t.scale.x, t.scale.y, t.scale.z}}
            };
        }

        // Mesh
        if (reg.all_of<ecs::MeshComponent>(rootEntity)) {
            const auto& m = reg.get<ecs::MeshComponent>(rootEntity);
            node["mesh"] = {
                {"meshId", m.meshId},
                {"materialId", m.materialId},
                {"albedoIdx", m.albedoIdx},
                {"normalIdx", m.normalIdx},
                {"ormIdx", m.ormIdx},
                {"visible", m.visible}
            };
        }

        // Material
        if (reg.all_of<ecs::MaterialComponent>(rootEntity)) {
            const auto& mat = reg.get<ecs::MaterialComponent>(rootEntity);
            node["material"] = {
                {"baseColor", {mat.baseColor.r, mat.baseColor.g, mat.baseColor.b, mat.baseColor.a}},
                {"metallic", mat.metallic},
                {"roughness", mat.roughness},
                {"emissive", {mat.emissive.r, mat.emissive.g, mat.emissive.b}},
                {"emissiveIntensity", mat.emissiveIntensity},
                {"materialId", mat.materialId},
                {"albedoPath", mat.albedoPath},
                {"normalPath", mat.normalPath},
                {"ormPath", mat.ormPath}
            };
        }

        // Light
        if (reg.all_of<ecs::LightComponent>(rootEntity)) {
            const auto& l = reg.get<ecs::LightComponent>(rootEntity);
            node["light"] = {
                {"type", l.type},
                {"color", {l.color.r, l.color.g, l.color.b}},
                {"intensity", l.intensity},
                {"range", l.range}
            };
        }

        // RigidBody
        if (reg.all_of<ecs::RigidBodyComponent>(rootEntity)) {
            const auto& rb = reg.get<ecs::RigidBodyComponent>(rootEntity);
            node["rigidBody"] = {
                {"mass", rb.mass},
                {"shapeType", rb.shapeType},
                {"halfExtents", {rb.halfExtents.x, rb.halfExtents.y, rb.halfExtents.z}},
                {"friction", rb.friction},
                {"restitution", rb.restitution},
                {"isSensor", rb.isSensor}
            };
        }

        // Scriptable
        if (reg.all_of<ecs::ScriptableComponent>(rootEntity)) {
            node["script"] = reg.get<ecs::ScriptableComponent>(rootEntity).scriptName;
        }

        // Recursive Children
        node["children"] = nlohmann::json::array();
        if (reg.all_of<ecs::HierarchyComponent>(rootEntity)) {
            const auto& children = reg.get<ecs::HierarchyComponent>(rootEntity).children;
            for (entt::entity child : children) {
                if (reg.valid(child)) {
                    node["children"].push_back(serializeHierarchy(registry, child));
                }
            }
        }

        return node;
    }

    entt::entity PrefabSystem::instantiateHierarchy(ecs::Registry* registry,
                                                    const nlohmann::json& prefabData,
                                                    entt::entity parent) {
        if (!registry || !prefabData.is_object()) return entt::null;

        entt::entity entity = registry->createEntity();

        // Tag
        std::string name = prefabData.value("name", "PrefabNode");
        registry->get().emplace<ecs::TagComponent>(entity, name);

        // UUID (generate fresh unique id on instancing)
        registry->get().emplace<ecs::UUIDComponent>(entity, generateRandomUUID());

        // Transform
        ecs::TransformComponent tc{};
        if (prefabData.contains("transform")) {
            const auto& t = prefabData["transform"];
            if (t.contains("pos") && t["pos"].is_array() && t["pos"].size() >= 3)
                tc.position = glm::vec3(t["pos"][0], t["pos"][1], t["pos"][2]);
            if (t.contains("rot") && t["rot"].is_array() && t["rot"].size() >= 3)
                tc.rotation = glm::vec3(t["rot"][0], t["rot"][1], t["rot"][2]);
            if (t.contains("scale") && t["scale"].is_array() && t["scale"].size() >= 3)
                tc.scale = glm::vec3(t["scale"][0], t["scale"][1], t["scale"][2]);
            tc.orientation = glm::quat(glm::radians(tc.rotation));
        }
        registry->get().emplace<ecs::TransformComponent>(entity, tc);

        // Local Transform
        ecs::LocalTransformComponent ltc{};
        ltc.position = tc.position;
        ltc.rotation = tc.rotation;
        ltc.orientation = tc.orientation;
        ltc.scale = tc.scale;
        if (prefabData.contains("localTransform")) {
            const auto& lt = prefabData["localTransform"];
            if (lt.contains("pos") && lt["pos"].is_array() && lt["pos"].size() >= 3)
                ltc.position = glm::vec3(lt["pos"][0], lt["pos"][1], lt["pos"][2]);
            if (lt.contains("rot") && lt["rot"].is_array() && lt["rot"].size() >= 3)
                ltc.rotation = glm::vec3(lt["rot"][0], lt["rot"][1], lt["rot"][2]);
            if (lt.contains("scale") && lt["scale"].is_array() && lt["scale"].size() >= 3)
                ltc.scale = glm::vec3(lt["scale"][0], lt["scale"][1], lt["scale"][2]);
            ltc.orientation = glm::quat(glm::radians(ltc.rotation));
        }
        registry->get().emplace<ecs::LocalTransformComponent>(entity, ltc);

        // Mesh
        if (prefabData.contains("mesh")) {
            const auto& m = prefabData["mesh"];
            ecs::MeshComponent mc{};
            mc.meshId = m.value("meshId", 0u);
            mc.materialId = m.value("materialId", 0u);
            mc.albedoIdx = m.value("albedoIdx", 0u);
            mc.normalIdx = m.value("normalIdx", 0u);
            mc.ormIdx = m.value("ormIdx", 0u);
            mc.visible = m.value("visible", true);
            registry->get().emplace<ecs::MeshComponent>(entity, mc);
        }

        // Material
        if (prefabData.contains("material")) {
            const auto& mat = prefabData["material"];
            ecs::MaterialComponent matComp{};
            if (mat.contains("baseColor") && mat["baseColor"].is_array() && mat["baseColor"].size() >= 4) {
                matComp.baseColor = glm::vec4(mat["baseColor"][0], mat["baseColor"][1], mat["baseColor"][2], mat["baseColor"][3]);
            }
            matComp.metallic = mat.value("metallic", 0.1f);
            matComp.roughness = mat.value("roughness", 0.5f);
            matComp.emissiveIntensity = mat.value("emissiveIntensity", 0.0f);
            matComp.albedoPath = mat.value("albedoPath", "");
            matComp.normalPath = mat.value("normalPath", "");
            matComp.ormPath = mat.value("ormPath", "");
            registry->get().emplace<ecs::MaterialComponent>(entity, matComp);
        }

        // Light
        if (prefabData.contains("light")) {
            const auto& l = prefabData["light"];
            ecs::LightComponent lc{};
            lc.type = l.value("type", "point");
            if (l.contains("color") && l["color"].is_array() && l["color"].size() >= 3) {
                lc.color = glm::vec3(l["color"][0], l["color"][1], l["color"][2]);
            }
            lc.intensity = l.value("intensity", 1.0f);
            lc.range = l.value("range", 20.0f);
            registry->get().emplace<ecs::LightComponent>(entity, lc);
        }

        // RigidBody
        if (prefabData.contains("rigidBody")) {
            const auto& rb = prefabData["rigidBody"];
            ecs::RigidBodyComponent rbc{};
            rbc.mass = rb.value("mass", 1.0f);
            rbc.shapeType = rb.value("shapeType", 0);
            if (rb.contains("halfExtents") && rb["halfExtents"].is_array() && rb["halfExtents"].size() >= 3) {
                rbc.halfExtents = glm::vec3(rb["halfExtents"][0], rb["halfExtents"][1], rb["halfExtents"][2]);
            }
            rbc.friction = rb.value("friction", 0.3f);
            rbc.restitution = rb.value("restitution", 0.6f);
            rbc.isSensor = rb.value("isSensor", false);
            registry->get().emplace<ecs::RigidBodyComponent>(entity, rbc);
        }

        // Script
        if (prefabData.contains("script")) {
            registry->get().emplace<ecs::ScriptableComponent>(entity, prefabData["script"].get<std::string>());
        }

        // Attach to parent if specified
        if (parent != entt::null && registry->get().valid(parent)) {
            registry->setParent(entity, parent);
        }

        // Instantiate children
        if (prefabData.contains("children") && prefabData["children"].is_array()) {
            for (const auto& childData : prefabData["children"]) {
                instantiateHierarchy(registry, childData, entity);
            }
        }

        return entity;
    }

    void PrefabSystem::applyOverride(ecs::Registry* registry, entt::entity entity, const nlohmann::json& overrides) {
        if (!registry || !registry->get().valid(entity) || !overrides.is_object()) return;

        if (overrides.contains("name")) {
            registry->get().get_or_emplace<ecs::TagComponent>(entity).name = overrides["name"].get<std::string>();
        }

        if (overrides.contains("position") && overrides["position"].is_array() && overrides["position"].size() >= 3) {
            auto& lt = registry->get().get_or_emplace<ecs::LocalTransformComponent>(entity);
            lt.position = glm::vec3(overrides["position"][0], overrides["position"][1], overrides["position"][2]);
            registry->markTransformDirty(entity);
        }

        if (overrides.contains("scale") && overrides["scale"].is_array() && overrides["scale"].size() >= 3) {
            auto& lt = registry->get().get_or_emplace<ecs::LocalTransformComponent>(entity);
            lt.scale = glm::vec3(overrides["scale"][0], overrides["scale"][1], overrides["scale"][2]);
            registry->markTransformDirty(entity);
        }
    }

    bool PrefabSystem::saveToFile(const std::string& filepath, const PrefabAsset& asset) {
        nlohmann::json j;
        j["uuid"] = asset.uuid;
        j["name"] = asset.name;
        j["entityTree"] = asset.entityTree;
        std::ofstream file(filepath);
        if (!file.is_open()) return false;
        file << j.dump(4);
        return true;
    }

    bool PrefabSystem::loadFromFile(const std::string& filepath, PrefabAsset& outAsset) {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;
        nlohmann::json j;
        try {
            file >> j;
            outAsset.uuid = j.value("uuid", 0ULL);
            outAsset.name = j.value("name", "");
            outAsset.entityTree = j.value("entityTree", nlohmann::json::object());
            return true;
        } catch (...) {
            return false;
        }
    }

} // namespace engine::world
