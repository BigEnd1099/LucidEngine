#include "network/McpKnowledgeBase.hpp"
#include "core/Engine.hpp"
#include "ecs/Registry.hpp"

namespace engine::network::mcp {

    nlohmann::json McpKnowledgeBase::getComponentSchemas() {
        return {
            {"TransformComponent", {
                {"description", "Pozycja, orientacja i skala encji w przestrzeni swiata"},
                {"fields", {
                    {"position", {{"type", "vec3"}, {"default", {0.0f, 0.0f, 0.0f}}}},
                    {"rotation", {{"type", "vec3"}, {"description", "Euler angles (degrees)"}, {"default", {0.0f, 0.0f, 0.0f}}}},
                    {"scale", {{"type", "vec3"}, {"default", {1.0f, 1.0f, 1.0f}}}},
                    {"orientation", {{"type", "quat"}, {"description", "Quaternion (w, x, y, z)"}}}
                }}
            }},
            {"LocalTransformComponent", {
                {"description", "Pozycja wzgledna wzgledem rodzica w hierarchii sceny"},
                {"fields", {
                    {"position", {{"type", "vec3"}}},
                    {"rotation", {{"type", "vec3"}}},
                    {"scale", {{"type", "vec3"}}},
                    {"orientation", {{"type", "quat"}}}
                }}
            }},
            {"HierarchyComponent", {
                {"description", "Relacja rodzic-dziecko w grafie sceny"},
                {"fields", {
                    {"parent", {{"type", "uint32"}, {"description", "0xFFFFFFFF jezeli brak"}}},
                    {"children", {{"type", "array_uint32"}}}
                }}
            }},
            {"TagComponent", {
                {"description", "Czytelna nazwa / etykieta encji w edytorze"},
                {"fields", {
                    {"name", {{"type", "string"}, {"maxLength", 64}}}
                }}
            }},
            {"MeshComponent", {
                {"description", "Dowiazanie do siatki 3D oraz materialu PBR"},
                {"fields", {
                    {"meshId", {{"type", "uint32"}}},
                    {"materialId", {{"type", "uint32"}}},
                    {"visible", {{"type", "boolean"}, {"default", true}}}
                }}
            }},
            {"RigidBodyComponent", {
                {"description", "Parametry bryly sztywnej w Jolt Physics"},
                {"fields", {
                    {"mass", {{"type", "float"}, {"description", "0.0 = statyczny, >0 = dynamiczny"}}},
                    {"shapeType", {{"type", "int"}, {"description", "0 = Box, 1 = Sphere, 2 = Capsule"}}},
                    {"halfExtents", {{"type", "vec3"}, {"default", {0.5f, 0.5f, 0.5f}}}},
                    {"restitution", {{"type", "float"}, {"default", 0.6f}}},
                    {"friction", {{"type", "float"}, {"default", 0.3f}}}
                }}
            }},
            {"VelocityComponent", {
                {"description", "Predkosc liniowa encji"},
                {"fields", {
                    {"velocity", {{"type", "vec3"}, {"default", {0.0f, 0.0f, 0.0f}}}}
                }}
            }}
        };
    }

    nlohmann::json McpKnowledgeBase::getShaderUniformsSchema() {
        return {
            {"PbrPushConstants", {
                {"stage", "VERTEX | FRAGMENT"},
                {"sizeBytes", 128},
                {"fields", {
                    {"model", "mat4 (offset 0..63)"},
                    {"materialParams", "vec4 (offset 64..79: x=metallic, y=roughness, z=ao, w=alpha)"},
                    {"baseColor", "vec4 (offset 80..95: rgba)"},
                    {"emissive", "vec4 (offset 96..111: rgb, w=intensity)"}
                }}
            }},
            {"CompositePush", {
                {"stage", "FRAGMENT"},
                {"sizeBytes", 32},
                {"fields", {
                    {"params", "vec4 (x=bloomIntensity, y=exposure, z=bloomEnabled, w=frameIndex)"},
                    {"params2", "vec4 (x=useFxaa, y=unused, z=unused, w=unused)"}
                }}
            }},
            {"WorldRadianceCell", {
                {"stage", "COMPUTE std430"},
                {"sizeBytes", 32},
                {"fields", {
                    {"irradiance", "vec4 (offset 0..15: rgb=radiance, a=sampleWeight)"},
                    {"cellKey", "uint (offset 16..19: quantized pos + normal)"},
                    {"packedNormal", "uint (offset 20..23: octahedral 16-bit + flags)"},
                    {"lastSeenFrame", "uint (offset 24..27: TTL)"},
                    {"lockOrFlags", "uint (offset 28..31: padding/atomic flag)"}
                }}
            }},
            {"RayCamera", {
                {"stage", "COMPUTE / RAY_QUERY std140"},
                {"sizeBytes", 96},
                {"fields", {
                    {"inverseViewProjection", "mat4 (offset 0..63)"},
                    {"cameraPosition", "vec4 (offset 64..79)"},
                    {"viewport", "vec4 (offset 80..95: xy=width/height)"}
                }}
            }}
        };
    }

    nlohmann::json McpKnowledgeBase::buildKnowledgeGraph(core::Engine* engine) {
        nlohmann::json summary = {
            {"engineName", "LucidEngine"},
            {"engineVersion", "2026.11-preview"},
            {"architecture", {
                {"language", "C++20"},
                {"ecs", "EnTT 3.13.0"},
                {"rendering", "Vulkan 1.3 Dynamic Rendering + VK_KHR_ray_query"},
                {"physics", "Jolt Physics 5.0"},
                {"scripting", "QuickJS-ng"},
                {"networking", "IXWebSocket / JSON-RPC / SinglePortUnifiedServer"}
            }},
            {"components", getComponentSchemas()},
            {"shaderUniforms", getShaderUniformsSchema()}
        };

        if (engine && engine->getRegistry()) {
            auto& reg = engine->getRegistry()->get();
            auto view = reg.view<ecs::TransformComponent>();
            summary["scene"] = {
                {"entityCount", view.size()},
                {"isPaused", engine->isPaused()},
                {"camera", {
                    {"pos", {engine->getCameraPosition().x, engine->getCameraPosition().y, engine->getCameraPosition().z}},
                    {"yaw", engine->getCameraYaw()},
                    {"pitch", engine->getCameraPitch()},
                    {"fov", engine->getCameraFov()}
                }}
            };
        }

        return summary;
    }

} // namespace engine::network::mcp
