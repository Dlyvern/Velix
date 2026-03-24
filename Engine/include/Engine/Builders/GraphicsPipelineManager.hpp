#ifndef ELIX_GRAPHICS_PIPELINE_MANAGER_HPP
#define ELIX_GRAPHICS_PIPELINE_MANAGER_HPP

#include "Core/GraphicsPipeline.hpp"
#include "Core/Shader.hpp"

#include "Engine/Builders/GraphicsPipelineKey.hpp"

#include <unordered_map>
#include <shared_mutex>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class GraphicsPipelineManager
{
public:
    static core::GraphicsPipeline::SharedPtr getOrCreate(const GraphicsPipelineKey &key);

    static void init();
    static void reloadShaders();
    static void destroy();

private:
    static core::GraphicsPipeline::SharedPtr createPipeline(const GraphicsPipelineKey &key);
    static void loadShaderModules();
    static void destroyShaderModules();
    static void destroyPipelines();

    static inline std::unordered_map<GraphicsPipelineKey, core::GraphicsPipeline::SharedPtr, GraphicsPipelineKeyHash> m_pipelines;
    static inline std::shared_mutex m_pipelinesMutex;

    static inline core::Shader::SharedPtr shadowStaticShader{nullptr};
    static inline core::Shader::SharedPtr shadowSkinnedShader{nullptr};

    static inline core::Shader::SharedPtr previewMeshShader{nullptr};
    static inline core::Shader::SharedPtr skyboxHDRShader{nullptr};
    static inline core::Shader::SharedPtr skyboxShader{nullptr};

    static inline core::Shader::SharedPtr skyLightShader{nullptr};
    static inline core::Shader::SharedPtr toneMapShader{nullptr};
    static inline core::Shader::SharedPtr selectionOverlayShader{nullptr};
    static inline core::Shader::SharedPtr presentShader{nullptr};

    static inline core::Shader::SharedPtr gBufferStaticShader{nullptr};
    static inline core::Shader::SharedPtr gBufferSkinnedShader{nullptr};

    static inline core::Shader::SharedPtr lightingShader{nullptr};
    static inline core::Shader::SharedPtr lightingRayQueryShader{nullptr};

    static inline core::Shader::SharedPtr fxaaShader{nullptr};
    static inline core::Shader::SharedPtr bloomExtractShader{nullptr};
    static inline core::Shader::SharedPtr bloomCompositeShader{nullptr};
    static inline core::Shader::SharedPtr ssaoShader{nullptr};
    static inline core::Shader::SharedPtr smaaShader{nullptr};
    static inline core::Shader::SharedPtr contactShadowShader{nullptr};
    static inline core::Shader::SharedPtr cinematicEffectsShader{nullptr};
    static inline core::Shader::SharedPtr editorBillboardShader{nullptr};
    static inline core::Shader::SharedPtr billboardShader{nullptr};
    static inline core::Shader::SharedPtr uiTextShader{nullptr};
    static inline core::Shader::SharedPtr uiQuadShader{nullptr};
    static inline core::Shader::SharedPtr particleShader{nullptr};
    static inline core::Shader::SharedPtr glassShader{nullptr};
    static inline core::Shader::SharedPtr rtReflectionsShader{nullptr};
    static inline core::Shader::SharedPtr rtaoShader{nullptr};
    static inline core::Shader::SharedPtr depthPrepassStaticShader{nullptr};
    static inline core::Shader::SharedPtr depthPrepassSkinnedShader{nullptr};
    static inline core::Shader::SharedPtr taaShader{nullptr};
    static inline core::Shader::SharedPtr animPreviewShader{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_GRAPHICS_PIPELINE_MANAGER_HPP
