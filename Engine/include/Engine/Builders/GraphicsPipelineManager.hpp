#ifndef ELIX_GRAPHICS_PIPELINE_MANAGER_HPP
#define ELIX_GRAPHICS_PIPELINE_MANAGER_HPP

#include "Core/GraphicsPipeline.hpp"
#include "Core/Shader.hpp"

#include "Engine/Builders/GraphicsPipelineKey.hpp"

#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class GraphicsPipelineManager
{
public:
    static core::GraphicsPipeline::SharedPtr getOrCreate(GraphicsPipelineKey key);

    static void init();
    static void destroy();

private:
    static core::GraphicsPipeline::SharedPtr createPipeline(const GraphicsPipelineKey &key);

    static inline std::unordered_map<GraphicsPipelineKey, core::GraphicsPipeline::SharedPtr, GraphicsPipelineKeyHash> m_pipelines;

    static inline core::Shader::SharedPtr staticShader{nullptr};
    static inline core::Shader::SharedPtr skeletonShader{nullptr};
    static inline core::Shader::SharedPtr wireframeShader{nullptr};
    static inline core::Shader::SharedPtr stencilShader{nullptr};
    static inline core::Shader::SharedPtr shadowStaticShader{nullptr};

    static inline core::Shader::SharedPtr previewMeshShader{nullptr};
    static inline core::Shader::SharedPtr skyboxHDRShader{nullptr};
    static inline core::Shader::SharedPtr skyboxShader{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_GRAPHICS_PIPELINE_MANAGER_HPP