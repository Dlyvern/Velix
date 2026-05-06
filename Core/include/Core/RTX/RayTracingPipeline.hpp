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
    DECLARE_VK_HANDLE_METHODS(VkPipeline)
    ELIX_DECLARE_VK_LIFECYCLE()
public:
    using SharedPtr = std::shared_ptr<RayTracingPipeline>;

    static SharedPtr create(const std::vector<VkPipelineShaderStageCreateInfo> &shaderStages,
                            const std::vector<VkRayTracingShaderGroupCreateInfoKHR> &shaderGroups,
                            VkPipelineLayout layout,
                            uint32_t maxRayRecursionDepth);

    RayTracingPipeline(const RayTracingPipeline &) = delete;
    RayTracingPipeline &operator=(const RayTracingPipeline &) = delete;

    ~RayTracingPipeline();

    uint32_t groupCount() const
    {
        return m_groupCount;
    }

private:
    RayTracingPipeline() = default;

    bool createInternal(const std::vector<VkPipelineShaderStageCreateInfo> &shaderStages,
                        const std::vector<VkRayTracingShaderGroupCreateInfoKHR> &shaderGroups,
                        VkPipelineLayout layout,
                        uint32_t maxRayRecursionDepth);

    VkDevice m_device{VK_NULL_HANDLE};
    uint32_t m_groupCount{0u};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif
