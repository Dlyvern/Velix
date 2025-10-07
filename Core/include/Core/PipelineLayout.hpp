#ifndef ELIX_PIPELINE_LAYOUT_HPP
#define ELIX_PIPELINE_LAYOUT_HPP

#include "Core/Macros.hpp"
#include "Core/DescriptorSetLayout.hpp"

#include <vector>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class PipelineLayout
{
public:
    using SharedPtr = std::shared_ptr<PipelineLayout>;

    PipelineLayout(VkDevice device, const std::vector<DescriptorSetLayout::SharedPtr>& setLayouts, const std::vector<VkPushConstantRange>& pushConstants = {});
    ~PipelineLayout();

    static SharedPtr create(VkDevice device, const std::vector<DescriptorSetLayout::SharedPtr>& setLayouts, const std::vector<VkPushConstantRange>& pushConstants = {});

    VkPipelineLayout vk();
private:
    VkDevice m_device{VK_NULL_HANDLE};
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_PIPELINE_LAYOUT_HPP