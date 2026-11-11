#include "renderer/SpatialAnnotation.hpp"
#include <algorithm>

namespace engine::renderer {

    SpatialAnnotationRegistry& SpatialAnnotationRegistry::get() {
        static SpatialAnnotationRegistry instance;
        return instance;
    }

    uint32_t SpatialAnnotationRegistry::addAnnotation(const SpatialAnnotation& ann) {
        std::lock_guard<std::mutex> lock(m_mutex);
        SpatialAnnotation record = ann;
        record.id = m_nextId++;
        m_annotations.push_back(record);
        return record.id;
    }

    bool SpatialAnnotationRegistry::removeAnnotation(uint32_t id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::remove_if(m_annotations.begin(), m_annotations.end(), [id](const SpatialAnnotation& a) {
            return a.id == id;
        });
        if (it != m_annotations.end()) {
            m_annotations.erase(it, m_annotations.end());
            return true;
        }
        return false;
    }

    void SpatialAnnotationRegistry::clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_annotations.clear();
    }

    std::vector<SpatialAnnotation> SpatialAnnotationRegistry::getActiveAnnotations() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_annotations;
    }

    nlohmann::json SpatialAnnotationRegistry::toJson() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& ann : m_annotations) {
            arr.push_back({
                {"id", ann.id},
                {"worldPos", {ann.worldPos.x, ann.worldPos.y, ann.worldPos.z}},
                {"radius", ann.radius},
                {"label", ann.label},
                {"color", {ann.color.r, ann.color.g, ann.color.b, ann.color.a}},
                {"agentId", ann.agentId}
            });
        }
        return arr;
    }

} // namespace engine::renderer
