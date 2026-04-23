#ifndef ELIX_SHADER_FAMILY_HPP
#define ELIX_SHADER_FAMILY_HPP

#include "Core/Macros.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/DescriptorSetLayout.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct ShaderFamily
{
    std::vector<core::DescriptorSetLayout::SharedPtr> layouts;
    core::PipelineLayout::SharedPtr pipelineLayout{nullptr};
    VkPushConstantRange pushConstantRange{};
};

class EngineShaderFamilies
{
public:
    static inline ShaderFamily meshShaderFamily;

    static inline core::DescriptorSetLayout::SharedPtr objectDescriptorSetLayout{nullptr};
    static inline core::DescriptorSetLayout::SharedPtr cameraDescriptorSetLayout{nullptr};
    static inline core::DescriptorSetLayout::SharedPtr materialDescriptorSetLayout{nullptr};



    static inline VkDescriptorSetLayout bindlessMaterialSetLayout{VK_NULL_HANDLE};
    static inline VkPipelineLayout      bindlessMeshPipelineLayout{VK_NULL_HANDLE};

    static constexpr uint32_t MAX_BINDLESS_TEXTURES  = 4096;
    static constexpr uint32_t MAX_BINDLESS_MATERIALS = 2048;

    static const ShaderFamily &getMeshShaderFamily();
    static const core::DescriptorSetLayout &getObjectDescriptorSetLayoutRef();
    static const core::DescriptorSetLayout &getCameraDescriptorSetLayoutRef();
    static const core::DescriptorSetLayout &getMaterialDescriptorSetLayoutRef();
    static VkDescriptorSetLayout getBindlessMaterialSetLayout();
    static VkPipelineLayout getBindlessMeshPipelineLayout();

    static void initEngineShaderFamilies();
    static void cleanEngineShaderFamilies();
};

ELIX_NESTED_NAMESPACE_END

#endif
