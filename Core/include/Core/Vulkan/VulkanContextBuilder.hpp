#ifndef ELIX_CORE_VULKAN_VULKAN_CONTEXT_BUILDER_HPP
#define ELIX_CORE_VULKAN_VULKAN_CONTEXT_BUILDER_HPP

#include "Core/Macros.hpp"
#include "Core/VulkanContext.hpp"

#include <volk.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class VulkanContext::Builder
{
public:
    explicit Builder(platform::Window &window);

    Builder &useEngineDefaults();
    Builder &applicationName(const char *name);
    Builder &applicationVersion(uint32_t major, uint32_t minor, uint32_t patch);
    Builder &engineName(const char *name);
    Builder &apiVersion(uint32_t version);
    Builder &enableValidationLayers(bool enabled);
    Builder &addInstanceExtension(const char *extensionName);
    Builder &addValidationLayer(const char *layerName);
    Builder &request(VulkanFeature feature, FeatureRequirement requirement);
    Builder &preferredDeviceType(VkPhysicalDeviceType type);
    Builder &maxRequestedSampleCount(VkSampleCountFlagBits sampleCount);

    std::shared_ptr<VulkanContext> build();

private:
    platform::Window *m_window{nullptr};
    std::string m_applicationName{"VelixCore"};
    std::string m_engineName{"VelixEngine"};
    uint32_t m_applicationVersion{VK_MAKE_VERSION(0, 0, 1)};
    uint32_t m_engineVersion{VK_MAKE_VERSION(0, 0, 1)};
    uint32_t m_apiVersion{VK_API_VERSION_1_3};
    std::optional<bool> m_validationLayersEnabled;
    std::vector<const char *> m_extraValidationLayers;
    std::vector<const char *> m_extraInstanceExtensions;
    std::unordered_map<VulkanFeature, FeatureRequirement> m_featureRequests;
    VkPhysicalDeviceType m_preferredDeviceType{VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU};
    VkSampleCountFlagBits m_maxRequestedSampleCount{VK_SAMPLE_COUNT_64_BIT};
};

ELIX_NESTED_NAMESPACE_END

#endif
