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
        VulkanContext(bool headless);
        ~VulkanContext();

        void init();
        void cleanup();
        void recreateSwapchain();

        VkDevice getDevice() const { return m_device; }
        VkInstance getInstance() const { return m_instance; }
        VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
        VmaAllocator getAllocator() const { return m_allocator; }
        VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
        uint32_t getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
        VkCommandPool getCommandPool() const { return m_commandPool; }

        bool isHeadless() const { return m_headless; }

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
        VkFormat m_swapchainImageFormat;
        VkExtent2D m_swapchainExtent;
        VkDescriptorPool m_imguiPool{VK_NULL_HANDLE};
#endif
    };
}
