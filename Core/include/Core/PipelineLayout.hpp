#ifndef ELIX_PIPELINE_LAYOUT_HPP
#define ELIX_PIPELINE_LAYOUT_HPP

#include "Core/Macros.hpp"
#include "Core/DescriptorSetLayout.hpp"

#include <vector>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class PipelineLayout
{
    DECLARE_VK_HANDLE_METHODS(VkPipelineLayout)
    ELIX_DECLARE_VK_LIFECYCLE()
    DECLARE_VK_SMART_PTRS(PipelineLayout, VkPipelineLayout)
public:
    PipelineLayout(VkDevice device, const std::vector<DescriptorSetLayout::SharedPtr> &setLayouts, const std::vector<VkPushConstantRange> &pushConstants = {});

    void createVk(const std::vector<DescriptorSetLayout::SharedPtr> &setLayouts, const std::vector<VkPushConstantRange> &pushConstants = {});

    ~PipelineLayout();

private:
    VkDevice m_device{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PIPELINE_LAYOUT_HPP