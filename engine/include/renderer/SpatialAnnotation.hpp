#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>

namespace engine::renderer {

    struct SpatialAnnotation {
        uint32_t id{0};
        glm::vec3 worldPos{0.0f};
        float radius{0.5f};
        std::string label;
        glm::vec4 color{1.0f, 0.8f, 0.2f, 1.0f};
        uint32_t agentId{0};
        float lifetimeSeconds{0.0f}; // 0 = permanent do odwolania
    };

    class SpatialAnnotationRegistry {
    public:
        static SpatialAnnotationRegistry& get();

        uint32_t addAnnotation(const SpatialAnnotation& ann);
        bool removeAnnotation(uint32_t id);
        void clear();
        std::vector<SpatialAnnotation> getActiveAnnotations() const;
        nlohmann::json toJson() const;

    private:
        SpatialAnnotationRegistry() = default;
        mutable std::mutex m_mutex;
        uint32_t m_nextId{1};
        std::vector<SpatialAnnotation> m_annotations;
    };

} // namespace engine::renderer
