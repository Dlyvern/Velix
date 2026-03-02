#ifndef ELIX_GAME_RUNTIME_HPP
#define ELIX_GAME_RUNTIME_HPP

#include "Core/Macros.hpp"

#include "Engine/Assets/ElixPacket.hpp"
#include "Engine/Camera.hpp"
#include "Engine/PluginSystem/PluginLoader.hpp"
#include "Engine/Render/GraphPasses/BloomCompositeRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/BloomRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/FXAARenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/GBufferRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/LightingRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/ParticleRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/PresentRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/SMAAPassRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/SSAORenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/SSRRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/SkyLightRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/TonemapRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/UIRenderGraphPass.hpp"
#include "Engine/Render/RenderGraph/RenderGraph.hpp"
#include "Engine/Runtime/ApplicationConfig.hpp"
#include "Engine/Runtime/IRuntime.hpp"
#include "Engine/Scene.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ScriptComponent;

class GameRuntime final : public IRuntime
{
public:
    explicit GameRuntime(const ApplicationConfig &config);

    static bool shouldRunForConfig(const ApplicationConfig &config);

    bool init() override;
    void tick(float deltaTime) override;
    void shutdown() override;

private:
    bool resolveLaunchPaths(std::string *errorMessage);
    bool loadProjectModule(std::string *errorMessage);
    bool extractPacket(std::string *errorMessage);
    void bindSceneToPasses();
    void syncViewportExtent();
    void refreshActiveCamera();
    void collectAndSubmitUIRenderData();
    void forEachScriptComponent(const std::function<void(ScriptComponent *)> &function);

    static std::filesystem::path currentExecutablePath();
    static std::filesystem::path currentExecutableDirectory();
    static std::filesystem::path findPacketInDirectory(const std::filesystem::path &directory);
    static std::filesystem::path findGameModuleInDirectory(const std::filesystem::path &directory);
    static std::vector<std::filesystem::path> findVelixSdkLibrariesInDirectory(const std::filesystem::path &directory);
    static std::string toLowerCopy(std::string value);

private:
    std::vector<std::string> m_args;

    std::filesystem::path m_packetPath;
    std::filesystem::path m_modulePath;
    std::filesystem::path m_extractionDirectory;
    std::filesystem::path m_entryScenePath;
    ElixPacketManifest m_packetManifest{};

    LibraryHandle m_gameModuleLibrary{nullptr};
    std::vector<LibraryHandle> m_preloadedRuntimeLibraries;

    Scene::SharedPtr m_scene{nullptr};
    Camera::SharedPtr m_renderCamera{nullptr};
    std::unique_ptr<renderGraph::RenderGraph> m_renderGraph{nullptr};

    renderGraph::GBufferRenderGraphPass *m_gBufferRenderGraphPass{nullptr};
    renderGraph::ShadowRenderGraphPass *m_shadowRenderGraphPass{nullptr};
    renderGraph::SSAORenderGraphPass *m_ssaoRenderGraphPass{nullptr};
    renderGraph::LightingRenderGraphPass *m_lightingRenderGraphPass{nullptr};
    renderGraph::SSRRenderGraphPass *m_ssrRenderGraphPass{nullptr};
    renderGraph::SkyLightRenderGraphPass *m_skyLightRenderGraphPass{nullptr};
    renderGraph::ParticleRenderGraphPass *m_particleRenderGraphPass{nullptr};
    renderGraph::BloomRenderGraphPass *m_bloomRenderGraphPass{nullptr};
    renderGraph::TonemapRenderGraphPass *m_tonemapRenderGraphPass{nullptr};
    renderGraph::BloomCompositeRenderGraphPass *m_bloomCompositeRenderGraphPass{nullptr};
    renderGraph::FXAARenderGraphPass *m_fxaaRenderGraphPass{nullptr};
    renderGraph::SMAAPassRenderGraphPass *m_smaaRenderGraphPass{nullptr};
    renderGraph::UIRenderGraphPass *m_uiRenderGraphPass{nullptr};
    renderGraph::PresentRenderGraphPass *m_presentRenderGraphPass{nullptr};

    VkExtent2D m_lastExtent{0u, 0u};
    bool m_scriptsAttached{false};
    bool m_initialized{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_GAME_RUNTIME_HPP
