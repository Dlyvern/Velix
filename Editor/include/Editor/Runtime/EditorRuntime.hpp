#ifndef ELIX_EDITOR_RUNTIME_HPP
#define ELIX_EDITOR_RUNTIME_HPP

#include "Engine/Runtime/ApplicationConfig.hpp"
#include "Engine/Runtime/IRuntime.hpp"
#include "Engine/Render/RenderGraph/RenderGraph.hpp"
#include "Engine/Scene.hpp"
#include "Engine/Shaders/ShaderHotReloader.hpp"
#include "Engine/Camera.hpp"

#include "Engine/Render/GraphPasses/GBufferRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/LightingRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/SkyLightRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/TonemapRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/SSRRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/BloomRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/BloomCompositeRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/FXAARenderGraphPass.hpp"
#include "Editor/RenderGraphPasses/PreviewAssetsRenderGraphPass.hpp"
#include "Editor/RenderGraphPasses/SelectionOverlayRenderGraphPass.hpp"
#include "Editor/ImGuiRenderGraphPass.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class PreviewAssetsRenderGraphPass;
class Project;
class Editor;

class EditorRuntime : public engine::IRuntime
{
public:
    explicit EditorRuntime(const engine::ApplicationConfig &config);

    bool init() override;
    void tick(float deltaTime) override;
    void shutdown() override;

private:
    std::string m_projectPath;

    std::shared_ptr<engine::Camera> m_currentRenderCamera{nullptr};

    std::shared_ptr<engine::Scene> m_scene{nullptr};
    std::shared_ptr<Project> m_project{nullptr};
    std::shared_ptr<Editor> m_editor{nullptr};

    std::unique_ptr<engine::renderGraph::RenderGraph> m_renderGraph{nullptr};
    std::unique_ptr<engine::shaders::ShaderHotReloader> m_shaderHotReloader{nullptr};

    PreviewAssetsRenderGraphPass *m_previewAssetsRenderGraphPass{nullptr};
    engine::renderGraph::GBufferRenderGraphPass    *m_gBufferRenderGraphPass{nullptr};
    engine::renderGraph::ShadowRenderGraphPass     *m_shadowRenderGraphPass{nullptr};
    engine::renderGraph::LightingRenderGraphPass   *m_lightingRenderGraphPass{nullptr};
    engine::renderGraph::SSRRenderGraphPass        *m_ssrRenderGraphPass{nullptr};
    engine::renderGraph::SkyLightRenderGraphPass   *m_skyLightRenderGraphPass{nullptr};
    engine::renderGraph::BloomRenderGraphPass      *m_bloomRenderGraphPass{nullptr};
    engine::renderGraph::TonemapRenderGraphPass    *m_tonemapRenderGraphPass{nullptr};
    engine::renderGraph::BloomCompositeRenderGraphPass *m_bloomCompositeRenderGraphPass{nullptr};
    engine::renderGraph::FXAARenderGraphPass       *m_fxaaRenderGraphPass{nullptr};
    SelectionOverlayRenderGraphPass *m_selectionOverlayRenderGraphPass{nullptr};
    ImGuiRenderGraphPass *m_imGuiRenderGraphPass{nullptr};

    bool m_shouldUpdate{false};
    bool m_isPlaySessionActive{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_RUNTIME_HPP
