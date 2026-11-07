#pragma once
#include "renderer/VulkanRenderer.hpp"
#include <glm/glm.hpp>

namespace engine::core {
    class MeshAsset;
}

namespace engine::renderer {

    extern uint32_t g_pendingRenderWidth;
    extern uint32_t g_pendingRenderHeight;
    extern int g_pendingHdriIndex;

    struct PbrPushConstants {
        glm::mat4 model;
        glm::vec4 normalCol0;      // xyz = col0 macierzy normalnych, w = metallic
        glm::vec4 normalCol1;      // xyz = col1 macierzy normalnych, w = roughness
        glm::vec4 normalCol2;      // xyz = col2 macierzy normalnych, w = emissiveIntensity
        glm::vec4 baseColorFactor; // rgb = baseColor, a = aoOverride
    };
    static_assert(sizeof(PbrPushConstants) == 128, "PbrPushConstants must fit within 128-byte Vulkan guarantee");

    struct DrawItem {
        core::MeshAsset* mesh{nullptr};
        PbrPushConstants pcs{};
    };

}
