#ifndef ELIX_SHADER_FAMILY_HPP
#define ELIX_SHADER_FAMILY_HPP

#include "Core/Macros.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/DescriptorSetLayout.hpp"

#include "Engine/PushConstant.hpp"

#include <glm/glm.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct ShaderFamily
{
    std::vector<core::DescriptorSetLayout::SharedPtr> layouts;
    core::PipelineLayout::SharedPtr pipelineLayout{nullptr};
};

namespace engineShaderFamilies
{
    extern ShaderFamily staticMeshShaderFamily;
    extern core::DescriptorSetLayout::SharedPtr staticMeshCameraLayout;
    extern core::DescriptorSetLayout::SharedPtr staticMeshLightLayout;
    extern core::DescriptorSetLayout::SharedPtr staticMeshMaterialLayout;

    void initEngineShaderFamilies();
    void cleanEngineShaderFamilies();
} // engineShaderFamilies

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SHADER_FAMILY_HPP