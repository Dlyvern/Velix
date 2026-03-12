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
#include "Engine/Render/GraphPasses/BloomRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/BloomCompositeRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/FXAARenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/SSAORenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/SMAAPassRenderGraphPass.hpp"
#include "Editor/RenderGraphPasses/PreviewAssetsRenderGraphPass.hpp"
#include "Editor/RenderGraphPasses/SelectionOverlayRenderGraphPass.hpp"
#include "Editor/RenderGraphPasses/EditorBillboardRenderGraphPass.hpp"
#include "Editor/ImGuiRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/UIRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/ParticleRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/ContactShadowRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/CinematicEffectsRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/RTReflectionsRenderGraphPass.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <mutex>

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

    void openSceneFromFile(const std::filesystem::path &path);

private:
    void applyEditorViewportExtent(uint32_t width, uint32_t height);
    void applyGameViewportExtent(uint32_t width, uint32_t height);

    void addLoadingRenderToImGui();
    void setLoadingStatus(std::string status);
    std::string getLoadingStatus() const;
    void setLoadingWindowDecorationsVisible(bool visible);

    bool m_stillLoadingTheScene{true};
    std::future<engine::Scene::SharedPtr> m_loadingFuture;
    std::chrono::steady_clock::time_point m_loadingStartedAt{};
    std::string m_loadingSceneName;
    mutable std::mutex m_loadingStatusMutex;
    std::string m_loadingStatus{"Preparing loading..."};
    bool m_loadingDecorationsHidden{false};
    bool m_loadingWindowSizeCaptured{false};
    int m_loadingPreviousWindowWidth{0};
    int m_loadingPreviousWindowHeight{0};

    void initGameViewportRenderGraph();
    void initEditorRenderGraph();
    void switchActiveScene(const std::shared_ptr<engine::Scene> &scene);
    void onEditorModeChanged(Editor::EditorMode mode);

    std::string m_projectPath;

    std::shared_ptr<engine::Camera> m_editorRenderCamera{nullptr};
    std::shared_ptr<engine::Camera> m_gameRenderCamera{nullptr};

    std::shared_ptr<engine::Scene> m_editorScene{nullptr};
    std::shared_ptr<engine::Scene> m_activeScene{nullptr};
    std::shared_ptr<engine::Scene> m_playScene{nullptr};
    std::shared_ptr<Project> m_project{nullptr};
    std::shared_ptr<Editor> m_editor{nullptr};

    std::unique_ptr<engine::renderGraph::RenderGraph> m_renderGraph{nullptr};
    std::unique_ptr<engine::renderGraph::RenderGraph> m_gameViewportRenderGraph{nullptr};
    std::unique_ptr<engine::shaders::ShaderHotReloader> m_shaderHotReloader{nullptr};

    PreviewAssetsRenderGraphPass *m_previewAssetsRenderGraphPass{nullptr};
    engine::renderGraph::GBufferRenderGraphPass *m_gBufferRenderGraphPass{nullptr};
    engine::renderGraph::ShadowRenderGraphPass *m_shadowRenderGraphPass{nullptr};
    engine::renderGraph::LightingRenderGraphPass *m_lightingRenderGraphPass{nullptr};
    engine::renderGraph::SkyLightRenderGraphPass *m_skyLightRenderGraphPass{nullptr};
    engine::renderGraph::BloomRenderGraphPass *m_bloomRenderGraphPass{nullptr};
    engine::renderGraph::TonemapRenderGraphPass *m_tonemapRenderGraphPass{nullptr};
    engine::renderGraph::BloomCompositeRenderGraphPass *m_bloomCompositeRenderGraphPass{nullptr};
    engine::renderGraph::FXAARenderGraphPass *m_fxaaRenderGraphPass{nullptr};
    engine::renderGraph::SSAORenderGraphPass *m_ssaoRenderGraphPass{nullptr};
    engine::renderGraph::SMAAPassRenderGraphPass *m_smaaRenderGraphPass{nullptr};
    engine::renderGraph::ContactShadowRenderGraphPass *m_contactShadowRenderGraphPass{nullptr};
    engine::renderGraph::RTReflectionsRenderGraphPass *m_rtReflectionsRenderGraphPass{nullptr};
    engine::renderGraph::CinematicEffectsRenderGraphPass *m_cinematicEffectsRenderGraphPass{nullptr};
    SelectionOverlayRenderGraphPass *m_selectionOverlayRenderGraphPass{nullptr};
    engine::renderGraph::UIRenderGraphPass *m_uiRenderGraphPass{nullptr};
    EditorBillboardRenderGraphPass *m_editorBillboardRenderGraphPass{nullptr};
    ImGuiRenderGraphPass *m_imGuiRenderGraphPass{nullptr};
    engine::renderGraph::ParticleRenderGraphPass *m_particleRenderGraphPass{nullptr};

    engine::renderGraph::GBufferRenderGraphPass *m_gameGBufferRenderGraphPass{nullptr};
    engine::renderGraph::ShadowRenderGraphPass *m_gameShadowRenderGraphPass{nullptr};
    engine::renderGraph::SSAORenderGraphPass *m_gameSSAORenderGraphPass{nullptr};
    engine::renderGraph::LightingRenderGraphPass *m_gameLightingRenderGraphPass{nullptr};
    engine::renderGraph::SkyLightRenderGraphPass *m_gameSkyLightRenderGraphPass{nullptr};
    engine::renderGraph::ParticleRenderGraphPass *m_gameParticleRenderGraphPass{nullptr};
    engine::renderGraph::BloomRenderGraphPass *m_gameBloomRenderGraphPass{nullptr};
    engine::renderGraph::TonemapRenderGraphPass *m_gameTonemapRenderGraphPass{nullptr};
    engine::renderGraph::BloomCompositeRenderGraphPass *m_gameBloomCompositeRenderGraphPass{nullptr};
    engine::renderGraph::FXAARenderGraphPass *m_gameFXAARenderGraphPass{nullptr};
    engine::renderGraph::SMAAPassRenderGraphPass *m_gameSMAARenderGraphPass{nullptr};
    engine::renderGraph::ContactShadowRenderGraphPass *m_gameContactShadowRenderGraphPass{nullptr};
    engine::renderGraph::RTReflectionsRenderGraphPass *m_gameRTReflectionsRenderGraphPass{nullptr};
    engine::renderGraph::CinematicEffectsRenderGraphPass *m_gameCinematicEffectsRenderGraphPass{nullptr};
    engine::renderGraph::UIRenderGraphPass *m_gameUIRenderGraphPass{nullptr};

    VkExtent2D m_lastEditorRenderExtent{0u, 0u};
    VkExtent2D m_lastGameRenderExtent{0u, 0u};

    bool m_shouldUpdate{false};
    bool m_isPlaySessionActive{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_RUNTIME_HPP
