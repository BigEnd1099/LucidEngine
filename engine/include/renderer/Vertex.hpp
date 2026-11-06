#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vector>

namespace engine::renderer {
    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texCoord;
        glm::vec4 tangent;
    };

    // The RT buffer-reference shader reads this exact packed layout via
    // VK_KHR_buffer_device_address; keep it in sync with RtVertex in pbr_rt.frag.
    static_assert(sizeof(Vertex) == 48, "Vertex must be tightly packed to 48 bytes for RT buffer references");
    static_assert(offsetof(Vertex, position) == 0, "Unexpected Vertex::position offset");
    static_assert(offsetof(Vertex, normal) == 12, "Unexpected Vertex::normal offset");
    static_assert(offsetof(Vertex, texCoord) == 24, "Unexpected Vertex::texCoord offset");
    static_assert(offsetof(Vertex, tangent) == 32, "Unexpected Vertex::tangent offset");

    struct VertexInputLayout {
        static std::vector<VkVertexInputBindingDescription> getBindingDescription() {
            std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
            bindingDescriptions[0].binding = 0;
            bindingDescriptions[0].stride = sizeof(Vertex);
            bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescriptions;
        }

        static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
            std::vector<VkVertexInputAttributeDescription> attributeDescriptions(4);

            // position
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, position);

            // normal
            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, normal);

            // texCoord
            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

            // tangent
            attributeDescriptions[3].binding = 0;
            attributeDescriptions[3].location = 3;
            attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributeDescriptions[3].offset = offsetof(Vertex, tangent);

            return attributeDescriptions;
        }
    };
}
