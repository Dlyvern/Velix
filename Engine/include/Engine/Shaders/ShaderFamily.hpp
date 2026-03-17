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

    // Bindless material set: binding 0 = sampler2D allTextures[], binding 1 = MaterialParams SSBO.
    // Used by the GBuffer pass to eliminate all per-batch descriptor-set binds.
    static inline VkDescriptorSetLayout bindlessMaterialSetLayout{VK_NULL_HANDLE};
    static inline VkPipelineLayout      bindlessMeshPipelineLayout{VK_NULL_HANDLE};

    static constexpr uint32_t MAX_BINDLESS_TEXTURES  = 4096;
    static constexpr uint32_t MAX_BINDLESS_MATERIALS = 2048;

    static void initEngineShaderFamilies();
    static void cleanEngineShaderFamilies();
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SHADER_FAMILY_HPP