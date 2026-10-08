#pragma once
#include "Common.hpp"
#include <set>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <vulkan/vulkan.h>
namespace hyprune::offload {
inline void vkcheck(VkResult r, const char *op) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(op) + " VkResult=" + std::to_string(r));
}
struct Vulkan {
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t family = 0;
    VkCommandPool pool{};
    VkPhysicalDeviceMemoryProperties memory{};
    bool foreign = false;
    Json caps;
    explicit Vulkan(const std::string &node) {
        struct stat drm{};
        check(stat(node.c_str(), &drm) == 0, "DRM identity");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "hyprune-renderer";
        app.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        vkcheck(vkCreateInstance(&ci, nullptr, &instance), "instance");
        uint32_t n = 0;
        vkEnumeratePhysicalDevices(instance, &n, nullptr);
        std::vector<VkPhysicalDevice> list(n);
        vkEnumeratePhysicalDevices(instance, &n, list.data());
        for (auto p : list) {
            VkPhysicalDeviceProperties prop;
            vkGetPhysicalDeviceProperties(p, &prop);
            VkPhysicalDeviceDrmPropertiesEXT identity{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT};
            VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            properties.pNext = &identity;
            vkGetPhysicalDeviceProperties2(p, &properties);
            if (prop.vendorID == 0x10de && identity.hasRender && identity.renderMajor == major(drm.st_rdev) &&
                identity.renderMinor == minor(drm.st_rdev)) {
                physical = p;
                caps["device"] = prop.deviceName;
                caps["driverVersion"] = prop.driverVersion;
            }
        }
        check(physical, "NVIDIA Vulkan device");
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> props(n);
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &n, props.data());
        std::set<std::string> found;
        for (auto &p : props)
            found.insert(p.extensionName);
        std::vector<const char *> extensions = {
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
            VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME};
        for (auto e : extensions)
            check(found.contains(e), e);
        foreign = found.contains(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
        if (foreign)
            extensions.push_back(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
        caps["extensions"] = found;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, nullptr);
        std::vector<VkQueueFamilyProperties> families(n);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, families.data());
        for (uint32_t i = 0; i < n; i++)
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                family = i;
                break;
            }
        float priority = 1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;
        di.enabledExtensionCount = extensions.size();
        di.ppEnabledExtensionNames = extensions.data();
        vkcheck(vkCreateDevice(physical, &di, nullptr, &device), "device");
        vkGetDeviceQueue(device, family, 0, &queue);
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.queueFamilyIndex = family;
        vkcheck(vkCreateCommandPool(device, &pi, nullptr, &pool), "pool");
    }
    ~Vulkan() {
        vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
    }
    uint32_t type(uint32_t bits) {
        for (uint32_t i = 0; i < memory.memoryTypeCount; i++)
            if (bits & (1u << i))
                return i;
        throw std::runtime_error("no compatible memory type");
    }
};
} // namespace hyprune::offload
