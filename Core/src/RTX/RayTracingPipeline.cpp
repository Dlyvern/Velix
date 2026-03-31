#include "Core/RTX/RayTracingPipeline.hpp"

#include "Core/VulkanContext.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(rtx)

RayTracingPipeline::SharedPtr RayTracingPipeline::create(
    const std::vector<VkPipelineShaderStageCreateInfo> &shaderStages,
    const std::vector<VkRayTracingShaderGroupCreateInfoKHR> &shaderGroups,
    VkPipelineLayout layout,
    uint32_t maxRayRecursionDepth)
{
    auto pipeline = std::shared_ptr<RayTracingPipeline>(new RayTracingPipeline());
    if (!pipeline->createInternal(shaderStages, shaderGroups, layout, maxRayRecursionDepth))
        return nullptr;

    return pipeline;
}

bool RayTracingPipeline::createInternal(
    const std::vector<VkPipelineShaderStageCreateInfo> &shaderStages,
    const std::vector<VkRayTracingShaderGroupCreateInfoKHR> &shaderGroups,
    VkPipelineLayout layout,
    uint32_t maxRayRecursionDepth)
{
    auto context = core::VulkanContext::getContext();
    if (!context || !context->hasRayTracingPipelineSupport() || layout == VK_NULL_HANDLE ||
        shaderStages.empty() || shaderGroups.empty())
        return false;

    m_device = context->getDevice();

    VkRayTracingPipelineCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    createInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    createInfo.pStages = shaderStages.data();
    createInfo.groupCount = static_cast<uint32_t>(shaderGroups.size());
    createInfo.pGroups = shaderGroups.data();
    createInfo.maxPipelineRayRecursionDepth = maxRayRecursionDepth;
    createInfo.layout = layout;

    if (vkCreateRayTracingPipelinesKHR(m_device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &createInfo, nullptr, &m_handle) != VK_SUCCESS)
        return false;

    m_groupCount = createInfo.groupCount;
    return true;
}

void RayTracingPipeline::destroy()
{
    if (m_handle != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE)
        vkDestroyPipeline(m_device, m_handle, nullptr);

    m_handle = VK_NULL_HANDLE;
    m_groupCount = 0u;
    m_device = VK_NULL_HANDLE;
}

RayTracingPipeline::~RayTracingPipeline()
{
    destroy();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
