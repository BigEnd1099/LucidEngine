#include <quickjs.h>
#include "script/QuickJSContext.hpp"
#include "core/Engine.hpp"
#include "core/Logger.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Components.hpp"
#include "physics/PhysicsContext.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Math/Quat.h>
#include <chrono>
#include <cmath>

namespace engine::script {
    QuickJSContext::QuickJSContext() {
        m_rt = JS_NewRuntime();
        m_ctx = JS_NewContext(m_rt);

        JS_SetInterruptHandler(m_rt, [](JSRuntime* rt, void* opaque) -> int {
            auto* self = static_cast<QuickJSContext*>(opaque);
            return self->m_interrupted ? 1 : 0;
        }, this);
    }

    QuickJSContext::~QuickJSContext() {
        if (m_ctx) JS_FreeContext(m_ctx);
        if (m_rt) JS_FreeRuntime(m_rt);
    }

    void QuickJSContext::setMemoryLimit(size_t bytes) {
        JS_SetMemoryLimit(m_rt, bytes);
    }

    void QuickJSContext::interrupt() {
        m_interrupted = true;
    }

    void QuickJSContext::resetInterrupt() {
        m_interrupted = false;
    }

    static JSValue js_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc >= 1) {
            const char* str = JS_ToCString(ctx, argv[0]);
            if (str) {
                core::Logger::info(std::string("JS: ") + str);
                JS_FreeCString(ctx, str);
            }
        }
        return JS_UNDEFINED;
    }

    static JSValue js_getTime(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        static auto startTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - startTime).count();
        return JS_NewFloat64(ctx, elapsed);
    }

    static JSValue js_spawnEntity(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 1) return JS_NewInt32(ctx, -1);
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (!engine) return JS_NewInt32(ctx, -1);

        JSValue jsonStrVal = JS_JSONStringify(ctx, argv[0], JS_UNDEFINED, JS_UNDEFINED);
        if (JS_IsException(jsonStrVal)) return JS_NewInt32(ctx, -1);
        const char* str = JS_ToCString(ctx, jsonStrVal);
        if (!str) {
            JS_FreeValue(ctx, jsonStrVal);
            return JS_NewInt32(ctx, -1);
        }

        try {
            nlohmann::json compJson = nlohmann::json::parse(str);
            uint32_t id = engine->spawnEntityFromJson(compJson);
            JS_FreeCString(ctx, str);
            JS_FreeValue(ctx, jsonStrVal);
            return JS_NewUint32(ctx, id);
        } catch (...) {
            JS_FreeCString(ctx, str);
            JS_FreeValue(ctx, jsonStrVal);
            return JS_NewInt32(ctx, -1);
        }
    }

    static JSValue js_destroyEntity(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 1) return JS_FALSE;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (!engine) return JS_FALSE;

        uint32_t id = 0;
        if (JS_ToUint32(ctx, &id, argv[0]) == 0) {
            bool ok = engine->destroyEntity(id);
            return JS_NewBool(ctx, ok);
        }
        return JS_FALSE;
    }

    static JSValue js_applyImpulse(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 4) return JS_FALSE;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (!engine || !engine->getPhysicsContext() || !engine->getRegistry()) return JS_FALSE;

        uint32_t id = 0;
        double ix = 0, iy = 0, iz = 0;
        if (JS_ToUint32(ctx, &id, argv[0]) == 0 &&
            JS_ToFloat64(ctx, &ix, argv[1]) == 0 &&
            JS_ToFloat64(ctx, &iy, argv[2]) == 0 &&
            JS_ToFloat64(ctx, &iz, argv[3]) == 0) {
            auto entity = static_cast<entt::entity>(id);
            if (engine->getRegistry()->get().valid(entity) &&
                engine->getRegistry()->get().all_of<ecs::RigidBodyComponent>(entity)) {
                auto& rb = engine->getRegistry()->get().get<ecs::RigidBodyComponent>(entity);
                if (rb.bodyId != 0xFFFFFFFF) {
                    JPH::BodyID bId(rb.bodyId);
                    engine->getPhysicsContext()->getBodyInterface().AddImpulse(bId, JPH::Vec3(static_cast<float>(ix), static_cast<float>(iy), static_cast<float>(iz)));
                    return JS_TRUE;
                }
            }
        }
        return JS_FALSE;
    }

    static JSValue js_setVelocity(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 4) return JS_FALSE;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (!engine || !engine->getRegistry()) return JS_FALSE;

        uint32_t id = 0;
        double vx = 0, vy = 0, vz = 0;
        if (JS_ToUint32(ctx, &id, argv[0]) == 0 &&
            JS_ToFloat64(ctx, &vx, argv[1]) == 0 &&
            JS_ToFloat64(ctx, &vy, argv[2]) == 0 &&
            JS_ToFloat64(ctx, &vz, argv[3]) == 0) {
            auto entity = static_cast<entt::entity>(id);
            if (engine->getRegistry()->get().valid(entity)) {
                if (engine->getRegistry()->get().all_of<ecs::VelocityComponent>(entity)) {
                    auto& v = engine->getRegistry()->get().get<ecs::VelocityComponent>(entity);
                    v.velocity = glm::vec3(static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz));
                }
                if (engine->getPhysicsContext() && engine->getRegistry()->get().all_of<ecs::RigidBodyComponent>(entity)) {
                    auto& rb = engine->getRegistry()->get().get<ecs::RigidBodyComponent>(entity);
                    if (rb.bodyId != 0xFFFFFFFF) {
                        JPH::BodyID bId(rb.bodyId);
                        engine->getPhysicsContext()->getBodyInterface().SetLinearVelocity(bId, JPH::Vec3(static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz)));
                    }
                }
                return JS_TRUE;
            }
        }
        return JS_FALSE;
    }

    static JSValue js_setScore(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 1) return JS_UNDEFINED;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            int32_t s = 0;
            if (JS_ToInt32(ctx, &s, argv[0]) == 0) {
                engine->setScore(s);
            }
        }
        return JS_UNDEFINED;
    }

    static JSValue js_getScore(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        int s = engine ? engine->getScore() : 0;
        return JS_NewInt32(ctx, s);
    }

    static JSValue js_setGameState(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 1) return JS_UNDEFINED;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            JSValue jsonStrVal = JS_JSONStringify(ctx, argv[0], JS_UNDEFINED, JS_UNDEFINED);
            if (!JS_IsException(jsonStrVal)) {
                const char* str = JS_ToCString(ctx, jsonStrVal);
                if (str) {
                    try {
                        engine->setGameState(nlohmann::json::parse(str));
                    } catch (...) {}
                    JS_FreeCString(ctx, str);
                }
                JS_FreeValue(ctx, jsonStrVal);
            }
        }
        return JS_UNDEFINED;
    }

    static JSValue js_getGameState(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (!engine) return JS_NewObject(ctx);
        std::string s = engine->getGameState().dump();
        JSValue val = JS_ParseJSON(ctx, s.c_str(), s.length(), "<gameState>");
        return JS_IsException(val) ? JS_NewObject(ctx) : val;
    }

    static JSValue js_playSfx(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 1) return JS_UNDEFINED;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            const char* str = JS_ToCString(ctx, argv[0]);
            if (str) {
                double vol = 1.0;
                if (argc >= 2) JS_ToFloat64(ctx, &vol, argv[1]);
                engine->broadcastSfx(str, static_cast<float>(vol));
                JS_FreeCString(ctx, str);
            }
        }
        return JS_UNDEFINED;
    }

    static JSValue js_shakeCamera(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            double intensity = 0.5, duration = 0.3;
            if (argc >= 1) JS_ToFloat64(ctx, &intensity, argv[0]);
            if (argc >= 2) JS_ToFloat64(ctx, &duration, argv[1]);
            engine->broadcastCameraShake(static_cast<float>(intensity), static_cast<float>(duration));
        }
        return JS_UNDEFINED;
    }

    static JSValue js_setHealth(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 1) return JS_UNDEFINED;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            int32_t hp = 100;
            if (JS_ToInt32(ctx, &hp, argv[0]) == 0) {
                engine->setHealth(hp);
            }
        }
        return JS_UNDEFINED;
    }

    static JSValue js_getHealth(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        int hp = engine ? engine->getHealth() : 100;
        return JS_NewInt32(ctx, hp);
    }

    static JSValue js_setGameStatus(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 1) return JS_UNDEFINED;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            const char* str = JS_ToCString(ctx, argv[0]);
            if (str) {
                engine->setGameStatus(str);
                JS_FreeCString(ctx, str);
            }
        }
        return JS_UNDEFINED;
    }

    static JSValue js_getGameStatus(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        std::string st = engine ? engine->getGameStatus() : "PLAYING";
        return JS_NewString(ctx, st.c_str());
    }

    static JSValue js_detonateExplosion(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 4) return JS_UNDEFINED;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            double x = 0, y = 0, z = 0, radius = 5.0, force = 50.0, damage = 100.0;
            JS_ToFloat64(ctx, &x, argv[0]);
            JS_ToFloat64(ctx, &y, argv[1]);
            JS_ToFloat64(ctx, &z, argv[2]);
            if (argc >= 4) JS_ToFloat64(ctx, &radius, argv[3]);
            if (argc >= 5) JS_ToFloat64(ctx, &force, argv[4]);
            if (argc >= 6) JS_ToFloat64(ctx, &damage, argv[5]);
            engine->detonateExplosion(glm::vec3(x, y, z), static_cast<float>(radius), static_cast<float>(force), static_cast<float>(damage));
        }
        return JS_UNDEFINED;
    }

    static JSValue js_fractureEntity(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 1) return JS_UNDEFINED;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            uint32_t id = 0;
            if (JS_ToUint32(ctx, &id, argv[0]) == 0) {
                engine->fractureEntity(id);
            }
        }
        return JS_UNDEFINED;
    }

    static JSValue js_ignite(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 1) return JS_UNDEFINED;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            uint32_t id = 0;
            double temp = 400.0;
            if (JS_ToUint32(ctx, &id, argv[0]) == 0) {
                if (argc >= 2) JS_ToFloat64(ctx, &temp, argv[1]);
                engine->igniteEntity(id, static_cast<float>(temp));
            }
        }
        return JS_UNDEFINED;
    }

    static JSValue js_extinguish(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 1) return JS_UNDEFINED;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            uint32_t id = 0;
            if (JS_ToUint32(ctx, &id, argv[0]) == 0) {
                engine->extinguishEntity(id);
            }
        }
        return JS_UNDEFINED;
    }

    static JSValue js_electrify(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 1) return JS_UNDEFINED;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            uint32_t id = 0;
            double v = 100.0;
            if (JS_ToUint32(ctx, &id, argv[0]) == 0) {
                if (argc >= 2) JS_ToFloat64(ctx, &v, argv[1]);
                engine->electrifyEntity(id, static_cast<float>(v));
            }
        }
        return JS_UNDEFINED;
    }

    static JSValue js_freeze(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 1) return JS_UNDEFINED;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            uint32_t id = 0;
            if (JS_ToUint32(ctx, &id, argv[0]) == 0) {
                engine->freezeEntity(id);
            }
        }
        return JS_UNDEFINED;
    }

    static JSValue js_findPath(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 6) return JS_NewArray(ctx);
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (!engine) return JS_NewArray(ctx);

        double sx = 0, sy = 0, sz = 0, tx = 0, ty = 0, tz = 0;
        JS_ToFloat64(ctx, &sx, argv[0]);
        JS_ToFloat64(ctx, &sy, argv[1]);
        JS_ToFloat64(ctx, &sz, argv[2]);
        JS_ToFloat64(ctx, &tx, argv[3]);
        JS_ToFloat64(ctx, &ty, argv[4]);
        JS_ToFloat64(ctx, &tz, argv[5]);

        auto waypoints = engine->findPath(glm::vec3(sx, sy, sz), glm::vec3(tx, ty, tz));
        JSValue arr = JS_NewArray(ctx);
        for (uint32_t i = 0; i < waypoints.size(); ++i) {
            JSValue p = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, p, "x", JS_NewFloat64(ctx, waypoints[i].x));
            JS_SetPropertyStr(ctx, p, "y", JS_NewFloat64(ctx, waypoints[i].y));
            JS_SetPropertyStr(ctx, p, "z", JS_NewFloat64(ctx, waypoints[i].z));
            JS_SetPropertyUint32(ctx, arr, i, p);
        }
        return arr;
    }

    static JSValue js_generateProceduralWorld(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            double areaSize = 120.0, resolution = 4.0;
            if (argc >= 1) JS_ToFloat64(ctx, &areaSize, argv[0]);
            if (argc >= 2) JS_ToFloat64(ctx, &resolution, argv[1]);
            engine->generateProceduralWorld(static_cast<float>(areaSize), static_cast<float>(resolution));
        }
        return JS_UNDEFINED;
    }

    static JSValue js_getProfilerStats(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (!engine) return JS_NewObject(ctx);
        std::string s = engine->getProfilerStats().dump();
        JSValue val = JS_ParseJSON(ctx, s.c_str(), s.length(), "<profilerStats>");
        return JS_IsException(val) ? JS_NewObject(ctx) : val;
    }

    static JSValue js_getCamera(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (!engine) return JS_NewObject(ctx);
        JSValue camObj = JS_NewObject(ctx);
        glm::vec3 pos = engine->getCameraPosition();
        JS_SetPropertyStr(ctx, camObj, "x", JS_NewFloat64(ctx, pos.x));
        JS_SetPropertyStr(ctx, camObj, "y", JS_NewFloat64(ctx, pos.y));
        JS_SetPropertyStr(ctx, camObj, "z", JS_NewFloat64(ctx, pos.z));
        JS_SetPropertyStr(ctx, camObj, "yaw", JS_NewFloat64(ctx, engine->getCameraYaw()));
        JS_SetPropertyStr(ctx, camObj, "pitch", JS_NewFloat64(ctx, engine->getCameraPitch()));
        JS_SetPropertyStr(ctx, camObj, "fov", JS_NewFloat64(ctx, engine->getCameraFov()));
        JS_SetPropertyStr(ctx, camObj, "mode", JS_NewString(ctx, engine->getCameraMode().c_str()));
        return camObj;
    }

    static JSValue js_setCamera(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 3) return JS_UNDEFINED;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            double x = 0, y = 0, z = 0;
            double yaw = engine->getCameraYaw();
            double pitch = engine->getCameraPitch();
            double fov = engine->getCameraFov();
            JS_ToFloat64(ctx, &x, argv[0]);
            JS_ToFloat64(ctx, &y, argv[1]);
            JS_ToFloat64(ctx, &z, argv[2]);
            if (argc >= 4) JS_ToFloat64(ctx, &yaw, argv[3]);
            if (argc >= 5) JS_ToFloat64(ctx, &pitch, argv[4]);
            if (argc >= 6) JS_ToFloat64(ctx, &fov, argv[5]);
            engine->setCamera(glm::vec3(x, y, z), static_cast<float>(yaw), static_cast<float>(pitch), static_cast<float>(fov));
        }
        return JS_UNDEFINED;
    }

    static JSValue js_restart(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            engine->restartSimulation();
        }
        return JS_UNDEFINED;
    }

    static JSValue js_setCameraMode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc < 1) return JS_UNDEFINED;
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        if (engine) {
            const char* str = JS_ToCString(ctx, argv[0]);
            if (str) {
                engine->setCameraMode(str);
                JS_FreeCString(ctx, str);
            }
        }
        return JS_UNDEFINED;
    }

    static JSValue js_getCameraMode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(ctx));
        std::string m = engine ? engine->getCameraMode() : "editor";
        return JS_NewString(ctx, m.c_str());
    }

    void QuickJSContext::bindEngineAPI(JSContext* ctx, core::Engine* engineInstance) {
        JS_SetContextOpaque(ctx, engineInstance);
        JSValue global_obj = JS_GetGlobalObject(ctx);
        JSValue engine_obj = JS_NewObject(ctx);

        JS_SetPropertyStr(ctx, engine_obj, "log", JS_NewCFunction(ctx, js_log, "log", 1));
        JS_SetPropertyStr(ctx, engine_obj, "getTime", JS_NewCFunction(ctx, js_getTime, "getTime", 0));
        JS_SetPropertyStr(ctx, engine_obj, "spawnEntity", JS_NewCFunction(ctx, js_spawnEntity, "spawnEntity", 1));
        JS_SetPropertyStr(ctx, engine_obj, "destroyEntity", JS_NewCFunction(ctx, js_destroyEntity, "destroyEntity", 1));
        JS_SetPropertyStr(ctx, engine_obj, "applyImpulse", JS_NewCFunction(ctx, js_applyImpulse, "applyImpulse", 4));
        JS_SetPropertyStr(ctx, engine_obj, "setVelocity", JS_NewCFunction(ctx, js_setVelocity, "setVelocity", 4));
        JS_SetPropertyStr(ctx, engine_obj, "setScore", JS_NewCFunction(ctx, js_setScore, "setScore", 1));
        JS_SetPropertyStr(ctx, engine_obj, "getScore", JS_NewCFunction(ctx, js_getScore, "getScore", 0));
        JS_SetPropertyStr(ctx, engine_obj, "setHealth", JS_NewCFunction(ctx, js_setHealth, "setHealth", 1));
        JS_SetPropertyStr(ctx, engine_obj, "getHealth", JS_NewCFunction(ctx, js_getHealth, "getHealth", 0));
        JS_SetPropertyStr(ctx, engine_obj, "setGameStatus", JS_NewCFunction(ctx, js_setGameStatus, "setGameStatus", 1));
        JS_SetPropertyStr(ctx, engine_obj, "getGameStatus", JS_NewCFunction(ctx, js_getGameStatus, "getGameStatus", 0));
        JS_SetPropertyStr(ctx, engine_obj, "playSfx", JS_NewCFunction(ctx, js_playSfx, "playSfx", 2));
        JS_SetPropertyStr(ctx, engine_obj, "shakeCamera", JS_NewCFunction(ctx, js_shakeCamera, "shakeCamera", 2));
        JS_SetPropertyStr(ctx, engine_obj, "setGameState", JS_NewCFunction(ctx, js_setGameState, "setGameState", 1));
        JS_SetPropertyStr(ctx, engine_obj, "getGameState", JS_NewCFunction(ctx, js_getGameState, "getGameState", 0));
        JS_SetPropertyStr(ctx, engine_obj, "detonateExplosion", JS_NewCFunction(ctx, js_detonateExplosion, "detonateExplosion", 6));
        JS_SetPropertyStr(ctx, engine_obj, "fractureEntity", JS_NewCFunction(ctx, js_fractureEntity, "fractureEntity", 1));
        JS_SetPropertyStr(ctx, engine_obj, "ignite", JS_NewCFunction(ctx, js_ignite, "ignite", 2));
        JS_SetPropertyStr(ctx, engine_obj, "extinguish", JS_NewCFunction(ctx, js_extinguish, "extinguish", 1));
        JS_SetPropertyStr(ctx, engine_obj, "electrify", JS_NewCFunction(ctx, js_electrify, "electrify", 2));
        JS_SetPropertyStr(ctx, engine_obj, "freeze", JS_NewCFunction(ctx, js_freeze, "freeze", 1));
        JS_SetPropertyStr(ctx, engine_obj, "findPath", JS_NewCFunction(ctx, js_findPath, "findPath", 6));
        JS_SetPropertyStr(ctx, engine_obj, "generateProceduralWorld", JS_NewCFunction(ctx, js_generateProceduralWorld, "generateProceduralWorld", 2));
        JS_SetPropertyStr(ctx, engine_obj, "getProfilerStats", JS_NewCFunction(ctx, js_getProfilerStats, "getProfilerStats", 0));
        JS_SetPropertyStr(ctx, engine_obj, "getCamera", JS_NewCFunction(ctx, js_getCamera, "getCamera", 0));
        JS_SetPropertyStr(ctx, engine_obj, "setCamera", JS_NewCFunction(ctx, js_setCamera, "setCamera", 6));
        JS_SetPropertyStr(ctx, engine_obj, "restart", JS_NewCFunction(ctx, js_restart, "restart", 0));
        JS_SetPropertyStr(ctx, engine_obj, "setCameraMode", JS_NewCFunction(ctx, js_setCameraMode, "setCameraMode", 1));
        JS_SetPropertyStr(ctx, engine_obj, "getCameraMode", JS_NewCFunction(ctx, js_getCameraMode, "getCameraMode", 0));

        JS_SetPropertyStr(ctx, global_obj, "engine", engine_obj);
        JS_FreeValue(ctx, global_obj);
    }

    void QuickJSContext::init(core::Engine* engineInstance) {
        bindEngineAPI(m_ctx, engineInstance);
    }

    bool QuickJSContext::evaluate(const std::string& code, const std::string& filename) {
        resetInterrupt();
        
        std::string cleanCode = code;
        size_t pos = 0;
        while ((pos = cleanCode.find("export ")) != std::string::npos) {
            cleanCode.replace(pos, 7, "");
        }
        cleanCode += "\n; if (typeof update !== 'undefined') globalThis.update = update;";
        
        JSValue val = JS_Eval(m_ctx, cleanCode.c_str(), cleanCode.length(), filename.c_str(), JS_EVAL_TYPE_GLOBAL);
        
        if (JS_IsException(val)) {
            JSValue ex = JS_GetException(m_ctx);
            const char* str = JS_ToCString(m_ctx, ex);
            JSValue stackVal = JS_GetPropertyStr(m_ctx, ex, "stack");
            const char* stackStr = JS_ToCString(m_ctx, stackVal);
            
            std::string errStr = str ? str : "Unknown Evaluate Error";
            std::string stack = stackStr ? stackStr : "";
            
            core::Logger::error("QuickJS Evaluate Error: " + errStr + (stack.empty() ? "" : "\n" + stack));
            if (m_onError) m_onError("EvaluateError", errStr, stack);
            
            if (str) JS_FreeCString(m_ctx, str);
            if (stackStr) JS_FreeCString(m_ctx, stackStr);
            JS_FreeValue(m_ctx, stackVal);
            JS_FreeValue(m_ctx, ex);
            JS_FreeValue(m_ctx, val);
            return false;
        }
        
        JS_FreeValue(m_ctx, val);
        core::Logger::info("QuickJS: Script loaded successfully.");
        return true;
    }

    std::string QuickJSContext::tick(const std::string& stateJson, float dt, const std::string& inputJson) {
        JSValue global = JS_GetGlobalObject(m_ctx);
        JS_SetPropertyStr(m_ctx, global, "__stateJson", JS_NewString(m_ctx, stateJson.c_str()));
        JS_SetPropertyStr(m_ctx, global, "__inputJson", JS_NewString(m_ctx, inputJson.empty() ? "{}" : inputJson.c_str()));
        JS_SetPropertyStr(m_ctx, global, "__dt", JS_NewFloat64(m_ctx, dt));
        
        const char* script = 
            "globalThis.__stateError = null;\n"
            "try {\n"
            "  var ents = JSON.parse(globalThis.__stateJson);\n"
            "  var input = JSON.parse(globalThis.__inputJson || '{}');\n"
            "  globalThis.input = input;\n"
            "  if (typeof globalThis.update === 'function') {\n"
            "    globalThis.update(ents, globalThis.__dt, input);\n"
            "  }\n"
            "  globalThis.__stateResult = JSON.stringify(ents);\n"
            "} catch(e) {\n"
            "  globalThis.__stateError = { errorType: e.name || 'Error', message: e.message || '', stackTrace: e.stack || '' };\n"
            "}\n";
            
        JSValue ret = JS_Eval(m_ctx, script, strlen(script), "tick.js", JS_EVAL_TYPE_GLOBAL);
        JS_FreeValue(m_ctx, ret);
        
        JSValue err = JS_GetPropertyStr(m_ctx, global, "__stateError");
        if (!JS_IsNull(err) && !JS_IsUndefined(err)) {
            if (m_onError) {
                JSValue errType = JS_GetPropertyStr(m_ctx, err, "errorType");
                JSValue errMsg = JS_GetPropertyStr(m_ctx, err, "message");
                JSValue errStack = JS_GetPropertyStr(m_ctx, err, "stackTrace");
                
                const char* typeStr = JS_ToCString(m_ctx, errType);
                const char* msgStr = JS_ToCString(m_ctx, errMsg);
                const char* stackStr = JS_ToCString(m_ctx, errStack);
                
                m_onError(typeStr ? typeStr : "Error", msgStr ? msgStr : "Unknown error", stackStr ? stackStr : "");
                
                if (typeStr) JS_FreeCString(m_ctx, typeStr);
                if (msgStr) JS_FreeCString(m_ctx, msgStr);
                if (stackStr) JS_FreeCString(m_ctx, stackStr);
                
                JS_FreeValue(m_ctx, errType);
                JS_FreeValue(m_ctx, errMsg);
                JS_FreeValue(m_ctx, errStack);
            }
            JS_FreeValue(m_ctx, err);
            JS_FreeValue(m_ctx, global);
            return "";
        }
        JS_FreeValue(m_ctx, err);
        
        JSValue res = JS_GetPropertyStr(m_ctx, global, "__stateResult");
        const char* resStr = JS_ToCString(m_ctx, res);
        std::string finalJson = resStr ? resStr : "";
        if (resStr) JS_FreeCString(m_ctx, resStr);
        JS_FreeValue(m_ctx, res);
        
        JS_FreeValue(m_ctx, global);
        return finalJson;
    }

    void QuickJSContext::tickDirect(engine::ecs::Registry* registry, engine::physics::PhysicsContext* physics, float dt, const nlohmann::json& inputState, const std::vector<engine::physics::ContactPoint>& contacts) {
        if (!m_ctx || !registry) return;

        JSValue global = JS_GetGlobalObject(m_ctx);
        JSValue updateFunc = JS_GetPropertyStr(m_ctx, global, "update");

        if (!JS_IsFunction(m_ctx, updateFunc)) {
            JS_FreeValue(m_ctx, updateFunc);
            JS_FreeValue(m_ctx, global);
            return;
        }

        // 1. Build entities array directly in JS
        JSValue entitiesArr = JS_NewArray(m_ctx);
        auto view = registry->get().view<ecs::TransformComponent>();
        uint32_t arrIdx = 0;

        for (auto entity : view) {
            const auto& t = view.get<ecs::TransformComponent>(entity);
            JSValue entObj = JS_NewObject(m_ctx);
            uint32_t eId = static_cast<uint32_t>(entity);
            JS_SetPropertyStr(m_ctx, entObj, "id", JS_NewUint32(m_ctx, eId));

            if (registry->get().all_of<ecs::TagComponent>(entity)) {
                JS_SetPropertyStr(m_ctx, entObj, "tag", JS_NewString(m_ctx, registry->get().get<ecs::TagComponent>(entity).name.c_str()));
            }

            JSValue transObj = JS_NewObject(m_ctx);
            JS_SetPropertyStr(m_ctx, transObj, "x", JS_NewFloat64(m_ctx, t.position.x));
            JS_SetPropertyStr(m_ctx, transObj, "y", JS_NewFloat64(m_ctx, t.position.y));
            JS_SetPropertyStr(m_ctx, transObj, "z", JS_NewFloat64(m_ctx, t.position.z));
            JS_SetPropertyStr(m_ctx, transObj, "rx", JS_NewFloat64(m_ctx, t.rotation.x));
            JS_SetPropertyStr(m_ctx, transObj, "ry", JS_NewFloat64(m_ctx, t.rotation.y));
            JS_SetPropertyStr(m_ctx, transObj, "rz", JS_NewFloat64(m_ctx, t.rotation.z));
            JS_SetPropertyStr(m_ctx, transObj, "sx", JS_NewFloat64(m_ctx, t.scale.x));
            JS_SetPropertyStr(m_ctx, transObj, "sy", JS_NewFloat64(m_ctx, t.scale.y));
            JS_SetPropertyStr(m_ctx, transObj, "sz", JS_NewFloat64(m_ctx, t.scale.z));
            JS_SetPropertyStr(m_ctx, entObj, "transform", transObj);

            if (registry->get().all_of<ecs::VelocityComponent>(entity)) {
                const auto& v = registry->get().get<ecs::VelocityComponent>(entity);
                JSValue velObj = JS_NewObject(m_ctx);
                JS_SetPropertyStr(m_ctx, velObj, "vx", JS_NewFloat64(m_ctx, v.velocity.x));
                JS_SetPropertyStr(m_ctx, velObj, "vy", JS_NewFloat64(m_ctx, v.velocity.y));
                JS_SetPropertyStr(m_ctx, velObj, "vz", JS_NewFloat64(m_ctx, v.velocity.z));
                JS_SetPropertyStr(m_ctx, entObj, "velocity", velObj);
            }

            if (registry->get().all_of<ecs::RigidBodyComponent>(entity)) {
                const auto& rb = registry->get().get<ecs::RigidBodyComponent>(entity);
                JSValue rbObj = JS_NewObject(m_ctx);
                JS_SetPropertyStr(m_ctx, rbObj, "mass", JS_NewFloat64(m_ctx, rb.mass));
                JS_SetPropertyStr(m_ctx, rbObj, "shapeType", JS_NewInt32(m_ctx, rb.shapeType));
                JS_SetPropertyStr(m_ctx, rbObj, "restitution", JS_NewFloat64(m_ctx, rb.restitution));
                JS_SetPropertyStr(m_ctx, rbObj, "friction", JS_NewFloat64(m_ctx, rb.friction));
                JS_SetPropertyStr(m_ctx, rbObj, "capsuleRadius", JS_NewFloat64(m_ctx, rb.capsuleRadius));
                JS_SetPropertyStr(m_ctx, rbObj, "capsuleHalfHeight", JS_NewFloat64(m_ctx, rb.capsuleHalfHeight));
                JS_SetPropertyStr(m_ctx, rbObj, "lockRotationX", JS_NewBool(m_ctx, rb.lockRotationX));
                JS_SetPropertyStr(m_ctx, rbObj, "lockRotationY", JS_NewBool(m_ctx, rb.lockRotationY));
                JS_SetPropertyStr(m_ctx, rbObj, "lockRotationZ", JS_NewBool(m_ctx, rb.lockRotationZ));
                JS_SetPropertyStr(m_ctx, rbObj, "isSensor", JS_NewBool(m_ctx, rb.isSensor));
                JS_SetPropertyStr(m_ctx, entObj, "rigidBody", rbObj);
            }

            if (registry->get().all_of<ecs::MeshComponent>(entity)) {
                const auto& m = registry->get().get<ecs::MeshComponent>(entity);
                JSValue meshObj = JS_NewObject(m_ctx);
                JS_SetPropertyStr(m_ctx, meshObj, "meshId", JS_NewUint32(m_ctx, m.meshId));
                JS_SetPropertyStr(m_ctx, meshObj, "materialId", JS_NewUint32(m_ctx, m.materialId));
                JS_SetPropertyStr(m_ctx, meshObj, "visible", JS_NewBool(m_ctx, m.visible));
                JS_SetPropertyStr(m_ctx, entObj, "mesh", meshObj);
            }

            JS_SetPropertyUint32(m_ctx, entitiesArr, arrIdx++, entObj);
        }

        // 2. Prepare arguments: (entities, dt, input, contacts)
        JSValue dtVal = JS_NewFloat64(m_ctx, dt);
        
        std::string inputStr = inputState.empty() ? "{}" : inputState.dump();
        JSValue inputVal = JS_ParseJSON(m_ctx, inputStr.c_str(), inputStr.length(), "<input>");
        if (JS_IsException(inputVal)) {
            inputVal = JS_NewObject(m_ctx);
        }

        JS_SetPropertyStr(m_ctx, global, "input", JS_DupValue(m_ctx, inputVal));

        JSValue contactsArr = JS_NewArray(m_ctx);
        for (uint32_t cIdx = 0; cIdx < contacts.size(); ++cIdx) {
            const auto& c = contacts[cIdx];
            JSValue cObj = JS_NewObject(m_ctx);
            JS_SetPropertyStr(m_ctx, cObj, "a", JS_NewUint32(m_ctx, c.entity1));
            JS_SetPropertyStr(m_ctx, cObj, "b", JS_NewUint32(m_ctx, c.entity2));
            JSValue normObj = JS_NewObject(m_ctx);
            JS_SetPropertyStr(m_ctx, normObj, "x", JS_NewFloat64(m_ctx, c.normalX));
            JS_SetPropertyStr(m_ctx, normObj, "y", JS_NewFloat64(m_ctx, c.normalY));
            JS_SetPropertyStr(m_ctx, normObj, "z", JS_NewFloat64(m_ctx, c.normalZ));
            JS_SetPropertyStr(m_ctx, cObj, "normal", normObj);
            JS_SetPropertyUint32(m_ctx, contactsArr, cIdx, cObj);
        }

        JSValue args[4] = { entitiesArr, dtVal, inputVal, contactsArr };
        JSValue ret = JS_Call(m_ctx, updateFunc, global, 4, args);

        JS_FreeValue(m_ctx, contactsArr);
        JS_FreeValue(m_ctx, dtVal);
        JS_FreeValue(m_ctx, inputVal);
        JS_FreeValue(m_ctx, updateFunc);

        // 3. Check for runtime errors
        if (JS_IsException(ret)) {
            JSValue ex = JS_GetException(m_ctx);
            const char* str = JS_ToCString(m_ctx, ex);
            JSValue stackVal = JS_GetPropertyStr(m_ctx, ex, "stack");
            const char* stackStr = JS_ToCString(m_ctx, stackVal);

            std::string errStr = str ? str : "Script Runtime Error";
            std::string stack = stackStr ? stackStr : "";
            core::Logger::error("QuickJS Runtime Error: " + errStr + (stack.empty() ? "" : "\n" + stack));
            if (m_onError) m_onError("RuntimeError", errStr, stack);

            if (str) JS_FreeCString(m_ctx, str);
            if (stackStr) JS_FreeCString(m_ctx, stackStr);
            JS_FreeValue(m_ctx, stackVal);
            JS_FreeValue(m_ctx, ex);
            JS_FreeValue(m_ctx, ret);
            JS_FreeValue(m_ctx, entitiesArr);
            JS_FreeValue(m_ctx, global);
            return;
        }
        JS_FreeValue(m_ctx, ret);

        // 4. Fast direct sync back from JS objects to ECS components & Jolt
        auto readPropFloat = [this](JSValue obj, const char* prop, float& target, bool& changed) {
            JSValue val = JS_GetPropertyStr(m_ctx, obj, prop);
            if (!JS_IsUndefined(val)) {
                double d = 0;
                if (JS_ToFloat64(m_ctx, &d, val) == 0) {
                    float f = static_cast<float>(d);
                    if (std::abs(target - f) > 1e-5f) {
                        target = f;
                        changed = true;
                    }
                }
            }
            JS_FreeValue(m_ctx, val);
        };

        for (uint32_t i = 0; i < arrIdx; ++i) {
            JSValue entObj = JS_GetPropertyUint32(m_ctx, entitiesArr, i);
            JSValue idVal = JS_GetPropertyStr(m_ctx, entObj, "id");
            uint32_t id = 0;
            if (JS_ToUint32(m_ctx, &id, idVal) == 0) {
                auto entity = static_cast<entt::entity>(id);
                if (registry->get().valid(entity)) {
                    // Sync Transform
                    JSValue transObj = JS_GetPropertyStr(m_ctx, entObj, "transform");
                    if (JS_IsObject(transObj)) {
                        auto& t = registry->get().get<ecs::TransformComponent>(entity);
                        bool posChanged = false;
                        bool rotChanged = false;

                        readPropFloat(transObj, "x", t.position.x, posChanged);
                        readPropFloat(transObj, "y", t.position.y, posChanged);
                        readPropFloat(transObj, "z", t.position.z, posChanged);
                        readPropFloat(transObj, "rx", t.rotation.x, rotChanged);
                        readPropFloat(transObj, "ry", t.rotation.y, rotChanged);
                        readPropFloat(transObj, "rz", t.rotation.z, rotChanged);
                        bool scaleChanged = false;
                        readPropFloat(transObj, "sx", t.scale.x, scaleChanged);
                        readPropFloat(transObj, "sy", t.scale.y, scaleChanged);
                        readPropFloat(transObj, "sz", t.scale.z, scaleChanged);

                        if (scaleChanged) {
                            auto* engine = static_cast<core::Engine*>(JS_GetContextOpaque(m_ctx));
                            if (engine) engine->syncEntityTransformPhysics(entity);
                        }

                        if ((posChanged || rotChanged) && physics && registry->get().all_of<ecs::RigidBodyComponent>(entity)) {
                            auto& rb = registry->get().get<ecs::RigidBodyComponent>(entity);
                            if (rb.bodyId != 0xFFFFFFFF) {
                                JPH::BodyID bId(rb.bodyId);
                                JPH::RVec3 jPos(t.position.x, t.position.y, t.position.z);
                                JPH::Quat jRot = JPH::Quat::sEulerAngles(JPH::Vec3(
                                    glm::radians(t.rotation.x),
                                    glm::radians(t.rotation.y),
                                    glm::radians(t.rotation.z)
                                ));
                                physics->getBodyInterface().SetPositionAndRotation(bId, jPos, jRot, JPH::EActivation::Activate);
                            }
                        }
                    }
                    JS_FreeValue(m_ctx, transObj);

                    // Sync MeshComponent (visibility)
                    JSValue meshObj = JS_GetPropertyStr(m_ctx, entObj, "mesh");
                    if (JS_IsObject(meshObj) && registry->get().all_of<ecs::MeshComponent>(entity)) {
                        auto& m = registry->get().get<ecs::MeshComponent>(entity);
                        JSValue visVal = JS_GetPropertyStr(m_ctx, meshObj, "visible");
                        if (!JS_IsUndefined(visVal)) {
                            int b = JS_ToBool(m_ctx, visVal);
                            if (b != -1) {
                                m.visible = (b != 0);
                            }
                        }
                        JS_FreeValue(m_ctx, visVal);
                    }
                    JS_FreeValue(m_ctx, meshObj);

                    // Sync Velocity
                    JSValue velObj = JS_GetPropertyStr(m_ctx, entObj, "velocity");
                    if (JS_IsObject(velObj) && registry->get().all_of<ecs::VelocityComponent>(entity)) {
                        auto& v = registry->get().get<ecs::VelocityComponent>(entity);
                        bool velChanged = false;

                        readPropFloat(velObj, "vx", v.velocity.x, velChanged);
                        readPropFloat(velObj, "vy", v.velocity.y, velChanged);
                        readPropFloat(velObj, "vz", v.velocity.z, velChanged);

                        if (velChanged && physics && registry->get().all_of<ecs::RigidBodyComponent>(entity)) {
                            auto& rb = registry->get().get<ecs::RigidBodyComponent>(entity);
                            if (rb.bodyId != 0xFFFFFFFF && rb.mass > 0.0f) {
                                JPH::BodyID bId(rb.bodyId);
                                physics->getBodyInterface().SetLinearVelocity(bId, JPH::Vec3(v.velocity.x, v.velocity.y, v.velocity.z));
                            }
                        }
                    }
                    JS_FreeValue(m_ctx, velObj);

                    // Sync RigidBody properties (restitution, friction)
                    JSValue rbObj = JS_GetPropertyStr(m_ctx, entObj, "rigidBody");
                    if (JS_IsObject(rbObj) && registry->get().all_of<ecs::RigidBodyComponent>(entity)) {
                        auto& rb = registry->get().get<ecs::RigidBodyComponent>(entity);
                        JSValue restVal = JS_GetPropertyStr(m_ctx, rbObj, "restitution");
                        if (!JS_IsUndefined(restVal)) {
                            double r = 0;
                            if (JS_ToFloat64(m_ctx, &r, restVal) == 0) {
                                rb.restitution = static_cast<float>(r);
                                if (physics && rb.bodyId != 0xFFFFFFFF) {
                                    physics->getBodyInterface().SetRestitution(JPH::BodyID(rb.bodyId), rb.restitution);
                                }
                            }
                        }
                        JS_FreeValue(m_ctx, restVal);

                        JSValue frictVal = JS_GetPropertyStr(m_ctx, rbObj, "friction");
                        if (!JS_IsUndefined(frictVal)) {
                            double f = 0;
                            if (JS_ToFloat64(m_ctx, &f, frictVal) == 0) {
                                rb.friction = static_cast<float>(f);
                                if (physics && rb.bodyId != 0xFFFFFFFF) {
                                    physics->getBodyInterface().SetFriction(JPH::BodyID(rb.bodyId), rb.friction);
                                }
                            }
                        }
                        JS_FreeValue(m_ctx, frictVal);
                    }
                    JS_FreeValue(m_ctx, rbObj);
                }
            }
            JS_FreeValue(m_ctx, idVal);
            JS_FreeValue(m_ctx, entObj);
        }

        JS_FreeValue(m_ctx, entitiesArr);
        JS_FreeValue(m_ctx, global);
    }
}
