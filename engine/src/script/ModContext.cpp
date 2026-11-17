#include "script/ModContext.hpp"
#include "core/Logger.hpp"
#include <quickjs.h>

namespace engine::script {

    ModContext::ModContext(const std::string& modName,
                           size_t memoryLimitBytes,
                           size_t stackLimitBytes)
        : m_modName(modName) {
        m_rt = JS_NewRuntime();
        m_ctx = JS_NewContext(m_rt);

        if (m_rt) {
            JS_SetMemoryLimit(m_rt, memoryLimitBytes);
            JS_SetMaxStackSize(m_rt, stackLimitBytes);

            JS_SetInterruptHandler(m_rt, [](JSRuntime* rt, void* opaque) -> int {
                (void)rt;
                auto* self = static_cast<ModContext*>(opaque);
                if (self->m_interrupted) return 1;
                if (self->m_hasDeadline && std::chrono::steady_clock::now() > self->m_deadline) {
                    return 1;
                }
                return 0;
            }, this);
        }

        _install_allowlist_api();
    }

    ModContext::~ModContext() {
        if (m_ctx) JS_FreeContext(m_ctx);
        if (m_rt) JS_FreeRuntime(m_rt);
    }

    void ModContext::_install_allowlist_api() {
        if (!m_ctx) return;

        JSValue global = JS_GetGlobalObject(m_ctx);
        JSValue modObj = JS_NewObject(m_ctx);

        // mod.log
        JSValue logFn = JS_NewCFunction(m_ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
            (void)this_val;
            if (argc >= 1) {
                const char* str = JS_ToCString(ctx, argv[0]);
                if (str) {
                    core::Logger::info(std::string("[Mod] ") + str);
                    JS_FreeCString(ctx, str);
                }
            }
            return JS_UNDEFINED;
        }, "log", 1);
        JS_SetPropertyStr(m_ctx, modObj, "log", logFn);

        // mod.getName
        JSValue nameVal = JS_NewString(m_ctx, m_modName.c_str());
        JS_SetPropertyStr(m_ctx, modObj, "name", nameVal);

        JS_SetPropertyStr(m_ctx, global, "mod", modObj);
        JS_FreeValue(m_ctx, global);
    }

    bool ModContext::load_script(const std::string& code, const std::string& filename) {
        (void)filename;
        return execute_with_timeout(code, 500);
    }

    bool ModContext::execute_with_timeout(const std::string& code, uint32_t timeoutMs) {
        if (!m_ctx || m_crashed) return false;

        m_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        m_hasDeadline = true;
        m_interrupted = false;

        JSValue val = JS_Eval(m_ctx, code.c_str(), code.length(), "mod.js", JS_EVAL_TYPE_GLOBAL);
        m_hasDeadline = false;

        if (JS_IsException(val)) {
            JSValue exceptionVal = JS_GetException(m_ctx);
            const char* errStr = JS_ToCString(m_ctx, exceptionVal);
            m_lastError = errStr ? errStr : "Unknown mod exception";
            if (errStr) JS_FreeCString(m_ctx, errStr);
            JS_FreeValue(m_ctx, exceptionVal);

            m_crashed = true;
            if (m_onError) {
                m_onError(m_modName, m_lastError);
            }
            core::Logger::error("[Mod Error in " + m_modName + "] " + m_lastError);
            JS_FreeValue(m_ctx, val);
            return false;
        }

        JS_FreeValue(m_ctx, val);
        return true;
    }

} // namespace engine::script
