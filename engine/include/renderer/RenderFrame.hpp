#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>

namespace engine::renderer {

    struct RenderInstance {
        glm::mat4 transform{1.0f};
        uint32_t meshId{0};
        uint32_t materialId{0};
        uint32_t albedoIdx{0};
        uint32_t normalIdx{0};
        uint32_t ormIdx{0};
        uint32_t entityId{0};
        bool visible{true};
        bool castShadow{true};
        bool rtEnabled{true};
    };

    struct RenderLight {
        glm::vec3 position{0.0f};
        glm::vec3 color{1.0f};
        float intensity{1.0f};
        float range{20.0f};
        std::string type{"point"};
        float innerConeAngle{25.0f};
        float outerConeAngle{40.0f};
    };

    struct RenderCamera {
        glm::vec3 position{0.0f};
        glm::mat4 view{1.0f};
        glm::mat4 proj{1.0f};
        float fov{45.0f};
        float nearZ{0.1f};
        float farZ{1000.0f};
    };

    struct RenderFrame {
        uint64_t frameIndex{0};
        float deltaTime{0.016f};
        RenderCamera camera;
        std::vector<RenderInstance> instances;
        std::vector<RenderLight> lights;
    };

} // namespace engine::renderer
