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
};

class EngineShaderFamilies
{
public:
    static inline ShaderFamily staticMeshShaderFamily;
    static inline ShaderFamily wireframeMeshShaderFamily;
    static inline ShaderFamily skeletonMeshShaderFamily;
    static inline ShaderFamily texturedStatisMeshShaderFamily;

    static inline core::DescriptorSetLayout::SharedPtr wireframeMeshCameraLayout{nullptr};

    static inline core::DescriptorSetLayout::SharedPtr objectDescriptorSetLayout{nullptr};

    static inline core::DescriptorSetLayout::SharedPtr cameraDescriptorSetLayout{nullptr};
    static inline core::DescriptorSetLayout::SharedPtr materialDescriptorSetLayout{nullptr};

    static void initEngineShaderFamilies();
    static void cleanEngineShaderFamilies();
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SHADER_FAMILY_HPP