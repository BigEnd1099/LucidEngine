#pragma once

#include "network/SharedStateRing.hpp"
#include "network/MpscCommandQueue.hpp"
#include "network/DualTransportContracts.hpp"
#include "network/EmbeddedAssets.hpp"
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace engine::ui {

/**
 * @brief Window layout descriptor for Dual-Transport UI Shell.
 * Embeds Vulkan HWND inside the rectangular cutout of the web layout,
 * allowing native mouse/keyboard events in the 3D scene to reach Vulkan directly.
 */
struct ViewportRect {
    int32_t x{0};
    int32_t y{0};
    int32_t width{1920};
    int32_t height{1080};
};

class NativeUiHost {
public:
    NativeUiHost(network::SharedStateRingBuffer& ringBuffer, network::MpscCommandQueue& commandQueue)
        : m_ringBuffer(ringBuffer), m_commandQueue(commandQueue) {}

    ~NativeUiHost() {
        shutdown();
    }

    /**
     * @brief Initialize native window host and bind to Vulkan HWND.
     * @param parentHwnd Top-level application window handle
     * @param vulkanHwnd Child window handle rendering Vulkan
     */
    bool initialize(void* parentHwnd, void* vulkanHwnd) {
#ifdef _WIN32
        m_parentHwnd = static_cast<HWND>(parentHwnd);
        m_vulkanHwnd = static_cast<HWND>(vulkanHwnd);
        m_isInitialized = true;
        return true;
#else
        (void)parentHwnd;
        (void)vulkanHwnd;
        m_isInitialized = true;
        return true;
#endif
    }

    /**
     * @brief Update the rectangular cutout where Vulkan HWND should be positioned
     * to avoid being blocked by web UI overlay elements.
     */
    void updateViewportCutout(const ViewportRect& rect) {
        m_viewportRect = rect;
#ifdef _WIN32
        if (m_vulkanHwnd && m_isInitialized) {
            SetWindowPos(m_vulkanHwnd, HWND_TOP, rect.x, rect.y, rect.width, rect.height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
#endif
    }

    /**
     * @brief Dispatch RPC command from local UI host into lock-free MPSC queue.
     */
    bool enqueueCommand(uint32_t commandId, uint64_t requestId, const uint8_t* payload, size_t payloadLen) {
        return m_commandQueue.enqueue(commandId, requestId, payload, payloadLen);
    }

    /**
     * @brief Lookup embedded web asset from compiled memory buffer (https://lucid.internal).
     */
    static const network::EmbeddedAsset* getEmbeddedAsset(const std::string& path) {
        return network::findEmbeddedAsset(path);
    }

    const ViewportRect& getViewportRect() const { return m_viewportRect; }
    bool isInitialized() const { return m_isInitialized; }

    void shutdown() {
        m_isInitialized = false;
    }

private:
    network::SharedStateRingBuffer& m_ringBuffer;
    network::MpscCommandQueue& m_commandQueue;
    ViewportRect m_viewportRect{};
    bool m_isInitialized{false};

#ifdef _WIN32
    HWND m_parentHwnd{nullptr};
    HWND m_vulkanHwnd{nullptr};
#endif
};

} // namespace engine::ui
