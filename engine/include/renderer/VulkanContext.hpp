#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>

#ifdef USE_GLFW
#include <GLFW/glfw3.h>
#endif

namespace engine::renderer {
    class VulkanContext {
    public:
        VulkanContext(bool headless, bool benchmarkMode = false);
        ~VulkanContext();

        void init();
        void cleanup();
        void recreateSwapchain();

        void setVsyncEnabled(bool enabled) {
            if (m_vsyncEnabled != enabled) {
                m_vsyncEnabled = enabled;
#ifdef USE_GLFW
                if (!m_headless) recreateSwapchain();
#endif
            }
        }
        bool isVsyncEnabled() const { return m_vsyncEnabled; }

        VkDevice getDevice() const { return m_device; }
        VkInstance getInstance() const { return m_instance; }
        VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
        VmaAllocator getAllocator() const { return m_allocator; }
        VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
        uint32_t getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
        VkCommandPool getCommandPool() const { return m_commandPool; }
        bool isHeadless() const { return m_headless; }
        float getMaxSamplerAnisotropy() const { return m_maxSamplerAnisotropy; }
        const VkPhysicalDeviceProperties& getPhysicalDeviceProperties() const { return m_deviceProperties; }
        uint32_t getTimestampValidBits() const { return m_timestampValidBits; }
        bool isRayTracingSupported() const { return m_rayTracingSupported; }
        bool isRayQuerySupported() const { return m_rayTracingSupported; }

#ifdef USE_GLFW
        GLFWwindow* getWindow() const { return m_window; }
        VkSwapchainKHR getSwapchain() const { return m_swapchain; }
        VkExtent2D getSwapchainExtent() const { return m_swapchainExtent; }
        const std::vector<VkImage>& getSwapchainImages() const { return m_swapchainImages; }
        const std::vector<VkImageView>& getSwapchainImageViews() const { return m_swapchainImageViews; }
        VkFormat getSwapchainImageFormat() const { return m_swapchainImageFormat; }
        VkDescriptorPool getImGuiPool() const { return m_imguiPool; }
#endif

    private:
        void createInstance();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createAllocator();
        void createCommandPool();
#ifdef USE_GLFW
        void createWindowAndSurface();
        void createSwapchain();
        void createImGuiPool();
#endif

        bool m_headless{true};
        bool m_benchmarkMode{false};
        bool m_rayTracingSupported{false};
        bool m_vsyncEnabled{true};
        float m_maxSamplerAnisotropy{1.0f};
        VkPhysicalDeviceProperties m_deviceProperties{};
        uint32_t m_timestampValidBits{0};
        VkInstance m_instance{VK_NULL_HANDLE};
        VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
        VkDevice m_device{VK_NULL_HANDLE};
        VkQueue m_graphicsQueue{VK_NULL_HANDLE};
        uint32_t m_graphicsQueueFamily{0};
        VmaAllocator m_allocator{VK_NULL_HANDLE};
        VkCommandPool m_commandPool{VK_NULL_HANDLE};

#ifdef USE_GLFW
        GLFWwindow* m_window{nullptr};
        VkSurfaceKHR m_surface{VK_NULL_HANDLE};
        VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
        std::vector<VkImage> m_swapchainImages;
        std::vector<VkImageView> m_swapchainImageViews;
        VkFormat m_swapchainImageFormat{VK_FORMAT_B8G8R8A8_UNORM};
        VkExtent2D m_swapchainExtent{1920, 1080};
        VkDescriptorPool m_imguiPool{VK_NULL_HANDLE};
#endif
    };
}
