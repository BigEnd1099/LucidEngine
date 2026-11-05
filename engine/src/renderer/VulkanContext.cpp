#define VMA_IMPLEMENTATION
#include "renderer/VulkanContext.hpp"
#include "core/Logger.hpp"
#include <stdexcept>
#include <algorithm>
#include <limits>

namespace engine::renderer {

    VulkanContext::VulkanContext(bool headless) : m_headless(headless) {}

    VulkanContext::~VulkanContext() {
        cleanup();
    }

    void VulkanContext::init() {
#ifdef USE_GLFW
        if (!m_headless) {
            if (!glfwInit()) {
                throw std::runtime_error("glfwInit failed!");
            }
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
            m_window = glfwCreateWindow(1920, 1080, "LUCID Engine | Lucid Studio 2026", nullptr, nullptr);
            if (!m_window) {
                throw std::runtime_error("glfwCreateWindow failed!");
            }
        }
#endif
        createInstance();
#ifdef USE_GLFW
        if (!m_headless) {
            if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS) {
                throw std::runtime_error("failed to create window surface!");
            }
        }
#endif
        pickPhysicalDevice();
        createLogicalDevice();
        createAllocator();
        createCommandPool();
#ifdef USE_GLFW
        if (!m_headless) {
            createSwapchain();
            createImGuiPool();
        }
#endif
        core::Logger::info("VulkanContext initialized successfully.");
    }

    void VulkanContext::cleanup() {
        if (m_device) {
            vkDeviceWaitIdle(m_device);
#ifdef USE_GLFW
            if (!m_headless) {
                vkDestroyDescriptorPool(m_device, m_imguiPool, nullptr);
                for (auto imageView : m_swapchainImageViews) {
                    vkDestroyImageView(m_device, imageView, nullptr);
                }
                vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
            }
#endif
            vkDestroyCommandPool(m_device, m_commandPool, nullptr);
            vmaDestroyAllocator(m_allocator);
            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }
        if (m_instance) {
#ifdef USE_GLFW
            if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
#endif
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }
#ifdef USE_GLFW
        if (m_window) {
            glfwDestroyWindow(m_window);
            glfwTerminate();
            m_window = nullptr;
        }
#endif
    }

    void VulkanContext::createInstance() {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Lucid Engine";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Lucid Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        std::vector<const char*> extensions;
#ifdef USE_GLFW
        if (!m_headless) {
            uint32_t glfwExtensionCount = 0;
            const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
            for (uint32_t i = 0; i < glfwExtensionCount; i++) {
                extensions.push_back(glfwExtensions[i]);
            }
        }
#endif
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        createInfo.enabledLayerCount = 0;

        if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
            throw std::runtime_error("failed to create Vulkan instance!");
        }
    }

    void VulkanContext::pickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
        if (deviceCount == 0) throw std::runtime_error("failed to find GPUs with Vulkan support!");
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

        auto isDeviceSuitable = [this](VkPhysicalDevice device) {
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

            bool hasGraphics = false;
            bool hasPresent = false;

            for (uint32_t i = 0; i < queueFamilyCount; i++) {
                if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    hasGraphics = true;
                }
#ifdef USE_GLFW
                if (!m_headless && m_surface != VK_NULL_HANDLE) {
                    VkBool32 presentSupport = false;
                    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
                    if (presentSupport) hasPresent = true;
                } else {
                    hasPresent = true;
                }
#else
                hasPresent = true;
#endif
                if (hasGraphics && hasPresent) return true;
            }
            return false;
        };

        m_physicalDevice = VK_NULL_HANDLE;
        for (const auto& device : devices) {
            if (!isDeviceSuitable(device)) continue;

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                m_physicalDevice = device;
                core::Logger::info(std::string("Selected Discrete GPU: ") + props.deviceName);
                break;
            }
        }

        if (m_physicalDevice == VK_NULL_HANDLE) {
            for (const auto& device : devices) {
                if (isDeviceSuitable(device)) {
                    m_physicalDevice = device;
                    VkPhysicalDeviceProperties props;
                    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
                    core::Logger::info(std::string("Fallback to GPU: ") + props.deviceName);
                    break;
                }
            }
        }

        if (m_physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
    }

    void VulkanContext::createLogicalDevice() {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

        m_graphicsQueueFamily = 0;
        bool found = false;
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            bool hasGraphics = (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            bool hasPresent = false;
#ifdef USE_GLFW
            if (!m_headless && m_surface != VK_NULL_HANDLE) {
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);
                if (presentSupport) hasPresent = true;
            } else {
                hasPresent = true;
            }
#else
            hasPresent = true;
#endif
            if (hasGraphics && hasPresent) {
                m_graphicsQueueFamily = i;
                found = true;
                break;
            }
        }
        if (!found) throw std::runtime_error("failed to find a graphics AND present queue family!");

        VkPhysicalDeviceProperties devProps{};
        vkGetPhysicalDeviceProperties(m_physicalDevice, &devProps);

        VkPhysicalDeviceVulkan12Features supportedFeatures12{};
        supportedFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

        VkPhysicalDeviceVulkan13Features supportedFeatures13{};
        supportedFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        supportedFeatures13.pNext = &supportedFeatures12;

        VkPhysicalDeviceFeatures2 supportedFeatures2{};
        supportedFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supportedFeatures2.pNext = &supportedFeatures13;
        vkGetPhysicalDeviceFeatures2(m_physicalDevice, &supportedFeatures2);

        if (!supportedFeatures13.dynamicRendering || !supportedFeatures13.synchronization2) {
            core::Logger::error("GPU does not support required Vulkan 1.3 features: dynamicRendering and synchronization2!");
            throw std::runtime_error("failed to find Vulkan 1.3 dynamicRendering/synchronization2 support!");
        }

        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = m_graphicsQueueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.descriptorIndexing = supportedFeatures12.descriptorIndexing;
        features12.runtimeDescriptorArray = supportedFeatures12.runtimeDescriptorArray;
        features12.descriptorBindingPartiallyBound = supportedFeatures12.descriptorBindingPartiallyBound;
        features12.descriptorBindingVariableDescriptorCount = supportedFeatures12.descriptorBindingVariableDescriptorCount;
        features12.bufferDeviceAddress = supportedFeatures12.bufferDeviceAddress;

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;
        features13.pNext = &features12;

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features13;
        if (supportedFeatures2.features.samplerAnisotropy) {
            features2.features.samplerAnisotropy = VK_TRUE;
            m_maxSamplerAnisotropy = std::min(16.0f, devProps.limits.maxSamplerAnisotropy);
        } else {
            m_maxSamplerAnisotropy = 1.0f;
        }

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &features2;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.queueCreateInfoCount = 1;

        std::vector<const char*> deviceExtensions;
        if (!m_headless) {
            deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }

        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
            throw std::runtime_error("failed to create logical device!");
        }
        vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    }

    void VulkanContext::createAllocator() {
        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
        allocatorInfo.physicalDevice = m_physicalDevice;
        allocatorInfo.device = m_device;
        allocatorInfo.instance = m_instance;
        allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

        if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
            throw std::runtime_error("failed to create VMA allocator!");
        }
    }

    void VulkanContext::createCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_graphicsQueueFamily;

        if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create command pool!");
        }
    }

#ifdef USE_GLFW
    void VulkanContext::createWindowAndSurface() {}

    void VulkanContext::createSwapchain() {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        if (formatCount != 0) {
            vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());
        }

        m_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
        VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        if (!formats.empty()) {
            m_swapchainImageFormat = formats[0].format;
            colorSpace = formats[0].colorSpace;
            for (const auto& availableFormat : formats) {
                if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    m_swapchainImageFormat = availableFormat.format;
                    colorSpace = availableFormat.colorSpace;
                    break;
                }
            }
        }

        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            m_swapchainExtent = capabilities.currentExtent;
        } else {
            int width = 0, height = 0;
            if (m_window) {
                glfwGetFramebufferSize(m_window, &width, &height);
            }
            m_swapchainExtent.width = std::clamp(static_cast<uint32_t>(width > 0 ? width : 1920),
                                                 capabilities.minImageExtent.width,
                                                 capabilities.maxImageExtent.width);
            m_swapchainExtent.height = std::clamp(static_cast<uint32_t>(height > 0 ? height : 1080),
                                                  capabilities.minImageExtent.height,
                                                  capabilities.maxImageExtent.height);
        }

        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
            imageCount = capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = m_swapchainImageFormat;
        createInfo.imageColorSpace = colorSpace;
        createInfo.imageExtent = m_swapchainExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        createInfo.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
            throw std::runtime_error("failed to create swap chain!");
        }

        uint32_t finalImageCount;
        vkGetSwapchainImagesKHR(m_device, m_swapchain, &finalImageCount, nullptr);
        m_swapchainImages.resize(finalImageCount);
        vkGetSwapchainImagesKHR(m_device, m_swapchain, &finalImageCount, m_swapchainImages.data());

        m_swapchainImageViews.resize(m_swapchainImages.size());
        for (size_t i = 0; i < m_swapchainImages.size(); i++) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = m_swapchainImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_swapchainImageFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create image views!");
            }
        }
    }

    void VulkanContext::createImGuiPool() {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };
        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * 11;
        pool_info.poolSizeCount = 11;
        pool_info.pPoolSizes = pool_sizes;

        if (vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_imguiPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create imgui descriptor pool!");
        }
    }
#endif

    void VulkanContext::recreateSwapchain() {
#ifdef USE_GLFW
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        while (width == 0 || height == 0) {
            if (glfwWindowShouldClose(m_window)) return;
            glfwGetFramebufferSize(m_window, &width, &height);
            glfwWaitEventsTimeout(0.05);
        }

        vkDeviceWaitIdle(m_device);

        for (auto imageView : m_swapchainImageViews) {
            vkDestroyImageView(m_device, imageView, nullptr);
        }
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);

        createSwapchain();
#endif
    }
}
