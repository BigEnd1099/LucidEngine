#include "renderer/VulkanRenderer.hpp"
#include "renderer/VulkanRendererInternal.hpp"
#include "core/Logger.hpp"
#include "renderer/Vertex.hpp"
#include "core/AssetManager.hpp"
#include "core/Engine.hpp"
#include "core/Profiler.hpp"
#include "ecs/Components.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <stdexcept>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <iostream>

#ifdef USE_GLFW
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#endif

namespace engine::renderer {
    static const std::string kEmptyTag;

    void VulkanRenderer::render(ecs::Registry& registry, core::AssetManager* assetManager, glm::mat4 viewMatrix, core::Engine* engine) {
        core::Profiler::ScopedSample renderCpuSample("Vulkan Render Thread");
        if (g_pendingRenderWidth > 0 && g_pendingRenderHeight > 0) {
            setRenderResolution(g_pendingRenderWidth, g_pendingRenderHeight);
            g_pendingRenderWidth = 0;
            g_pendingRenderHeight = 0;
        }
        if (g_pendingHdriIndex >= 0) {
            vkDeviceWaitIdle(m_context->getDevice());
            loadEnvironmentHdri(g_pendingHdriIndex);
            m_selectedHdriIndex = g_pendingHdriIndex;
            g_pendingHdriIndex = -1;
        }

#ifdef USE_GLFW
        uint32_t imageIndex = 0;
        if (!m_context->isHeadless()) {
            {
                core::Profiler::ScopedSample waitFenceSample("Vulkan Fence Wait");
                vkWaitForFences(m_context->getDevice(), 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
            }
            VkResult result = VK_SUCCESS;
            {
                core::Profiler::ScopedSample acquireSample("Vulkan Acquire Image");
                result = vkAcquireNextImageKHR(m_context->getDevice(), m_context->getSwapchain(),
                                               UINT64_MAX, m_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
            }
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
                m_context->recreateSwapchain();
                return;
            } else if (result != VK_SUCCESS) {
                return;
            }
            vkResetFences(m_context->getDevice(), 1, &m_inFlightFence);
        }
#endif

        if (m_context->isHeadless()) {
            core::Profiler::ScopedSample waitFenceSample("Vulkan Fence Wait");
            vkWaitForFences(m_context->getDevice(), 1, &m_headlessInFlightFence, VK_TRUE, UINT64_MAX);
        }

        // The frame fence guarantees the previous timestamp set
        // is available. Never stall here: consume it only when Vulkan reports ready.
        if (m_timestampQueryPool && m_timestampQueryPending) {
            uint64_t timestamps[11]{};
            const VkResult queryResult = vkGetQueryPoolResults(
                m_context->getDevice(), m_timestampQueryPool, 0, 11,
                sizeof(timestamps), timestamps, sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT);
            if (queryResult == VK_SUCCESS) {
                const uint32_t validBits = m_context->getTimestampValidBits();
                const uint64_t mask = validBits >= 64
                    ? UINT64_MAX
                    : ((uint64_t{1} << validBits) - 1u);
                const double timestampPeriodNs =
                    static_cast<double>(m_context->getPhysicalDeviceProperties().limits.timestampPeriod);
                auto elapsedMs = [&](uint32_t begin, uint32_t end) {
                    const uint64_t ticks = (timestamps[end] - timestamps[begin]) & mask;
                    return static_cast<double>(ticks) * timestampPeriodNs / 1000000.0;
                };

                core::Profiler::get().recordGpuTime(static_cast<float>(elapsedMs(0, 8)));
                core::Profiler::get().recordMetric("gpuScenePrepMs", elapsedMs(0, 1));
                core::Profiler::get().recordMetric("gpuShadowMs", elapsedMs(1, 2));
                core::Profiler::get().recordMetric("gpuDepthMs", elapsedMs(2, 3));
                core::Profiler::get().recordMetric("gpuGBufferMs", elapsedMs(3, 4));
                core::Profiler::get().recordMetric("gpuAoRtMs", elapsedMs(4, 5));
                core::Profiler::get().recordMetric("gpuScreenSpaceAoGiMs", m_screenSpaceAoGiPassActive ? elapsedMs(4, 9) : 0.0);
                core::Profiler::get().recordMetric("gpuScreenSpaceBlurMs", m_screenSpaceAoGiPassActive ? elapsedMs(9, 10) : 0.0);
                core::Profiler::get().recordMetric("gpuHdrMs", elapsedMs(5, 6));
                core::Profiler::get().recordMetric("gpuPostMs", elapsedMs(6, 7));
                core::Profiler::get().recordMetric("gpuUiMs", elapsedMs(7, 8));
                m_timestampQueryPending = false;
            }
        }

        // The previous frame has completed (window or headless fence). Scratch buffers
        // from its BLAS/TLAS builds can now be released safely.
        m_accelerationStructure.releaseCompletedScratch();
        ++m_deformationFrameCounter;
        if ((m_deformationFrameCounter % 600u) == 0u) {
            for (auto it = m_deformedVertexCache.begin(); it != m_deformedVertexCache.end();) {
                const bool entityGone = !registry.get().valid(it->first);
                const bool cacheStale = m_deformationFrameCounter - it->second.lastSeenFrame > 600u;
                if (entityGone || cacheStale) {
                    if (it->second.buffer)
                        vmaDestroyBuffer(m_context->getAllocator(), it->second.buffer, it->second.allocation);
                    it = m_deformedVertexCache.erase(it);
                } else {
                    ++it;
                }
            }
        }
        updateRayTracingTextureDescriptors(assetManager);

        vkResetCommandBuffer(m_commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
        if (m_timestampQueryPool) {
            vkCmdResetQueryPool(m_commandBuffer, m_timestampQueryPool, 0, 11);
            vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_timestampQueryPool, 0);
        }

        // =========================================================================
        // PRZYGOTOWANIE KAMERY, SĹOĹCA, 4 KASKAD CSM ORAZ LISTY OBIEKTĂ“W SCENY
        // =========================================================================
        glm::vec3 sunDir = glm::normalize(glm::vec3(0.45f, 0.85f, 0.30f));
        glm::vec3 sunColor(1.0f, 0.98f, 0.92f);
        float sunIntensity = 2.8f;
        glm::vec3 ambientColor(0.20f, 0.24f, 0.34f);
        float ambientIntensity = 0.85f;
        bool sunEnabled = true;
        bool sunDirectionFromPosition = false;

        if (engine) {
            const auto& env = engine->getEnvironmentSettings();
            sunDir = env.sunDirection;
            sunColor = env.sunColor;
            sunIntensity = env.sunIntensity;
            ambientColor = env.ambientColor;
            ambientIntensity = env.ambientIntensity;
            sunEnabled = env.sunEnabled;
            sunDirectionFromPosition = env.sunDirectionFromPosition;
        }

        const glm::vec3 camPos = engine ? engine->getCameraPosition() : glm::vec3(glm::inverse(viewMatrix)[3]);

        glm::vec3 sunPos(0.0f, 18.0f, 0.0f);
        auto dirLightView = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
        bool hasDirLight = false;
        for (auto dlEntity : dirLightView) {
            const auto& lc = dirLightView.get<ecs::LightComponent>(dlEntity);
            if (lc.type == "directional" || lc.type == "sun") {
                hasDirLight = true;
                const auto& lt = dirLightView.get<ecs::TransformComponent>(dlEntity);
                sunPos = lt.position;
                if (sunDirectionFromPosition) {
                    sunDir = glm::length(sunPos) > 0.001f ? glm::normalize(sunPos) : glm::vec3(0.0f, 1.0f, 0.0f);
                } else {
                    const float pitch = glm::radians(lt.rotation.x);
                    const float yaw   = glm::radians(lt.rotation.y);
                    glm::vec3 sunForward = glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
                    sunDir = glm::normalize(-sunForward);
                }
                sunColor = lc.color;
                sunIntensity = lc.intensity;
                break;
            }
        }

        // Benchmark lighting is controlled by the harness; do not inject the editor fallback sun.
        if (!hasDirLight && engine && !m_standaloneGameMode && !m_benchmarkMode) {
            auto sunEntity = registry.createEntity();
            registry.get().emplace<ecs::TagComponent>(sunEntity, ecs::TagComponent{"DirectionalLight (Sun)"});
            registry.get().emplace<ecs::TransformComponent>(sunEntity, glm::vec3(0.0f, 18.0f, 0.0f), glm::vec3(-45.0f, -40.0f, 0.0f), glm::vec3(1.0f));
            ecs::LightComponent sl;
            sl.type = "directional";
            sl.color = glm::vec3(1.0f, 0.98f, 0.92f);
            sl.intensity = 3.5f;
            sl.range = 0.0f;
            registry.get().emplace<ecs::LightComponent>(sunEntity, sl);
            sunPos = glm::vec3(0.0f, 18.0f, 0.0f);
            if (sunDirectionFromPosition) {
                sunDir = glm::length(sunPos) > 0.001f ? glm::normalize(sunPos) : glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                const float pitch = glm::radians(-45.0f);
                const float yaw   = glm::radians(-40.0f);
                sunDir = glm::normalize(-glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
            }
            sunColor = sl.color;
            sunIntensity = sl.intensity;
        }

        if (!sunEnabled) {
            sunIntensity = 0.0f;
        } else {
            float horizonMetric = sunDirectionFromPosition ? (sunPos.y / std::max(1.0f, glm::length(sunPos))) : sunDir.y;
            if (horizonMetric <= 0.0f) {
                sunIntensity = 0.0f;
            } else if (horizonMetric < 0.08f) {
                sunIntensity *= (horizonMetric / 0.08f); // Plynne sciemnianie zmierzchowe / switu
            }
        }
#ifdef USE_GLFW
        const float sceneAspect = (m_standaloneGameMode || m_viewportAspectRatio <= 0.1f)
            ? (static_cast<float>(m_width) / static_cast<float>(std::max(1u, m_height)))
            : m_viewportAspectRatio;
#else
        const float sceneAspect = static_cast<float>(m_width) / static_cast<float>(std::max(1u, m_height));
#endif
        float sceneFov = engine ? engine->getCameraFov() : 45.0f;
        if (sceneFov < 10.0f || sceneFov > 140.0f) sceneFov = 45.0f;

        glm::mat4 projection = glm::perspective(glm::radians(sceneFov), sceneAspect, 0.1f, 1000.0f);
        projection[1][1] *= -1.0f;
        const glm::mat4 vp = projection * viewMatrix;

        glm::vec3 camFront(0.0f, 0.0f, -1.0f);
        if (engine) {
            const float yRad = glm::radians(engine->getCameraYaw());
            const float pRad = glm::radians(engine->getCameraPitch());
            camFront = glm::normalize(glm::vec3(std::cos(yRad) * std::cos(pRad), std::sin(pRad), std::sin(yRad) * std::cos(pRad)));
        }
        const glm::vec3 camRight = glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 camUp    = glm::normalize(glm::cross(camRight, camFront));

        const float cSplits[SHADOW_CASCADE_COUNT] = {
            std::min(m_shadowDistance * 0.10f, 8.0f),
            std::min(m_shadowDistance * 0.28f, 22.0f),
            std::min(m_shadowDistance * 0.60f, 54.0f),
            m_shadowDistance
        };
        float cRadii[SHADOW_CASCADE_COUNT]{};
        glm::mat4 cascadeLightSpace[SHADOW_CASCADE_COUNT]{};
        const float fovRad = glm::radians(sceneFov);

        for (uint32_t c = 0; c < SHADOW_CASCADE_COUNT; ++c) {
            const float nDist = (c == 0) ? 0.1f : (cSplits[c - 1] * 0.85f);
            const float fDist = cSplits[c];
            const float hFar  = std::tan(fovRad * 0.5f) * fDist;
            const float wFar  = hFar * sceneAspect;

            const glm::vec3 nCenter = camPos + camFront * nDist;
            const glm::vec3 fCenter = camPos + camFront * fDist;
            const glm::vec3 fMid    = (nCenter + fCenter) * 0.5f;
            const glm::vec3 farTopRight = fCenter + camRight * wFar + camUp * hFar;
            const float radius = std::ceil(glm::length(farTopRight - fMid) * 16.0f) / 16.0f;
            cRadii[c] = radius;

            // Stabilizacja tekselowa eliminujÄ…ca drĹĽenie krawÄ™dzi cieni podczas ruchu kamery
            const glm::vec3 upRef = (std::abs(sunDir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            const float texelsPerUnit = static_cast<float>(m_shadowWidth) / (radius * 2.0f);
            const glm::mat4 tempLook = glm::lookAt(sunDir * 100.0f, glm::vec3(0.0f), upRef);
            glm::vec3 lightMid = glm::vec3(tempLook * glm::vec4(fMid, 1.0f));
            lightMid.x = std::floor(lightMid.x * texelsPerUnit) / texelsPerUnit;
            lightMid.y = std::floor(lightMid.y * texelsPerUnit) / texelsPerUnit;
            const glm::vec3 snappedMid = glm::vec3(glm::inverse(tempLook) * glm::vec4(lightMid, 1.0f));

            const float cHeight = std::max(20.0f, std::max(0.5f, sunPos.y));
            const float cDist = std::max(radius + 35.0f, cHeight / std::max(0.15f, std::max(0.05f, sunDir.y)));
            const glm::mat4 lView = glm::lookAt(snappedMid + sunDir * cDist, snappedMid, upRef);
            glm::mat4 lProj = glm::ortho(-radius, radius, -radius, radius, 0.5f, cDist * 2.5f);
            lProj[1][1] *= -1.0f;
            cascadeLightSpace[c] = lProj * lView;
        }

        // WybĂłr gĹ‚Ăłwnego Ĺ›wiatĹ‚a punktowego rzucajÄ…cego cienie Cubemap
        int shadowedPointLightIdx = -1;
        float shadowedPointLightRange = 25.0f;
        glm::vec3 shadowedPointLightPos(0.0f);
        {
            auto lightView = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
            int localIdx = 0;
            for (auto lEntity : lightView) {
                const auto& lc = lightView.get<ecs::LightComponent>(lEntity);
                if (lc.type == "directional" || lc.type == "sun") continue;
                if (localIdx >= 8) break;
                if ((lc.type == "point" || lc.type == "spot") && shadowedPointLightIdx < 0 && lc.range > 0.1f && lc.intensity > 0.05f) {
                    shadowedPointLightPos = lightView.get<ecs::TransformComponent>(lEntity).position;
                    shadowedPointLightRange = lc.range;
                    shadowedPointLightIdx = localIdx;
                }
                localIdx++;
            }
        }

        bool rtLightOrCameraMoved = false;

        // Zapis SceneUboData przed wszystkimi przebiegami (uzywane przez Depth Prepass, PBR i Skybox)
        if (m_sceneUboMappedPtr) {
            SceneUboData ubo{};
            ubo.viewProj = vp;
            ubo.view = viewMatrix;
            ubo.invViewProj = glm::inverse(vp);
            for (int c = 0; c < 4; ++c) ubo.cascadeLightSpace[c] = cascadeLightSpace[c];
            ubo.cascadeSplits = glm::vec4(cSplits[0], cSplits[1], cSplits[2], cSplits[3]);
            ubo.cascadeRadii  = glm::vec4(cRadii[0],  cRadii[1],  cRadii[2],  cRadii[3]);
            ubo.cameraPos     = glm::vec4(camPos, 1.0f);
            ubo.sunDirection  = glm::vec4(sunDir, sunIntensity);
            ubo.sunPosition   = glm::vec4(sunPos, 1.0f);
            ubo.sunColor      = glm::vec4(sunColor, 1.0f);
            ubo.ambientColor  = glm::vec4(ambientColor, ambientIntensity);
            ubo.fogParams     = glm::vec4(m_fogEnabled ? 1.0f : 0.0f, m_fogDensity, m_fogHeightFalloff, 0.0f);
            ubo.fogColor      = glm::vec4(m_fogColor, 1.0f);
            ubo.screenParams  = glm::vec4(static_cast<float>(m_width), static_cast<float>(m_height),
                                          1.0f / static_cast<float>(std::max(1u, m_width)),
                                          1.0f / static_cast<float>(std::max(1u, m_height)));
            ubo.pointShadowParams = glm::vec4(static_cast<float>(shadowedPointLightIdx), 0.1f, shadowedPointLightRange, 0.015f);
            ubo.bounceParams = glm::vec4(
                m_lightBounceEnabled ? 1.0f : 0.0f,
                m_lightBounceIntensity,
                m_lightBounceGroundReflectance,
                m_softShadows ? 1.0f : 0.0f
            );
            auto srgbToLinear = [](float c) {
                return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
            };
            glm::vec3 linearGroundColor(
                srgbToLinear(m_lightBounceGroundColor.r),
                srgbToLinear(m_lightBounceGroundColor.g),
                srgbToLinear(m_lightBounceGroundColor.b)
            );
            ubo.bounceColor = glm::vec4(linearGroundColor, 1.0f);
            // RT is enabled only after this frame's TLAS has been built.
            ubo.rtParams = glm::vec4(
                0.0f,
                m_rtSettings.shadowsEnabled ? 1.0f : 0.0f,
                m_rtSettings.aoEnabled ? 1.0f : 0.0f,
                m_rtSettings.reflectionsEnabled ? static_cast<float>(std::clamp(m_rtSettings.reflectionSamples, 1, 16)) : 0.0f
            );
            // rtFeatureParams: x = diffuse GI, y = RT instance count (set below), z = GI intensity, w = debugMode
            ubo.rtFeatureParams = glm::vec4(
                m_rtSettings.giEnabled ? 1.0f : 0.0f,
                0.0f,
                m_rtSettings.giIntensity,
                static_cast<float>(static_cast<uint32_t>(m_rtSettings.debugMode))
            );
            ubo.frameIndex = static_cast<int>(m_frameCounter++);
            // Bit 0 controls skybox rendering metadata; bit 1 independently
            // controls environment lighting (diffuse IBL + specular reflections).
            ubo.skyboxMode = (m_skyboxEnabled ? 1 : 0) | (m_iblEnabled ? 2 : 0);

            auto lightView = registry.get().view<ecs::TransformComponent, ecs::LightComponent>();
            int lightIdx = 0;
            for (auto lEntity : lightView) {
                if (lightIdx >= 8) break;
                const auto& lt = lightView.get<ecs::TransformComponent>(lEntity);
                const auto& lc = lightView.get<ecs::LightComponent>(lEntity);
                if (lc.type == "directional" || lc.type == "sun") continue;

                float lType = 0.0f;
                if (lc.type == "spot") lType = 1.0f;
                else if (lc.type == "area") lType = 2.0f;

                const float pitch = glm::radians(lt.rotation.x);
                const float yaw   = glm::radians(lt.rotation.y);
                const glm::vec3 forward = glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));

                ubo.lightsPos[lightIdx] = glm::vec4(lt.position, std::max(1.0f, lc.range));
                ubo.lightsCol[lightIdx] = glm::vec4(lc.color, std::max(0.0f, lc.intensity));
                ubo.lightsDir[lightIdx] = glm::vec4(forward, lType);
                ubo.lightsParams[lightIdx] = glm::vec4(
                    std::cos(glm::radians(std::clamp(lc.innerConeAngle, 1.0f, 85.0f))),
                    std::cos(glm::radians(std::clamp(lc.outerConeAngle, lc.innerConeAngle + 1.0f, 89.0f))),
                    std::max(0.1f, lc.areaSizeX),
                    std::max(0.1f, lc.areaSizeY)
                );
                lightIdx++;
            }
            ubo.numLights = lightIdx;

            static glm::vec4 s_prevLightsPos[8]{};
            static glm::vec4 s_prevLightsCol[8]{};
            static glm::vec4 s_prevLightsDir[8]{};
            static glm::vec4 s_prevSunDir{0.0f};
            static glm::vec4 s_prevSunCol{0.0f};
            static int s_prevNumLights{-1};
            static bool s_hasPrevState{false};

            if (!s_hasPrevState) {
                rtLightOrCameraMoved = true;
                s_hasPrevState = true;
            } else {
                if (s_prevNumLights != ubo.numLights) {
                    rtLightOrCameraMoved = true;
                }
                if (glm::distance(glm::vec3(s_prevSunDir), glm::vec3(ubo.sunDirection)) > 0.001f ||
                    std::abs(s_prevSunDir.w - ubo.sunDirection.w) > 0.01f ||
                    glm::distance(glm::vec3(s_prevSunCol), glm::vec3(ubo.sunColor)) > 0.01f) {
                    rtLightOrCameraMoved = true;
                }
                for (int i = 0; i < ubo.numLights; ++i) {
                    if (glm::distance(glm::vec3(s_prevLightsPos[i]), glm::vec3(ubo.lightsPos[i])) > 0.005f ||
                        glm::distance(glm::vec3(s_prevLightsCol[i]), glm::vec3(ubo.lightsCol[i])) > 0.005f ||
                        std::abs(s_prevLightsCol[i].w - ubo.lightsCol[i].w) > 0.01f ||
                        glm::distance(glm::vec3(s_prevLightsDir[i]), glm::vec3(ubo.lightsDir[i])) > 0.005f) {
                        rtLightOrCameraMoved = true;
                        break;
                    }
                }
            }
            s_prevSunDir = ubo.sunDirection;
            s_prevSunCol = ubo.sunColor;
            s_prevNumLights = ubo.numLights;
            for (int i = 0; i < 8; ++i) {
                s_prevLightsPos[i] = ubo.lightsPos[i];
                s_prevLightsCol[i] = ubo.lightsCol[i];
                s_prevLightsDir[i] = ubo.lightsDir[i];
            }

            // Analityczne cienie sferyczne usuniete - zastepuja je rzeczywiste cienie mapowane
            ubo.numOccluders = 0;
            std::memcpy(m_sceneUboMappedPtr, &ubo, sizeof(SceneUboData));
        }

        // Budowa listy rysowania PBR (wspĂłlna dla Depth Prepass oraz Main PBR Pass)
        if (m_motionBufferMapped) {
            static_cast<glm::mat4*>(m_motionBufferMapped)[0] = m_prevViewProj;
        }
        // Reuse per-thread scratch storage: the render path is single-threaded,
        // and clearing these containers retains their capacity between frames.
        static thread_local std::vector<DrawItem> drawItems;
        static thread_local std::unordered_map<entt::entity, VkBuffer> deformedVertexBuffers;
        static thread_local std::vector<AccelerationStructure::Instance> rtInstances;
        static thread_local std::vector<RtInstanceGpuData> rtGpuInstances;
        drawItems.clear();
        deformedVertexBuffers.clear();
        rtInstances.clear();
        rtGpuInstances.clear();
        if (drawItems.capacity() < 64) drawItems.reserve(64);
        if (rtInstances.capacity() < 64) rtInstances.reserve(64);
        if (rtGpuInstances.capacity() < 64) rtGpuInstances.reserve(64);
        if (assetManager) {
            core::Profiler::ScopedSample drawItemPreparationSample("Draw Item Preparation");
            auto meshEntities = registry.get().view<ecs::TransformComponent, ecs::MeshComponent>();
            for (auto entity : meshEntities) {
                const auto& transform = meshEntities.get<ecs::TransformComponent>(entity);
                const auto& meshComp  = meshEntities.get<ecs::MeshComponent>(entity);
                const std::string& tag = registry.get().all_of<ecs::TagComponent>(entity) ? registry.get().get<ecs::TagComponent>(entity).name : kEmptyTag;

                // Only skip pure directional light helper entities that have no mesh to display
                if (tag == "DirectionalLight (Sun)" || tag == "DirectionalLight") continue;
                if (!meshComp.visible) continue;

                core::MeshAsset* meshAsset = assetManager->getMesh(meshComp.meshId);
                if (!meshAsset || !meshAsset->vertexBuffer || !meshAsset->indexBuffer) continue;

                glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.position)
                                * glm::mat4_cast(transform.orientation)
                                * glm::scale(glm::mat4(1.0f), transform.scale);

                // If default unscaled GroundPlane entity, apply fallback floor scaling
                if ((tag == "GroundPlane" || tag == "Ground") && transform.scale == glm::vec3(1.0f) && meshComp.meshId == 1) {
                    model = glm::scale(model, glm::vec3(100.0f, 4.0f, 100.0f));
                }

                glm::vec4 baseColor(0.22f, 0.74f, 0.97f, 1.0f);
                float metallic = 0.18f;
                float roughness = 0.42f;
                float emissive = 0.0f;
                uint32_t activeMaterialId = 0;
                bool reflectsLight = true;

                if (registry.get().all_of<ecs::MaterialComponent>(entity)) {
                    const auto& mat = registry.get().get<ecs::MaterialComponent>(entity);
                    baseColor = mat.baseColor;
                    metallic  = mat.metallic;
                    roughness = mat.roughness;
                    emissive  = mat.emissiveIntensity;
                    activeMaterialId = mat.materialId;
                    reflectsLight = mat.reflectsLight;
                } else if (meshAsset->materialId != 0 && assetManager->getMaterial(meshAsset->materialId)) {
                    core::MaterialAsset* mat = assetManager->getMaterial(meshAsset->materialId);
                    baseColor = mat->baseColorFactor;
                    metallic  = mat->metallicFactor;
                    roughness = mat->roughnessFactor;
                    emissive  = 0.0f;
                    activeMaterialId = meshAsset->materialId;
                } else if (meshComp.meshId == 1 || tag == "GroundPlane" || tag == "Ground") {
                    baseColor = glm::vec4(0.16f, 0.19f, 0.25f, 1.0f);
                    metallic  = 0.05f;
                    roughness = 0.78f;
                } else {
                    bool isCapsule = false;
                    bool isSensor  = false;
                    if (registry.get().all_of<ecs::RigidBodyComponent>(entity)) {
                        const auto& rb = registry.get().get<ecs::RigidBodyComponent>(entity);
                        isCapsule = (rb.shapeType == 2);
                        isSensor  = rb.isSensor;
                    }
                    glm::vec3 col(0.22f, 0.74f, 0.97f);
                    if (tag == "Sun" || tag == "Coin" || tag == "CoinTrigger" || tag.find("Coin") != std::string::npos) {
                        col = glm::vec3(0.980f, 0.804f, 0.082f); metallic = 0.85f; roughness = 0.22f; emissive = 0.45f;
                    } else if (tag == "Earth" || tag.find("Player") != std::string::npos || isCapsule) {
                        col = glm::vec3(0.000f, 0.941f, 1.000f); emissive = 0.35f;
                    } else if (tag == "Paddle") {
                        col = glm::vec3(0.063f, 0.725f, 0.506f);
                    } else if (tag == "Ball") {
                        col = glm::vec3(0.957f, 0.247f, 0.369f); emissive = 0.45f;
                    } else if (tag == "Brick") {
                        col = glm::vec3(0.659f, 0.333f, 0.969f);
                    } else if (tag == "LaserBullet") {
                        col = glm::vec3(0.000f, 1.000f, 1.000f); emissive = 0.8f;
                    } else if (tag.find("KillZ") != std::string::npos) {
                        col = glm::vec3(0.937f, 0.267f, 0.267f);
                    } else if (tag == "Checkpoint" || tag == "SpeedBoost") {
                        col = glm::vec3(0.133f, 0.773f, 0.369f);
                    } else if (isSensor) {
                        col = glm::vec3(0.918f, 0.702f, 0.031f);
                    } else if (tag.find("Enemy") != std::string::npos || tag.find("Meteor") != std::string::npos || tag == "Mars") {
                        col = glm::vec3(0.973f, 0.443f, 0.443f);
                    } else {
                        const uint32_t eId = static_cast<uint32_t>(entity);
                        col = glm::vec3(
                            0.20f + 0.80f * static_cast<float>((eId * 31u) % 100u) / 100.0f,
                            0.20f + 0.80f * static_cast<float>((eId * 53u) % 100u) / 100.0f,
                            0.20f + 0.80f * static_cast<float>((eId * 97u) % 100u) / 100.0f
                        );
                    }
                    baseColor = glm::vec4(col, 1.0f);
                }

                uint32_t instanceIdx = static_cast<uint32_t>(drawItems.size());
                uint32_t entityId = static_cast<uint32_t>(entity);
                auto itPrev = m_prevModelMatrices.find(entityId);
                glm::mat4 prevModel = (itPrev != m_prevModelMatrices.end()) ? itPrev->second : model;
                if (m_motionBufferMapped && (1 + instanceIdx) < (1 + RT_MAX_INSTANCE_COUNT)) {
                    static_cast<glm::mat4*>(m_motionBufferMapped)[1 + instanceIdx] = prevModel;
                }
                m_prevModelMatrices[entityId] = model;

                const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));
                DrawItem item{};
                item.mesh = meshAsset;
                item.vertexBuffer = meshAsset->vertexBuffer;
                item.entity = entity;
                item.materialId = activeMaterialId != 0 ? activeMaterialId : meshAsset->materialId;

                // CPU deformation keeps the established Vertex/RT ABI unchanged.
                // Every animated instance gets an independent transient vertex stream.
                const ecs::SkinComponent* skin = registry.get().all_of<ecs::SkinComponent>(entity)
                    ? &registry.get().get<ecs::SkinComponent>(entity) : nullptr;
                const ecs::MorphWeightsComponent* morph = registry.get().all_of<ecs::MorphWeightsComponent>(entity)
                    ? &registry.get().get<ecs::MorphWeightsComponent>(entity) : nullptr;
                // Additional glTF primitives are child entities of the source node.
                // Their morph weights are owned by that node and must stay live while
                // animation channels update the node's component.
                if (!morph && registry.get().all_of<ecs::HierarchyComponent>(entity)) {
                    const entt::entity parent = registry.get().get<ecs::HierarchyComponent>(entity).parent;
                    if (parent != entt::null && registry.get().valid(parent) &&
                        registry.get().all_of<ecs::MorphWeightsComponent>(parent)) {
                        morph = &registry.get().get<ecs::MorphWeightsComponent>(parent);
                    }
                }
                const bool hasMorph = morph && !morph->weights.empty() && !meshAsset->morphTargets.empty();
                const bool hasSkin = skin && !skin->jointMatrices.empty() && meshAsset->hasSkinInfluences &&
                                     meshAsset->skinInfluences.size() == meshAsset->vertexCount;
                bool deformedGeometryChanged = false;
                if ((hasMorph || hasSkin) && meshAsset->vertices.size() == meshAsset->vertexCount) {
                    std::vector<Vertex> deformed = meshAsset->vertices;
                    for (size_t vertexIndex = 0; vertexIndex < deformed.size(); ++vertexIndex) {
                        Vertex& vertex = deformed[vertexIndex];
                        if (hasMorph) {
                            const size_t targetCount = std::min(morph->weights.size(), meshAsset->morphTargets.size());
                            for (size_t targetIndex = 0; targetIndex < targetCount; ++targetIndex) {
                                const float weight = std::isfinite(morph->weights[targetIndex]) ? morph->weights[targetIndex] : 0.0f;
                                if (std::abs(weight) < 1e-6f) continue;
                                const auto& target = meshAsset->morphTargets[targetIndex];
                                if (vertexIndex < target.positionDeltas.size()) vertex.position += target.positionDeltas[vertexIndex] * weight;
                                if (vertexIndex < target.normalDeltas.size()) vertex.normal += target.normalDeltas[vertexIndex] * weight;
                                if (vertexIndex < target.tangentDeltas.size()) vertex.tangent += glm::vec4(target.tangentDeltas[vertexIndex] * weight, 0.0f);
                            }
                        }
                        if (hasSkin) {
                            glm::vec4 skinnedPosition(0.0f);
                            glm::vec3 skinnedNormal(0.0f);
                            glm::vec3 skinnedTangent(0.0f);
                            float totalWeight = 0.0f;
                            const auto& influence = meshAsset->skinInfluences[vertexIndex];
                            auto applyInfluence = [&](uint32_t jointIndex, float weight) {
                                if (!std::isfinite(weight) || weight <= 0.0f || jointIndex >= skin->jointMatrices.size()) return;
                                const glm::mat4& jointMatrix = skin->jointMatrices[jointIndex];
                                const glm::mat3 linearMatrix(jointMatrix);
                                const float linearDeterminant = glm::determinant(linearMatrix);
                                const glm::mat3 normalMatrix =
                                    (std::isfinite(linearDeterminant) && std::abs(linearDeterminant) > 1e-8f)
                                    ? glm::transpose(glm::inverse(linearMatrix))
                                    : glm::mat3(1.0f);
                                skinnedPosition += (jointMatrix * glm::vec4(vertex.position, 1.0f)) * weight;
                                skinnedNormal += (normalMatrix * vertex.normal) * weight;
                                // Tangents are directions in the surface plane: use the
                                // linear transform, then re-orthogonalize against the normal.
                                skinnedTangent += (linearMatrix * glm::vec3(vertex.tangent)) * weight;
                                totalWeight += weight;
                            };
                            for (int k = 0; k < 4; ++k) {
                                applyInfluence(influence.joints0[k], influence.weights0[k]);
                                applyInfluence(influence.joints1[k], influence.weights1[k]);
                            }
                            if (totalWeight > 1e-6f) {
                                vertex.position = glm::vec3(skinnedPosition) / totalWeight;
                                vertex.normal = skinnedNormal / totalWeight;
                                vertex.tangent = glm::vec4(skinnedTangent / totalWeight, vertex.tangent.w);
                            }
                        }
                        if (glm::dot(vertex.normal, vertex.normal) > 1e-8f)
                            vertex.normal = glm::normalize(vertex.normal);
                        glm::vec3 tangent = glm::vec3(vertex.tangent);
                        if (glm::dot(vertex.normal, vertex.normal) > 1e-8f)
                            tangent -= vertex.normal * glm::dot(vertex.normal, tangent);
                        if (glm::dot(tangent, tangent) > 1e-8f)
                            vertex.tangent = glm::vec4(glm::normalize(tangent), vertex.tangent.w);
                    }

                    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(deformed.size() * sizeof(Vertex));
                    uint64_t contentHash = 1469598103934665603ull;
                    const auto* vertexBytes = reinterpret_cast<const uint8_t*>(deformed.data());
                    for (size_t byte = 0; byte < static_cast<size_t>(byteSize); ++byte) {
                        contentHash ^= vertexBytes[byte];
                        contentHash *= 1099511628211ull;
                    }

                    const bool wantsRayTracingBuffer = isRayTracingEnabled();
                    auto cacheIt = m_deformedVertexCache.find(entity);
                    bool cacheValid = cacheIt != m_deformedVertexCache.end() &&
                        cacheIt->second.buffer != VK_NULL_HANDLE &&
                        cacheIt->second.size == byteSize &&
                        (!wantsRayTracingBuffer || cacheIt->second.rayTracingCapable);
                    bool geometryChanged = !cacheValid ||
                        (cacheIt != m_deformedVertexCache.end() && cacheIt->second.contentHash != contentHash);

                    if (cacheValid && geometryChanged) {
                        void* mapped = nullptr;
                        if (vmaMapMemory(m_context->getAllocator(), cacheIt->second.allocation, &mapped) == VK_SUCCESS) {
                            std::memcpy(mapped, deformed.data(), static_cast<size_t>(byteSize));
                            vmaFlushAllocation(m_context->getAllocator(), cacheIt->second.allocation, 0, VK_WHOLE_SIZE);
                            vmaUnmapMemory(m_context->getAllocator(), cacheIt->second.allocation);
                        } else {
                            cacheValid = false;
                        }
                    }

                    if (!cacheValid) {
                        VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
                        bufferInfo.size = byteSize;
                        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                        if (wantsRayTracingBuffer) {
                            bufferInfo.usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                        }
                        VmaAllocationCreateInfo allocationInfo{};
                        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
                        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
                        VkBuffer newBuffer = VK_NULL_HANDLE;
                        VmaAllocation newAllocation = VK_NULL_HANDLE;
                        if (vmaCreateBuffer(m_context->getAllocator(), &bufferInfo, &allocationInfo,
                                            &newBuffer, &newAllocation, nullptr) == VK_SUCCESS) {
                            void* mapped = nullptr;
                            if (vmaMapMemory(m_context->getAllocator(), newAllocation, &mapped) == VK_SUCCESS) {
                                std::memcpy(mapped, deformed.data(), static_cast<size_t>(byteSize));
                                vmaFlushAllocation(m_context->getAllocator(), newAllocation, 0, VK_WHOLE_SIZE);
                                vmaUnmapMemory(m_context->getAllocator(), newAllocation);
                                if (cacheIt != m_deformedVertexCache.end() && cacheIt->second.buffer)
                                    vmaDestroyBuffer(m_context->getAllocator(), cacheIt->second.buffer, cacheIt->second.allocation);
                                DeformedVertexCache cache{};
                                cache.buffer = newBuffer;
                                cache.allocation = newAllocation;
                                cache.size = byteSize;
                                cache.contentHash = contentHash;
                                cache.lastSeenFrame = m_deformationFrameCounter;
                                cache.rayTracingCapable = wantsRayTracingBuffer;
                                if (cacheIt == m_deformedVertexCache.end())
                                    m_deformedVertexCache.emplace(entity, cache);
                                else
                                    cacheIt->second = cache;
                                cacheIt = m_deformedVertexCache.find(entity);
                                cacheValid = true;
                                geometryChanged = true;
                            } else {
                                vmaDestroyBuffer(m_context->getAllocator(), newBuffer, newAllocation);
                            }
                        }
                    }

                    if (cacheValid) {
                        auto& cache = cacheIt->second;
                        cache.contentHash = contentHash;
                        cache.lastSeenFrame = m_deformationFrameCounter;
                        item.vertexBuffer = cache.buffer;
                        deformedVertexBuffers[entity] = cache.buffer;
                        deformedGeometryChanged = geometryChanged;
                    }
                }

                item.pcs.model = model;
                item.pcs.normalCol0 = glm::vec4(normalMat[0], metallic);
                item.pcs.normalCol1 = glm::vec4(normalMat[1], roughness);
                item.pcs.normalCol2 = glm::vec4(normalMat[2], emissive);
                item.pcs.baseColorFactor = glm::vec4(glm::vec3(baseColor), reflectsLight ? 1.0f : -1.0f);
                drawItems.push_back(item);
                if (isRayTracingEnabled() && rtGpuInstances.size() < RT_MAX_INSTANCE_COUNT) {
                    const bool hasDeformedRtGeometry = item.vertexBuffer != meshAsset->vertexBuffer;
                    const uint64_t blasId = hasDeformedRtGeometry
                        ? (0x8000000000000000ull |
                           (static_cast<uint64_t>(static_cast<uint32_t>(entity)) << 32u) |
                           static_cast<uint64_t>(meshComp.meshId))
                        : static_cast<uint64_t>(meshComp.meshId);
                    if (m_accelerationStructure.ensureBLAS(m_commandBuffer, blasId, *meshAsset,
                                                            item.vertexBuffer, hasDeformedRtGeometry,
                                                            deformedGeometryChanged)) {
                        VkBufferDeviceAddressInfo vertexAddressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
                        vertexAddressInfo.buffer = item.vertexBuffer;
                        VkBufferDeviceAddressInfo indexAddressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
                        indexAddressInfo.buffer = meshAsset->indexBuffer;
                        const VkDeviceAddress vertexAddress = vkGetBufferDeviceAddress(m_context->getDevice(), &vertexAddressInfo);
                        const VkDeviceAddress indexAddress = vkGetBufferDeviceAddress(m_context->getDevice(), &indexAddressInfo);

                        if (vertexAddress != 0 && indexAddress != 0) {
                            const uint32_t rtIndex = static_cast<uint32_t>(rtGpuInstances.size());
                            AccelerationStructure::Instance instance{};
                            instance.blasId = blasId;
                            instance.customIndex = rtIndex;
                            instance.transform = model;
                            rtInstances.push_back(instance);

                            RtInstanceGpuData gpu{};
                            gpu.model = model;
                            gpu.normalMatrix = glm::transpose(glm::inverse(model));
                            gpu.baseColor = glm::vec4(glm::vec3(baseColor), 1.0f);
                            gpu.material = glm::vec4(
                                glm::clamp(metallic, 0.0f, 1.0f),
                                glm::clamp(roughness, 0.0f, 1.0f),
                                std::max(emissive, 0.0f),
                                reflectsLight ? 1.0f : 0.0f
                            );
                            gpu.vertexAddress = vertexAddress;
                            gpu.indexAddress = indexAddress;
                            gpu.vertexCount = meshAsset->vertexCount;
                            gpu.indexCount = meshAsset->indexCount;
                            uint32_t rtMatId = activeMaterialId != 0 ? activeMaterialId : meshAsset->materialId;
                            if (auto* rtMaterial = assetManager->getMaterial(rtMatId)) {
                                gpu.textureIndices = glm::uvec4(
                                    rtMaterial->albedoTextureId < RT_MATERIAL_TEXTURE_COUNT ? rtMaterial->albedoTextureId : 0u,
                                    rtMaterial->normalTextureId < RT_MATERIAL_TEXTURE_COUNT ? rtMaterial->normalTextureId : 0u,
                                    rtMaterial->ormTextureId < RT_MATERIAL_TEXTURE_COUNT ? rtMaterial->ormTextureId : 0u,
                                    0u
                                );
                            }
                            rtGpuInstances.push_back(gpu);
                        }
                    }
                }
            }
            if ((m_frameCounter % 600u) == 0u && m_prevModelMatrices.size() > drawItems.size()) {
                for (auto it = m_prevModelMatrices.begin(); it != m_prevModelMatrices.end();) {
                    if (!registry.get().valid(static_cast<entt::entity>(it->first))) it = m_prevModelMatrices.erase(it);
                    else ++it;
                }
            }
            core::Profiler::get().recordMetric("drawItems", static_cast<double>(drawItems.size()));
            core::Profiler::get().recordMetric("rtInstances", static_cast<double>(rtGpuInstances.size()));

            bool rtSceneReady = false;
            if (isRayTracingEnabled() && !rtInstances.empty() &&
                rtGpuInstances.size() <= RT_MAX_INSTANCE_COUNT && m_rtInstanceDataMapped) {
                const VkDeviceSize rtDataBytes = sizeof(RtInstanceGpuData) * rtGpuInstances.size();
                std::memcpy(m_rtInstanceDataMapped, rtGpuInstances.data(), static_cast<size_t>(rtDataBytes));
                vmaFlushAllocation(m_context->getAllocator(), m_rtInstanceDataAllocation, 0, rtDataBytes);
                bool tlasHandleChanged = false;
                rtSceneReady = m_accelerationStructure.buildTLAS(m_commandBuffer, rtInstances, &tlasHandleChanged);
                if (tlasHandleChanged) {
                    updateRayTracingDescriptorSet(m_descriptorSet);
                    for (const auto& materialSet : m_materialDescriptorSets) {
                        updateRayTracingDescriptorSet(materialSet.second);
                    }
                }
            }
            if (m_sceneUboMappedPtr) {
                auto* sceneUbo = static_cast<SceneUboData*>(m_sceneUboMappedPtr);
                sceneUbo->rtParams.x = rtSceneReady ? 1.0f : 0.0f;
                sceneUbo->rtFeatureParams.y = static_cast<float>(rtGpuInstances.size());
            }
        }

        if (m_timestampQueryPool)
            vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, 1);

        // -----------------------------------------------------------------
        // Cache cieni: jesli kaskady i casterzy sie nie zmienili od poprzedniej
        // klatki, obrazy cieni zostaja w SHADER_READ_ONLY i nie sa renderowane.
        // -----------------------------------------------------------------
        auto fnv = [](uint64_t h, const void* data, size_t n) {
            const auto* b = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
            return h;
        };
        uint64_t casterHash = 1469598103934665603ull;
        for (const auto& di : drawItems) {
            casterHash = fnv(casterHash, &di.pcs.model, sizeof(di.pcs.model));
            casterHash = fnv(casterHash, &di.mesh, sizeof(di.mesh));
        }
        uint64_t dirShadowHash = fnv(casterHash, cascadeLightSpace, sizeof(cascadeLightSpace));
        const bool sunCasts = sunIntensity > 0.001f;
        dirShadowHash = fnv(dirShadowHash, &sunCasts, sizeof(sunCasts));
        dirShadowHash = fnv(dirShadowHash, &m_shadowWidth, sizeof(m_shadowWidth));
        dirShadowHash |= 1ull; // 0 = "brak cache"
        uint64_t pointShadowHash = fnv(casterHash, &shadowedPointLightIdx, sizeof(shadowedPointLightIdx));
        pointShadowHash = fnv(pointShadowHash, &shadowedPointLightPos, sizeof(shadowedPointLightPos));
        pointShadowHash = fnv(pointShadowHash, &shadowedPointLightRange, sizeof(shadowedPointLightRange));
        pointShadowHash |= 1ull;
        const bool dirShadowCached   = (dirShadowHash == m_shadowCacheHash);
        const bool pointShadowCached = (pointShadowHash == m_pointShadowCacheHash);

        // =========================================================================
        // PASS 1A: DIRECTIONAL 4-CASCADE SHADOW MAPS (4096x4096)
        // =========================================================================
        // An empty scene has no shadow casters. Mark its state cached without
        // recording four empty 4096x4096 cascade render passes every frame.
        if (drawItems.empty()) {
            m_shadowCacheHash = dirShadowHash;
        } else if (m_shadowPipeline && m_shadowImage && !dirShadowCached) {
            m_shadowCacheHash = dirShadowHash;
            transitionImage(m_commandBuffer, m_shadowImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, SHADOW_CASCADE_COUNT,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
            VkViewport shadowVp{ 0.0f, 0.0f, static_cast<float>(m_shadowWidth), static_cast<float>(m_shadowHeight), 0.0f, 1.0f };
            VkRect2D shadowSc{ {0, 0}, {m_shadowWidth, m_shadowHeight} };
            vkCmdSetViewport(m_commandBuffer, 0, 1, &shadowVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &shadowSc);

            auto shadowView = registry.get().view<ecs::TransformComponent, ecs::MeshComponent>();
            for (uint32_t c = 0; c < SHADOW_CASCADE_COUNT; ++c) {
                VkRenderingAttachmentInfo depthAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
                depthAtt.imageView = m_shadowCascadeImageViews[c];
                depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                depthAtt.clearValue.depthStencil = { 1.0f, 0 };

                VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
                ri.renderArea = { {0, 0}, {m_shadowWidth, m_shadowHeight} };
                ri.layerCount = 1;
                ri.pDepthAttachment = &depthAtt;
                vkCmdBeginRendering(m_commandBuffer, &ri);

                if (sunIntensity > 0.001f && assetManager) {
                    for (auto entity : shadowView) {
                        const auto& tc = shadowView.get<ecs::TransformComponent>(entity);
                        const auto& mc = shadowView.get<ecs::MeshComponent>(entity);
                        const std::string& tag = registry.get().all_of<ecs::TagComponent>(entity) ? registry.get().get<ecs::TagComponent>(entity).name : kEmptyTag;
                        if (mc.meshId == 1 || tag == "GroundPlane" || tag == "Ground") continue;
                        if (!mc.visible) continue;
                        if (tag == "DirectionalLight (Sun)" || tag == "DirectionalLight") continue;
                        if (registry.get().all_of<ecs::RigidBodyComponent>(entity) && registry.get().get<ecs::RigidBodyComponent>(entity).isSensor) continue;

                        core::MeshAsset* meshAsset = assetManager->getMesh(mc.meshId);
                        if (!meshAsset || !meshAsset->vertexBuffer || !meshAsset->indexBuffer) continue;

                        const glm::mat4 model = glm::translate(glm::mat4(1.0f), tc.position)
                                              * glm::mat4_cast(tc.orientation)
                                              * glm::scale(glm::mat4(1.0f), tc.scale);
                        const glm::mat4 lsMvp = cascadeLightSpace[c] * model;
                        vkCmdPushConstants(m_commandBuffer, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &lsMvp);

                        VkDeviceSize offset = 0;
                        VkBuffer shadowVertexBuffer = meshAsset->vertexBuffer;
                        if (auto deformedIt = deformedVertexBuffers.find(entity); deformedIt != deformedVertexBuffers.end())
                            shadowVertexBuffer = deformedIt->second;
                        vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &shadowVertexBuffer, &offset);
                        vkCmdBindIndexBuffer(m_commandBuffer, meshAsset->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(m_commandBuffer, meshAsset->indexCount, 1, 0, 0, 0);
                    }
                }
                vkCmdEndRendering(m_commandBuffer);
            }

            transitionImage(m_commandBuffer, m_shadowImage,
                            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, SHADOW_CASCADE_COUNT,
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }

        // =========================================================================
        // PASS 1B: OMNIDIRECTIONAL POINT LIGHT SHADOW CUBEMAP (1024x1024 x 6)
        // =========================================================================
        if (m_pointShadowImage && m_shadowPipeline && shadowedPointLightIdx >= 0 && !pointShadowCached) {
            m_pointShadowCacheHash = pointShadowHash;
            transitionImage(m_commandBuffer, m_pointShadowImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 6,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

            if (shadowedPointLightIdx >= 0 && assetManager) {
                const glm::vec3 targets[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
                const glm::vec3 ups[6]     = { {0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0} };
                glm::mat4 pProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, shadowedPointLightRange);
                // Uwaga: Dla cubemapy Vulkan NIE odwracamy pProj[1][1], poniewaz wektory ups[6]
                // juz dokladnie definiuja orientacje scian cubemapy zgodnie ze specyfikacja Vulkan/OpenGL.
                // Odwrocenie pProj[1][1] powodowalo lustrzane odbicie cieni wzdluz osi Z na scianie dolnej (-Y).

                VkViewport vpCube{ 0.0f, 0.0f, static_cast<float>(POINT_SHADOW_MAP_SIZE), static_cast<float>(POINT_SHADOW_MAP_SIZE), 0.0f, 1.0f };
                VkRect2D scCube{ {0, 0}, {POINT_SHADOW_MAP_SIZE, POINT_SHADOW_MAP_SIZE} };
                auto view = registry.get().view<ecs::TransformComponent, ecs::MeshComponent>();

                for (uint32_t face = 0; face < 6; ++face) {
                    VkRenderingAttachmentInfo faceAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
                    faceAtt.imageView = m_pointShadowFaceViews[face];
                    faceAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    faceAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    faceAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    faceAtt.clearValue.depthStencil = { 1.0f, 0 };

                    VkRenderingInfo faceRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
                    faceRi.renderArea = { {0, 0}, {POINT_SHADOW_MAP_SIZE, POINT_SHADOW_MAP_SIZE} };
                    faceRi.layerCount = 1;
                    faceRi.pDepthAttachment = &faceAtt;

                    vkCmdBeginRendering(m_commandBuffer, &faceRi);
                    vkCmdSetViewport(m_commandBuffer, 0, 1, &vpCube);
                    vkCmdSetScissor(m_commandBuffer, 0, 1, &scCube);
                    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);

                    const glm::mat4 pVp = pProj * glm::lookAt(shadowedPointLightPos, shadowedPointLightPos + targets[face], ups[face]);
                    for (auto entity : view) {
                        const auto& tc = view.get<ecs::TransformComponent>(entity);
                        const auto& mc = view.get<ecs::MeshComponent>(entity);
                        if (mc.meshId == 1) continue;
                        if (!mc.visible) continue;
                        const std::string& tag = registry.get().all_of<ecs::TagComponent>(entity) ? registry.get().get<ecs::TagComponent>(entity).name : kEmptyTag;
                        if (tag.find("Light") != std::string::npos || tag.find("light") != std::string::npos) continue;

                        core::MeshAsset* mesh = assetManager->getMesh(mc.meshId);
                        if (!mesh || !mesh->vertexBuffer || !mesh->indexBuffer) continue;

                        const glm::mat4 mvp = pVp * glm::translate(glm::mat4(1.0f), tc.position)
                                            * glm::mat4_cast(tc.orientation)
                                            * glm::scale(glm::mat4(1.0f), tc.scale);
                        vkCmdPushConstants(m_commandBuffer, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);

                        VkDeviceSize offset = 0;
                        VkBuffer shadowVertexBuffer = mesh->vertexBuffer;
                        if (auto deformedIt = deformedVertexBuffers.find(entity); deformedIt != deformedVertexBuffers.end())
                            shadowVertexBuffer = deformedIt->second;
                        vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &shadowVertexBuffer, &offset);
                        vkCmdBindIndexBuffer(m_commandBuffer, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(m_commandBuffer, mesh->indexCount, 1, 0, 0, 0);
                    }
                    vkCmdEndRendering(m_commandBuffer);
                }
            }

            transitionImage(m_commandBuffer, m_pointShadowImage,
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 6,
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }

        if (m_timestampQueryPool)
            vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, 2);

        const VkViewport fullVp{ 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f };
        const VkRect2D fullSc{ {0, 0}, {m_width, m_height} };

        // With no raster draw items and no RT instances, geometry prepasses and
        // screen-space AO/GI are redundant. The HDR pass clears the depth attachment.
        const bool skipEmptySceneGeometryPasses = drawItems.empty() && rtInstances.empty();

        // =========================================================================
        // PASS 2A: DEPTH PREPASS (WypeĹ‚nienie Z-Bufora dla GTAO oraz Early-Z)
        // =========================================================================
        transitionImage(m_commandBuffer, m_depthImage,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, 0,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

        if (!skipEmptySceneGeometryPasses) {
            VkRenderingAttachmentInfo depthPreAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            depthPreAtt.imageView = m_depthImageView;
            depthPreAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthPreAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthPreAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthPreAtt.clearValue.depthStencil = { 1.0f, 0 };

            VkRenderingInfo preRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            preRi.renderArea = { {0, 0}, {m_width, m_height} };
            preRi.layerCount = 1;
            preRi.pDepthAttachment = &depthPreAtt;

            vkCmdBeginRendering(m_commandBuffer, &preRi);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);
            if (m_depthPrepassPipeline && m_descriptorSet) {
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_depthPrepassPipeline);
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
                for (const auto& item : drawItems) {
                    vkCmdPushConstants(m_commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PbrPushConstants), &item.pcs);
                    VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &item.vertexBuffer, &offset);
                    vkCmdBindIndexBuffer(m_commandBuffer, item.mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(m_commandBuffer, item.mesh->indexCount, 1, 0, 0, 0);
                }
            }
            vkCmdEndRendering(m_commandBuffer);
        }

        if (m_timestampQueryPool)
            vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, 3);

        // =========================================================================
        // PASS 2A.5: THIN G-BUFFER (Smooth Shading Normals + Roughness + Albedo + Metallic + Motion)
        // Rendered with depthCompareOp = EQUAL against Depth Prepass Z-buffer.
        // =========================================================================
        if (!skipEmptySceneGeometryPasses && m_gBufferPipeline && m_gBufferNormalRoughnessImage &&
            m_gBufferAlbedoMetallicImage && m_gBufferMotionImage) {
            transitionImage(m_commandBuffer, m_gBufferNormalRoughnessImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            transitionImage(m_commandBuffer, m_gBufferAlbedoMetallicImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            transitionImage(m_commandBuffer, m_gBufferMotionImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            VkRenderingAttachmentInfo gbufferAtts[3]{};
            gbufferAtts[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            gbufferAtts[0].imageView = m_gBufferNormalRoughnessImageView;
            gbufferAtts[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            gbufferAtts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            gbufferAtts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            gbufferAtts[0].clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

            gbufferAtts[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            gbufferAtts[1].imageView = m_gBufferAlbedoMetallicImageView;
            gbufferAtts[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            gbufferAtts[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            gbufferAtts[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            gbufferAtts[1].clearValue.color = { {0.0f, 0.0f, 0.0f, 0.0f} };

            gbufferAtts[2].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            gbufferAtts[2].imageView = m_gBufferMotionImageView;
            gbufferAtts[2].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            gbufferAtts[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            gbufferAtts[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            gbufferAtts[2].clearValue.color = { {0.0f, 0.0f, 0.0f, 0.0f} };

            VkRenderingAttachmentInfo gbufferDepthAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            gbufferDepthAtt.imageView = m_depthImageView;
            gbufferDepthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            gbufferDepthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            gbufferDepthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo gbufferRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            gbufferRi.renderArea = { {0, 0}, {m_width, m_height} };
            gbufferRi.layerCount = 1;
            gbufferRi.colorAttachmentCount = 3;
            gbufferRi.pColorAttachments = gbufferAtts;
            gbufferRi.pDepthAttachment = &gbufferDepthAtt;

            vkCmdBeginRendering(m_commandBuffer, &gbufferRi);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);

            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gBufferPipeline);
            VkDescriptorSet currentBoundSet = VK_NULL_HANDLE;
            for (uint32_t drawIdx = 0; drawIdx < drawItems.size(); ++drawIdx) {
                const auto& item = drawItems[drawIdx];
                VkDescriptorSet set = m_descriptorSet;
                uint32_t matId = item.materialId != 0 ? item.materialId : (item.mesh ? item.mesh->materialId : 0);
                if (matId != 0) {
                    set = getOrCreateMaterialDescriptorSet(matId, assetManager);
                }
                if (set != currentBoundSet) {
                    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &set, 0, nullptr);
                    currentBoundSet = set;
                }
                vkCmdPushConstants(m_commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PbrPushConstants), &item.pcs);
                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &item.vertexBuffer, &offset);
                vkCmdBindIndexBuffer(m_commandBuffer, item.mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(m_commandBuffer, item.mesh->indexCount, 1, 0, 0, drawIdx);
            }
            vkCmdEndRendering(m_commandBuffer);

            transitionImage(m_commandBuffer, m_gBufferNormalRoughnessImage,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

            transitionImage(m_commandBuffer, m_gBufferAlbedoMetallicImage,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

            transitionImage(m_commandBuffer, m_gBufferMotionImage,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }

        if (m_timestampQueryPool)
            vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, 4);

        // =========================================================================
        // PASS 2B: MULTI-BOUNCE GTAO + BILATERAL BLUR (przed cieniowaniem IBL PBR!)
        // =========================================================================
        if (!skipEmptySceneGeometryPasses) {
            transitionImage(m_commandBuffer, m_depthImage,
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }

        core::Profiler::get().recordMetric("gtaoEnabled", m_gtaoEnabled ? 1.0 : 0.0);
        core::Profiler::get().recordMetric("ssgiEnabled", m_lightBounceEnabled ? 1.0 : 0.0);
        core::Profiler::get().recordMetric("gtaoRadius", m_gtaoRadius);
        core::Profiler::get().recordMetric("gtaoIntensity", m_gtaoIntensity);
        core::Profiler::get().recordMetric("ssgiIntensity", m_lightBounceIntensity);
        core::Profiler::get().recordMetric("ssgiGroundReflectance", m_lightBounceGroundReflectance);
        core::Profiler::get().recordMetric("ssgiMultiBounce", m_lightBounceMultiBounce);
        core::Profiler::get().recordMetric("ssgiGroundColorR", m_lightBounceGroundColor.r);
        core::Profiler::get().recordMetric("ssgiGroundColorG", m_lightBounceGroundColor.g);
        core::Profiler::get().recordMetric("ssgiGroundColorB", m_lightBounceGroundColor.b);
        m_screenSpaceAoGiPassActive = (m_gtaoEnabled || m_lightBounceEnabled) &&
            m_gtaoPipeline && m_gtaoBlurPipeline && !drawItems.empty();
        if (m_screenSpaceAoGiPassActive) {
            transitionImage(m_commandBuffer, m_aoImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            VkRenderingAttachmentInfo aoAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            aoAtt.imageView = m_aoImageView;
            aoAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            aoAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            aoAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo aoRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            aoRi.renderArea = { {0, 0}, {m_width, m_height} };
            aoRi.layerCount = 1;
            aoRi.colorAttachmentCount = 1;
            aoRi.pColorAttachments = &aoAtt;

            vkCmdBeginRendering(m_commandBuffer, &aoRi);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gtaoPipeline);
            vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gtaoPipelineLayout, 0, 1, &m_gtaoDescSet, 0, nullptr);

            const float tanHalfFovY = std::tan(glm::radians(sceneFov) * 0.5f);
            const glm::mat4 currentViewProj = vp;
            // On the first frame (and after a resize), use the current camera as
            // the history camera. This produces an identity reprojection instead
            // of pulling unrelated pixels from an undefined/stale history image.
            const glm::mat4 historyViewProj = m_hasPreviousViewProj
                ? m_prevViewProj
                : currentViewProj;
            const glm::mat4 prevViewProjFromView = historyViewProj * glm::inverse(viewMatrix);
            m_prevViewProj = currentViewProj;
            m_hasPreviousViewProj = true;

            struct GtaoPush {
                glm::vec4 params;
                glm::vec4 screenParams;
                glm::vec4 flags;
                glm::mat4 prevViewProjFromView;
            } gtaoPcsData;
            gtaoPcsData.params = glm::vec4(m_gtaoRadius, m_gtaoIntensity, tanHalfFovY, sceneAspect);
            gtaoPcsData.screenParams = glm::vec4(1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height), 0.1f, 1000.0f);
            gtaoPcsData.flags = glm::vec4(m_gtaoEnabled ? 1.0f : 0.0f, m_lightBounceEnabled ? 1.0f : 0.0f, 0.0f, 0.0f);
            gtaoPcsData.prevViewProjFromView = prevViewProjFromView;

            vkCmdPushConstants(m_commandBuffer, m_gtaoPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(gtaoPcsData), &gtaoPcsData);
            vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            vkCmdEndRendering(m_commandBuffer);
            if (m_timestampQueryPool)
                vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, 9);

            transitionImage(m_commandBuffer, m_aoImage,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

            transitionImage(m_commandBuffer, m_aoBlurImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            VkRenderingAttachmentInfo blurAtt = aoAtt;
            blurAtt.imageView = m_aoBlurImageView;
            VkRenderingInfo blurRi = aoRi;
            blurRi.pColorAttachments = &blurAtt;

            vkCmdBeginRendering(m_commandBuffer, &blurRi);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gtaoBlurPipeline);
            vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gtaoBlurPipelineLayout, 0, 1, &m_gtaoBlurDescSet, 0, nullptr);

            const glm::vec4 blurPcs[2] = {
                glm::vec4(1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height), 0.1f, 1000.0f),
                glm::vec4(tanHalfFovY, sceneAspect, 0.0f, 0.0f)
            };
            vkCmdPushConstants(m_commandBuffer, m_gtaoBlurPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(blurPcs), blurPcs);
            vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            vkCmdEndRendering(m_commandBuffer);
            if (m_timestampQueryPool)
                vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, 10);

            transitionImage(m_commandBuffer, m_aoBlurImage,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        } else {
            // No screen-space shader or bilateral blur ran this frame; keep both subpass timings at zero.
            if (m_timestampQueryPool) {
                vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, 9);
                vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, 10);
            }
            transitionImage(m_commandBuffer, m_aoBlurImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            VkRenderingAttachmentInfo clearAo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            clearAo.imageView = m_aoBlurImageView;
            clearAo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            clearAo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            clearAo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            clearAo.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

            VkRenderingInfo clearRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            clearRi.renderArea = { {0, 0}, {m_width, m_height} };
            clearRi.layerCount = 1;
            clearRi.colorAttachmentCount = 1;
            clearRi.pColorAttachments = &clearAo;
            vkCmdBeginRendering(m_commandBuffer, &clearRi);
            vkCmdEndRendering(m_commandBuffer);

            transitionImage(m_commandBuffer, m_aoBlurImage,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }

        // Keep the motion-vector camera history current even when GTAO and light bounce are disabled.
        m_prevViewProj = vp;
        m_hasPreviousViewProj = true;

        // =========================================================================
        // PASS 2C: RT INDIRECT (diffuse GI + AO): trace -> temporal accumulation -> a-trous
        // Depth is still in DEPTH_STENCIL_READ_ONLY here. Result is sampled by pbr_rt.frag.
        // =========================================================================
        {
            const bool tlasReady = m_sceneUboMappedPtr &&
                static_cast<SceneUboData*>(m_sceneUboMappedPtr)->rtParams.x > 0.5f;

            if (tlasReady && m_worldRadianceCache.isInitialized() && m_accelerationStructure.getTLAS() != VK_NULL_HANDLE) {
                const auto* sceneUbo = static_cast<const SceneUboData*>(m_sceneUboMappedPtr);
                m_worldRadianceCache.dispatchUpdate(
                    m_commandBuffer,
                    m_descriptorSet,
                    m_accelerationStructure.getTLAS(),
                    m_frameCounter,
                    glm::vec3(sceneUbo->cameraPos),
                    glm::vec3(sceneUbo->sunDirection),
                    sceneUbo->sunColor
                );
            }

            const bool rtIndirectActive = m_rtDenoiser.isReady() && isRayTracingEnabled() &&
                m_rtGraphicsPipeline != VK_NULL_HANDLE &&
                m_accelerationStructure.getTLAS() != VK_NULL_HANDLE &&
                tlasReady && m_descriptorSet != VK_NULL_HANDLE &&
                (m_rtSettings.giEnabled || m_rtSettings.aoEnabled || m_rtSettings.reflectionsEnabled || m_rtSettings.debugMode != RtDebugMode::None);
            core::Profiler::get().recordMetric("rtGiEnabled", m_rtSettings.giEnabled ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtAoEnabled", m_rtSettings.aoEnabled ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtReflectionsEnabled", m_rtSettings.reflectionsEnabled ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtEnabled", m_rtSettings.enabled ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtAvailable", isRayTracingEnabled() ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtShadowsEnabled", m_rtSettings.shadowsEnabled ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtGiHalfRes", m_rtSettings.giHalfRes ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtGiCheckerboard", m_rtSettings.giCheckerboard ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtGiIntensity", m_rtSettings.giIntensity);
            core::Profiler::get().recordMetric("rtGiSamples", std::clamp(m_rtSettings.giSamples, 1, 16));
            core::Profiler::get().recordMetric("rtGiBounces", std::clamp(m_rtSettings.giBounces, 1, 15));
            core::Profiler::get().recordMetric("rtReflectionSamples", std::clamp(m_rtSettings.reflectionSamples, 1, 16));
            core::Profiler::get().recordMetric("rtReflectionBounces", std::clamp(m_rtSettings.reflectionBounces, 2, 4));
            core::Profiler::get().recordMetric("rtReflectionInfiniteBounces", m_rtSettings.reflectionInfiniteBounces ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtReflectionMaxRoughness", m_rtSettings.reflectionMaxRoughness);
            core::Profiler::get().recordMetric("rtReflectionMinThroughput", m_rtSettings.reflectionMinThroughput);
            core::Profiler::get().recordMetric("rtDenoiseEnabled", m_rtSettings.denoiseEnabled ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtSvgfEnabled", (m_rtSettings.denoiseEnabled && m_rtSettings.svgfEnabled) ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtMaxHistory", m_rtSettings.denoiseEnabled ? m_rtSettings.maxHistory : 1.0);
            core::Profiler::get().recordMetric("rtSpatialStrength", m_rtSettings.denoiseEnabled ? m_rtSettings.spatialStrength : 0.0);
            core::Profiler::get().recordMetric("rtReflectionCheckerboard", m_rtSettings.reflectionCheckerboard ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtReflectionStochasticLights", m_rtSettings.reflectionStochasticLights ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtReflectionPrimaryStochasticLights", m_rtSettings.reflectionPrimaryStochasticLights ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtReflectionLodBias", m_rtSettings.reflectionLodBias ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtReflectionHalfRes", m_rtSettings.reflectionHalfRes ? 1.0 : 0.0);
            core::Profiler::get().recordMetric("rtDebugMode", static_cast<uint32_t>(m_rtSettings.debugMode));
            if (rtIndirectActive) {
                RtDenoiser::Params rtp;
                rtp.viewProj = vp;
                rtp.samples = static_cast<uint32_t>(std::clamp(m_rtSettings.giSamples, 1, 16));
                rtp.frameIndex = m_frameCounter;
                rtp.giEnabled = m_rtSettings.giEnabled;
                rtp.giCheckerboard = m_rtSettings.giCheckerboard;
                rtp.aoEnabled = m_rtSettings.aoEnabled;
                rtp.reflectionsEnabled = m_rtSettings.reflectionsEnabled;
                rtp.reflectionSamples = static_cast<uint32_t>(std::clamp(m_rtSettings.reflectionSamples, 1, 16));
                rtp.bounces = static_cast<uint32_t>(std::clamp(m_rtSettings.giBounces, 1, 15));
                rtp.reflectionBounces = static_cast<uint32_t>(std::clamp(m_rtSettings.reflectionBounces, 2, 4));
                rtp.reflectionInfiniteBounces = m_rtSettings.reflectionInfiniteBounces;
                rtp.reflectionMaxRoughness = m_rtSettings.reflectionMaxRoughness;
                rtp.reflectionMinThroughput = m_rtSettings.reflectionMinThroughput;
                rtp.reflectionCheckerboard = m_rtSettings.reflectionCheckerboard;
                rtp.reflectionStochasticLights = m_rtSettings.reflectionStochasticLights;
                rtp.reflectionPrimaryStochasticLights = m_rtSettings.reflectionPrimaryStochasticLights;
                rtp.reflectionLodBias = m_rtSettings.reflectionLodBias;
                rtp.reflectionHalfRes = m_rtSettings.reflectionHalfRes;
                rtp.reflectionShowLightBulbs = m_rtSettings.reflectionShowLightBulbs;
                rtp.reflectionDenoiseMirrors = m_rtSettings.reflectionDenoiseMirrors;
                rtp.maxHistory = static_cast<float>(m_rtSettings.maxHistory);
                rtp.spatialStrength = m_rtSettings.spatialStrength;
                rtp.svgfEnabled = m_rtSettings.denoiseEnabled && m_rtSettings.svgfEnabled;
                rtp.giTemporalFilterEnabled = m_rtSettings.denoiseEnabled && m_rtSettings.giTemporalFilterEnabled;
                rtp.giHistoryClampEnabled = m_rtSettings.denoiseEnabled && m_rtSettings.giHistoryClampEnabled;
                rtp.giTemporalFireflyFilterEnabled = m_rtSettings.denoiseEnabled && m_rtSettings.giTemporalFireflyFilterEnabled;
                rtp.giTraceFireflyClampEnabled = m_rtSettings.giTraceFireflyClampEnabled;
                rtp.giAtrousPassEnabled = {
                    m_rtSettings.denoiseEnabled && m_rtSettings.giAtrousPass1Enabled,
                    m_rtSettings.denoiseEnabled && m_rtSettings.giAtrousPass2Enabled,
                    m_rtSettings.denoiseEnabled && m_rtSettings.giAtrousPass3Enabled,
                    m_rtSettings.denoiseEnabled && m_rtSettings.giAtrousPass4Enabled
                };
                rtp.reflectionTemporalFilterEnabled = m_rtSettings.reflectionTemporalFilterEnabled;
                rtp.reflectionOutlierFilterEnabled = m_rtSettings.reflectionOutlierFilterEnabled;
                rtp.reflectionSpatialFilterEnabled = m_rtSettings.reflectionSpatialFilterEnabled;
                rtp.lightChanged = rtLightOrCameraMoved;
                rtp.debugMode = m_rtSettings.debugMode;
                core::Profiler::ScopedSample rtDenoiserRecordSample("RT Denoiser Command Recording");
                m_rtDenoiser.record(m_commandBuffer, m_descriptorSet, rtp);
            } else {
                m_rtDenoiser.invalidateHistory();
                core::Profiler::get().recordMetric("rtGiTraceMs", 0.0);
                core::Profiler::get().recordMetric("rtGiTemporalMs", 0.0);
                core::Profiler::get().recordMetric("rtGiAtrous0Ms", 0.0);
                core::Profiler::get().recordMetric("rtGiAtrous1Ms", 0.0);
                core::Profiler::get().recordMetric("rtReflectionTraceMs", 0.0);
                auto& rtProfiler = core::Profiler::get();
                rtProfiler.recordMetric("rtReflectionTemporalMs", 0.0);
                rtProfiler.recordMetric("rtComputeTotalMs", 0.0);
                rtProfiler.recordMetric("rtGiTraceExecMs", 0.0);
                rtProfiler.recordMetric("rtGiTraceSyncMs", 0.0);
                rtProfiler.recordMetric("rtGiTemporalExecMs", 0.0);
                rtProfiler.recordMetric("rtGiTemporalSyncMs", 0.0);
                rtProfiler.recordMetric("rtGiAtrous0ExecMs", 0.0);
                rtProfiler.recordMetric("rtGiAtrous0SyncMs", 0.0);
                rtProfiler.recordMetric("rtReflectionStartSyncMs", 0.0);
                rtProfiler.recordMetric("rtReflectionTraceExecMs", 0.0);
                rtProfiler.recordMetric("rtReflectionTraceSyncMs", 0.0);
                rtProfiler.recordMetric("rtReflectionTemporalExecMs", 0.0);
                rtProfiler.recordMetric("rtReflectionTemporalSyncMs", 0.0);

                // Keep the quality snapshot current even when the RT dispatch is inactive.
                rtProfiler.recordMetric("rtGiSamples", std::clamp(m_rtSettings.giSamples, 1, 16));
                rtProfiler.recordMetric("rtGiBounces", std::clamp(m_rtSettings.giBounces, 1, 15));
                rtProfiler.recordMetric("rtGiWidth", m_rtDenoiser.getGiWidth());
                rtProfiler.recordMetric("rtGiHeight", m_rtDenoiser.getGiHeight());
                rtProfiler.recordMetric("rtMaxHistory", m_rtSettings.denoiseEnabled
                    ? static_cast<double>(m_rtSettings.maxHistory) : 1.0);
                rtProfiler.recordMetric("rtSpatialStrength", m_rtSettings.denoiseEnabled
                    ? static_cast<double>(m_rtSettings.spatialStrength) : 0.0);
                rtProfiler.recordMetric("rtSvgfEnabled",
                    m_rtSettings.denoiseEnabled && m_rtSettings.svgfEnabled ? 1.0 : 0.0);
                rtProfiler.recordMetric("rtGiCheckerboard", m_rtSettings.giCheckerboard ? 1.0 : 0.0);
                rtProfiler.recordMetric("rtGiHistoryValid", 0.0);
                rtProfiler.recordMetric("rtReflectionHistoryValid", 0.0);
                rtProfiler.recordMetric("rtHistoryInvalidatedByLightChange", 0.0);
                rtProfiler.recordMetric("rtReflectionSamples", std::clamp(m_rtSettings.reflectionSamples, 1, 16));
                rtProfiler.recordMetric("rtReflectionBounces", std::clamp(m_rtSettings.reflectionBounces, 2, 4));
                rtProfiler.recordMetric("rtReflectionInfiniteBounces", m_rtSettings.reflectionInfiniteBounces ? 1.0 : 0.0);
                rtProfiler.recordMetric("rtReflectionMaxRoughness", m_rtSettings.reflectionMaxRoughness);
                rtProfiler.recordMetric("rtReflectionCheckerboard", m_rtSettings.reflectionCheckerboard ? 1.0 : 0.0);
                rtProfiler.recordMetric("rtReflectionStochasticLights", m_rtSettings.reflectionStochasticLights ? 1.0 : 0.0);
                rtProfiler.recordMetric("rtReflectionPrimaryStochasticLights", m_rtSettings.reflectionPrimaryStochasticLights ? 1.0 : 0.0);
                rtProfiler.recordMetric("rtReflectionLodBias", m_rtSettings.reflectionLodBias ? 1.0 : 0.0);
                rtProfiler.recordMetric("rtReflectionHalfRes", m_rtSettings.reflectionHalfRes ? 1.0 : 0.0);
                rtProfiler.recordMetric("rtDebugMode", static_cast<uint32_t>(m_rtSettings.debugMode));
            }
        }

        if (m_timestampQueryPool)
            vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, 5);

        // =========================================================================
        // PASS 3: MAIN HDR PBR PASS + CUBEMAP SKYBOX + SOFT GAUSSIAN PARTICLES
        // =========================================================================
        transitionImage(m_commandBuffer, m_hdrImage,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        if (!skipEmptySceneGeometryPasses) {
            transitionImage(m_commandBuffer, m_depthImage,
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
        }

        {
            VkRenderingAttachmentInfo hdrColorAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            hdrColorAtt.imageView = m_hdrImageView;
            hdrColorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            hdrColorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            hdrColorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            hdrColorAtt.clearValue.color = { {0.015f, 0.018f, 0.032f, 1.0f} };

            VkRenderingAttachmentInfo hdrDepthAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            hdrDepthAtt.imageView = m_depthImageView;
            hdrDepthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            hdrDepthAtt.loadOp = skipEmptySceneGeometryPasses
                ? VK_ATTACHMENT_LOAD_OP_CLEAR
                : VK_ATTACHMENT_LOAD_OP_LOAD; // Reuse the prepass depth when geometry exists.
            hdrDepthAtt.clearValue.depthStencil = { 1.0f, 0 };
            hdrDepthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo hdrRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            hdrRi.renderArea = { {0, 0}, {m_width, m_height} };
            hdrRi.layerCount = 1;
            hdrRi.colorAttachmentCount = 1;
            hdrRi.pColorAttachments = &hdrColorAtt;
            hdrRi.pDepthAttachment = &hdrDepthAtt;

            vkCmdBeginRendering(m_commandBuffer, &hdrRi);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);

            if (m_graphicsPipeline && m_descriptorSet) {
                const bool useRayQuery = isRayTracingEnabled() &&
                    m_rtGraphicsPipeline != VK_NULL_HANDLE &&
                    m_accelerationStructure.getTLAS() != VK_NULL_HANDLE;
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    useRayQuery ? m_rtGraphicsPipeline : m_graphicsPipeline);
                VkDescriptorSet currentBoundSet = VK_NULL_HANDLE;
                for (const auto& item : drawItems) {
                    VkDescriptorSet set = m_descriptorSet;
                    uint32_t matId = item.materialId != 0 ? item.materialId : (item.mesh ? item.mesh->materialId : 0);
                    if (matId != 0) {
                        set = getOrCreateMaterialDescriptorSet(matId, assetManager);
                    }
                    if (set != currentBoundSet) {
                        vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &set, 0, nullptr);
                        currentBoundSet = set;
                    }
                    vkCmdPushConstants(m_commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PbrPushConstants), &item.pcs);
                    VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &item.vertexBuffer, &offset);
                    vkCmdBindIndexBuffer(m_commandBuffer, item.mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(m_commandBuffer, item.mesh->indexCount, 1, 0, 0, 0);
                }
            }

            // Fizyczny Skybox HDR z prefiltrowanej Cubemapy (renderowany tylko tam, gdzie depth == 1.0)
            if (m_skyboxEnabled && m_skyboxPipeline && m_descriptorSet) {
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyboxPipeline);
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyboxPipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
                vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            }

            // Energetyczne czÄ…steczki HDR (Niagara-style)
            if (m_particlePipeline && m_particleMappedPtr) {
                static std::vector<ParticleVertex> particles;
                particles.clear();
                particles.reserve(2048);

                static auto particleClockStart = std::chrono::steady_clock::now();
                const float curTime = std::chrono::duration<float>(std::chrono::steady_clock::now() - particleClockStart).count();

                auto particleView = registry.get().view<ecs::TransformComponent, ecs::ParticleEmitterComponent>();
                for (auto entity : particleView) {
                    const auto& transform = particleView.get<ecs::TransformComponent>(entity);
                    const auto& emitter   = particleView.get<ecs::ParticleEmitterComponent>(entity);
                    const uint32_t numParticles = std::min(emitter.count, 500u);
                    const glm::vec3 basePos = transform.position;
                    const glm::vec3 emitColor = emitter.color;
                    const float emitSpeed = emitter.speed > 0.01f ? emitter.speed : 3.0f;
                    const float emitSize  = emitter.size  > 0.01f ? emitter.size  : 0.4f;
                    const int pType = emitter.type == "sparks" ? 0 : emitter.type == "smoke" ? 1 : emitter.type == "portal" ? 2 : emitter.type == "explosion" ? 3 : 4;

                    for (uint32_t i = 0; i < numParticles && particles.size() < MAX_PARTICLES; ++i) {
                        const float seed  = static_cast<float>((i * 1337u) % 1000u) / 1000.0f;
                        const float seed2 = static_cast<float>((i * 7919u) % 1000u) / 1000.0f;
                        const float seed3 = static_cast<float>((i * 4973u) % 1000u) / 1000.0f;
                        const float lifetime = 1.0f + seed * 1.5f;
                        const float age = std::fmod(curTime * emitSpeed + seed * lifetime, lifetime);
                        const float progress = age / lifetime;

                        ParticleVertex pv{};
                        if (pType == 0) {
                            const float phi = seed2 * 6.283185f;
                            const float theta = seed3 * 3.14159f;
                            const glm::vec3 dir(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
                            pv.pos = basePos + dir * (age * emitSpeed) + glm::vec3(0.0f, -4.9f * age * age, 0.0f);
                            pv.color = glm::vec4(emitColor * 1.6f, 1.0f - progress);
                            pv.size = emitSize * (1.0f - progress * 0.5f);
                        } else if (pType == 1) {
                            const glm::vec3 dir(std::sin(seed2 * 6.28f) * 0.5f, 1.0f, std::cos(seed3 * 6.28f) * 0.5f);
                            pv.pos = basePos + dir * (age * emitSpeed * 0.6f);
                            pv.color = glm::vec4(emitColor * 0.6f, std::sin(progress * 3.14159f) * 0.35f);
                            pv.size = emitSize * (1.0f + progress * 2.5f);
                        } else if (pType == 2) {
                            const float angle = curTime * emitSpeed * 2.0f + seed * 6.283185f;
                            const float radius = 0.8f + 0.3f * std::sin(angle * 2.0f);
                            pv.pos = basePos + glm::vec3(std::cos(angle) * radius, (seed2 - 0.5f) * 0.5f + std::sin(angle * 3.0f) * 0.2f, std::sin(angle) * radius);
                            pv.color = glm::vec4(emitColor * 1.4f, 0.85f);
                            pv.size = emitSize * (0.8f + 0.4f * std::sin(curTime * 5.0f + seed));
                        } else if (pType == 3) {
                            const float phi = seed2 * 6.283185f;
                            const float theta = seed3 * 3.14159f;
                            const glm::vec3 dir(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
                            pv.pos = basePos + dir * (progress * emitSpeed * 2.5f);
                            pv.color = glm::vec4(glm::mix(emitColor * 2.2f, glm::vec3(0.2f, 0.05f, 0.0f), progress), std::max(0.0f, 1.0f - progress));
                            pv.size = emitSize * (1.5f - progress);
                        } else {
                            const float phi = seed2 * 6.283185f;
                            const float r = (1.0f - progress) * 0.3f;
                            pv.pos = basePos + glm::vec3(std::cos(phi) * r, age * emitSpeed, std::sin(phi) * r);
                            pv.color = glm::vec4(glm::mix(emitColor * 1.8f, glm::vec3(0.8f, 0.1f, 0.0f), progress), 1.0f - progress);
                            pv.size = emitSize * (1.0f - progress * 0.6f);
                        }
                        particles.push_back(pv);
                    }
                }

                if (!particles.empty()) {
                    std::memcpy(m_particleMappedPtr, particles.data(), particles.size() * sizeof(ParticleVertex));
                    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_particlePipeline);
                    vkCmdPushConstants(m_commandBuffer, m_particlePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &vp);
                    VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &m_particleVertexBuffer, &offset);
                    vkCmdDraw(m_commandBuffer, static_cast<uint32_t>(particles.size()), 1, 0, 0);
                }
            }

            vkCmdEndRendering(m_commandBuffer);
        }

        if (m_timestampQueryPool)
            vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, 6);

        // =========================================================================
        // PASS 4: 5-MIP JIMENEZ BLOOM PYRAMID (13-Tap Karis Downsample + 3x3 Tent Up)
        // =========================================================================
        transitionImage(m_commandBuffer, m_hdrImage,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        if (m_bloomEnabled && m_bloomDownPipeline && m_bloomUpPipeline && m_activeBloomMips > 0) {
            // Progresywny Downsampling: HDR -> Mip0 (Karis Average) -> Mip1 -> ... -> Mip(N-1)
            for (uint32_t m = 0; m < m_activeBloomMips; ++m) {
                transitionImage(m_commandBuffer, m_bloomImage,
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, m, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

                const VkExtent2D dstExt = m_bloomMipExtents[m];
                const VkExtent2D srcExt = (m == 0) ? VkExtent2D{ m_width, m_height } : m_bloomMipExtents[m - 1];

                VkRenderingAttachmentInfo bAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
                bAtt.imageView = m_bloomMipViews[m];
                bAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                bAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                bAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                VkRenderingInfo bRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
                bRi.renderArea = { {0, 0}, dstExt };
                bRi.layerCount = 1;
                bRi.colorAttachmentCount = 1;
                bRi.pColorAttachments = &bAtt;

                vkCmdBeginRendering(m_commandBuffer, &bRi);
                VkViewport bVp{ 0.0f, 0.0f, static_cast<float>(dstExt.width), static_cast<float>(dstExt.height), 0.0f, 1.0f };
                VkRect2D bSc{ {0, 0}, dstExt };
                vkCmdSetViewport(m_commandBuffer, 0, 1, &bVp);
                vkCmdSetScissor(m_commandBuffer, 0, 1, &bSc);
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomDownPipeline);

                VkDescriptorSet srcDs = (m == 0) ? m_bloomDownHdrDescSet : m_bloomMipDescSets[m - 1];
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomPipelineLayout, 0, 1, &srcDs, 0, nullptr);

                const glm::vec4 downParams(
                    1.0f / static_cast<float>(std::max(1u, srcExt.width)),
                    1.0f / static_cast<float>(std::max(1u, srcExt.height)),
                    m_bloomThreshold,
                    static_cast<float>(m)
                );
                vkCmdPushConstants(m_commandBuffer, m_bloomPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), &downParams);
                vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
                vkCmdEndRendering(m_commandBuffer);

                transitionImage(m_commandBuffer, m_bloomImage,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, m, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            }

            // Progresywny addytywny Upsampling 3x3 Tent: Mip(N-1) -> ... -> Mip1 -> Mip0
            for (int m = static_cast<int>(m_activeBloomMips) - 2; m >= 0; --m) {
                transitionImage(m_commandBuffer, m_bloomImage,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(m), 1, 0, 1,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

                const VkExtent2D dstExt = m_bloomMipExtents[m];
                const VkExtent2D srcExt = m_bloomMipExtents[m + 1];

                VkRenderingAttachmentInfo uAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
                uAtt.imageView = m_bloomMipViews[m];
                uAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                uAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Kumulacja addytywna (ONE + ONE)
                uAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                VkRenderingInfo uRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
                uRi.renderArea = { {0, 0}, dstExt };
                uRi.layerCount = 1;
                uRi.colorAttachmentCount = 1;
                uRi.pColorAttachments = &uAtt;

                vkCmdBeginRendering(m_commandBuffer, &uRi);
                VkViewport uVp{ 0.0f, 0.0f, static_cast<float>(dstExt.width), static_cast<float>(dstExt.height), 0.0f, 1.0f };
                VkRect2D uSc{ {0, 0}, dstExt };
                vkCmdSetViewport(m_commandBuffer, 0, 1, &uVp);
                vkCmdSetScissor(m_commandBuffer, 0, 1, &uSc);
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomUpPipeline);
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomPipelineLayout, 0, 1, &m_bloomMipDescSets[m + 1], 0, nullptr);

                const glm::vec4 upParams(
                    1.0f / static_cast<float>(std::max(1u, srcExt.width)),
                    1.0f / static_cast<float>(std::max(1u, srcExt.height)),
                    1.0f,
                    0.85f
                );
                vkCmdPushConstants(m_commandBuffer, m_bloomPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), &upParams);
                vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
                vkCmdEndRendering(m_commandBuffer);

                transitionImage(m_commandBuffer, m_bloomImage,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(m), 1, 0, 1,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            }
        }

        // =========================================================================
        // PASS 5: ACES RRT+ODT COMPOSITE + DEDICATED FXAA 3.11 PASS
        // =========================================================================
        const bool useFxaa = (m_antiAliasingMode > 0) && (m_fxaaPipeline != VK_NULL_HANDLE);
        VkImage compositeTargetImg = useFxaa ? m_ldrTempImage : m_colorImage;
        VkImageView compositeTargetView = useFxaa ? m_ldrTempImageView : m_colorImageView;

        transitionImage(m_commandBuffer, compositeTargetImg,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        {
            VkRenderingAttachmentInfo compAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            compAtt.imageView = compositeTargetView;
            compAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            compAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            compAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo compRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            compRi.renderArea = { {0, 0}, {m_width, m_height} };
            compRi.layerCount = 1;
            compRi.colorAttachmentCount = 1;
            compRi.pColorAttachments = &compAtt;

            vkCmdBeginRendering(m_commandBuffer, &compRi);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);
            if (m_compositePipeline) {
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePipeline);
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePipelineLayout, 0, 1, &m_compositeDescSet, 0, nullptr);
                struct CompositePushData {
                    glm::vec4 params;
                    glm::vec4 params2;
                } compData;
                compData.params = glm::vec4(
                    m_bloomIntensity,
                    m_exposure,
                    m_bloomEnabled ? 1.0f : 0.0f,
                    static_cast<float>(m_frameCounter)
                );
                compData.params2 = glm::vec4(useFxaa ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
                vkCmdPushConstants(m_commandBuffer, m_compositePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CompositePushData), &compData);
                vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            }
            vkCmdEndRendering(m_commandBuffer);
        }

        if (useFxaa) {
            transitionImage(m_commandBuffer, m_ldrTempImage,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

            transitionImage(m_commandBuffer, m_colorImage,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            VkRenderingAttachmentInfo fxaaAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            fxaaAtt.imageView = m_colorImageView;
            fxaaAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            fxaaAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            fxaaAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo fxaaRi{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            fxaaRi.renderArea = { {0, 0}, {m_width, m_height} };
            fxaaRi.layerCount = 1;
            fxaaRi.colorAttachmentCount = 1;
            fxaaRi.pColorAttachments = &fxaaAtt;

            vkCmdBeginRendering(m_commandBuffer, &fxaaRi);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &fullVp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &fullSc);
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fxaaPipeline);
            vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fxaaPipelineLayout, 0, 1, &m_fxaaDescSet, 0, nullptr);

            const float subpixQuality = (m_antiAliasingMode == 1) ? 0.50f : ((m_antiAliasingMode == 2) ? 0.75f : 0.90f);
            const float edgeThreshold = (m_antiAliasingMode == 1) ? 0.166f : ((m_antiAliasingMode == 2) ? 0.125f : 0.083f);
            const glm::vec4 fxaaParams(1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height), subpixQuality, edgeThreshold);
            vkCmdPushConstants(m_commandBuffer, m_fxaaPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), &fxaaParams);
            vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            vkCmdEndRendering(m_commandBuffer);
        }

        if (m_timestampQueryPool)
            vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, 7);

        // =========================================================================
        // PASS 6: SWAPCHAIN BLIT / IMGUI EDITOR RENDERING & PRESENT
        // =========================================================================
#ifdef USE_GLFW
        if (!m_context->isHeadless()) {
            VkImage swapchainImg = m_context->getSwapchainImages()[imageIndex];

            if (m_standaloneGameMode) {
                transitionImage(m_commandBuffer, swapchainImg,
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
                transitionImage(m_commandBuffer, m_colorImage,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

                VkImageBlit blit{};
                blit.srcOffsets[1] = { static_cast<int32_t>(m_width), static_cast<int32_t>(m_height), 1 };
                blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                blit.dstOffsets[1] = { static_cast<int32_t>(m_context->getSwapchainExtent().width), static_cast<int32_t>(m_context->getSwapchainExtent().height), 1 };
                blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                vkCmdBlitImage(m_commandBuffer, m_colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               swapchainImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

                transitionImage(m_commandBuffer, swapchainImg,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
                transitionImage(m_commandBuffer, m_colorImage,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            } else {
                transitionImage(m_commandBuffer, swapchainImg,
                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
                transitionImage(m_commandBuffer, m_colorImage,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            }

            renderImGui(m_commandBuffer, m_context->getSwapchainImageViews()[imageIndex], registry, assetManager, engine);

            transitionImage(m_commandBuffer, swapchainImg,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
        } else {
            transitionImage(m_commandBuffer, m_colorImage,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
        }
#else
        transitionImage(m_commandBuffer, m_colorImage,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
#endif

        if (m_timestampQueryPool) {
            vkCmdWriteTimestamp(m_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, 8);
        }
        vkEndCommandBuffer(m_commandBuffer);

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffer;

#ifdef USE_GLFW
        const VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        if (!m_context->isHeadless()) {
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &m_imageAvailableSemaphore;
            submitInfo.pWaitDstStageMask = waitStages;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &m_renderFinishedSemaphore;
            VkResult submitResult = VK_SUCCESS;
            VkResult res = VK_SUCCESS;
            {
                core::Profiler::ScopedSample submitPresentSample("Vulkan Submit + Present");
                submitResult = vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, m_inFlightFence);
                m_timestampQueryPending = m_timestampQueryPool && submitResult == VK_SUCCESS;

                VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
                presentInfo.waitSemaphoreCount = 1;
                presentInfo.pWaitSemaphores = &m_renderFinishedSemaphore;
                VkSwapchainKHR swapChains[] = { m_context->getSwapchain() };
                presentInfo.swapchainCount = 1;
                presentInfo.pSwapchains = swapChains;
                presentInfo.pImageIndices = &imageIndex;
                res = vkQueuePresentKHR(m_context->getGraphicsQueue(), &presentInfo);
            }
            if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_SURFACE_LOST_KHR) {
                m_context->recreateSwapchain();
            }
        } else {
            vkResetFences(m_context->getDevice(), 1, &m_headlessInFlightFence);
            const VkResult submitResult = vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, m_headlessInFlightFence);
            m_timestampQueryPending = m_timestampQueryPool && submitResult == VK_SUCCESS;
            if (submitResult != VK_SUCCESS) {
                vkDestroyFence(m_context->getDevice(), m_headlessInFlightFence, nullptr);
                VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
                fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                if (vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr, &m_headlessInFlightFence) != VK_SUCCESS)
                    throw std::runtime_error("failed to recover headless frame fence after queue submission failure");
            }
        }
#else
        vkResetFences(m_context->getDevice(), 1, &m_headlessInFlightFence);
        const VkResult submitResult = vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, m_headlessInFlightFence);
        m_timestampQueryPending = m_timestampQueryPool && submitResult == VK_SUCCESS;
        if (submitResult != VK_SUCCESS) {
            vkDestroyFence(m_context->getDevice(), m_headlessInFlightFence, nullptr);
            VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            if (vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr, &m_headlessInFlightFence) != VK_SUCCESS)
                throw std::runtime_error("failed to recover headless frame fence after queue submission failure");
        }
#endif
    }
}