#ifndef ELIX_CORE_VULKAN_FEATURE_REGISTRY_HPP
#define ELIX_CORE_VULKAN_FEATURE_REGISTRY_HPP

#include "Core/Macros.hpp"
#include "Core/VulkanContext.hpp"

#include <volk.h>

#include <string>
#include <unordered_map>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class FeatureRegistry
{
public:
    explicit FeatureRegistry(std::unordered_map<VulkanFeature, FeatureRequirement> requests);

    void probe(VkPhysicalDevice device);

    void disableAllOptional();

    void buildEnableChain(std::vector<const char *> &outDeviceExtensions,
                          VkPhysicalDeviceFeatures2 *&outFeatures2);

    bool isSupported(VulkanFeature feature) const;
    bool isEnabled(VulkanFeature feature) const;
    FeatureRequirement requirementOf(VulkanFeature feature) const;

    const std::unordered_map<VulkanFeature, bool> &enabledMap() const { return m_enabled; }

    const VkPhysicalDeviceVulkan12Features &supportedV12() const { return m_supportedV12; }
    const VkPhysicalDeviceVulkan13Features &supportedV13() const { return m_supportedV13; }
    const VkPhysicalDeviceFeatures &supportedCoreFeatures() const { return m_supportedFeatures2.features; }

private:
    bool isRequested(VulkanFeature feature) const;
    bool isExtensionAvailable(const char *name) const;
    bool dependenciesSupported(VulkanFeature feature) const;

    void enableFeature(VulkanFeature feature);

    std::unordered_map<VulkanFeature, FeatureRequirement> m_requests;
    std::unordered_map<VulkanFeature, bool> m_supported;
    std::unordered_map<VulkanFeature, bool> m_enabled;
    std::vector<VkExtensionProperties> m_deviceExtensions;

    VkPhysicalDeviceFeatures2 m_supportedFeatures2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan12Features m_supportedV12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features m_supportedV13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR m_supportedAS{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR m_supportedRTP{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR m_supportedRQ{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

    VkPhysicalDeviceFeatures2 m_enableFeatures2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan12Features m_enableV12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features m_enableV13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR m_enableAS{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR m_enableRTP{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR m_enableRQ{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
};

ELIX_NESTED_NAMESPACE_END

#endif
