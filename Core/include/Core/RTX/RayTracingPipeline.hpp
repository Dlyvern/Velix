#ifndef ELIX_CORE_RTX_RAY_TRACING_PIPELINE_HPP
#define ELIX_CORE_RTX_RAY_TRACING_PIPELINE_HPP

#include "Core/Macros.hpp"

#include <volk.h>

#include <cstdint>
#include <memory>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(rtx)

class RayTracingPipeline
{
public:
    using SharedPtr = std::shared_ptr<RayTracingPipeline>;

    static SharedPtr create(const std::vector<VkPipelineShaderStageCreateInfo> &shaderStages,
                            const std::vector<VkRayTracingShaderGroupCreateInfoKHR> &shaderGroups,
                            VkPipelineLayout layout,
                            uint32_t maxRayRecursionDepth);

    ~RayTracingPipeline();

    VkPipeline vk() const
    {
        return m_handle;
    }

    bool isValid() const
    {
        return m_handle != VK_NULL_HANDLE;
    }

    uint32_t groupCount() const
    {
        return m_groupCount;
    }

    void destroy();

private:
    RayTracingPipeline() = default;

    bool createInternal(const std::vector<VkPipelineShaderStageCreateInfo> &shaderStages,
                        const std::vector<VkRayTracingShaderGroupCreateInfoKHR> &shaderGroups,
                        VkPipelineLayout layout,
                        uint32_t maxRayRecursionDepth);

    VkDevice m_device{VK_NULL_HANDLE};
    VkPipeline m_handle{VK_NULL_HANDLE};
    uint32_t m_groupCount{0u};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_CORE_RTX_RAY_TRACING_PIPELINE_HPP
