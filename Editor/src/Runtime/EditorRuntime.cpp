#include "Editor/Runtime/EditorRuntime.hpp"

#include "Editor/Editor.hpp"
#include "Editor/ImGuiRenderGraphPass.hpp"
#include "Editor/Project.hpp"
#include "Editor/ProjectLoader.hpp"

#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Components/CameraComponent.hpp"
#include "Engine/Components/ParticleSystemComponent.hpp"
#include "Engine/Components/ScriptComponent.hpp"
#include "Engine/PluginSystem/PluginLoader.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Scripting/ScriptsRegister.hpp"
#include "Engine/Scripting/VelixAPI.hpp"

#include "Core/Logger.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace
{
    std::string sharedLibraryExtension()
    {
#if defined(_WIN32)
        return ".dll";
#elif defined(__linux__)
        return ".so";
#else
        return "";
#endif
    }

    std::string toLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    VkExtent2D makeScaledRenderExtent(uint32_t width, uint32_t height)
    {
        const float renderScale = std::clamp(elix::engine::RenderQualitySettings::getInstance().renderScale, 0.25f, 2.0f);
        const uint32_t baseWidth = std::max(width, 1u);
        const uint32_t baseHeight = std::max(height, 1u);

        const uint32_t scaledWidth = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<double>(baseWidth) * renderScale)));
        const uint32_t scaledHeight = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<double>(baseHeight) * renderScale)));
        return VkExtent2D{scaledWidth, scaledHeight};
    }

    int buildArtifactPreference(const std::filesystem::path &path)
    {
        for (const auto &segment : path)
        {
            const std::string loweredSegment = toLowerCopy(segment.string());
            if (loweredSegment == "release")
                return 0;
            if (loweredSegment == "relwithdebinfo")
                return 1;
            if (loweredSegment == "minsizerel")
                return 2;
            if (loweredSegment == "debug")
                return 4;
        }

        return 3;
    }

    std::filesystem::path findGameModuleLibraryPath(const std::filesystem::path &buildDirectory)
    {
        if (buildDirectory.empty() || !std::filesystem::exists(buildDirectory))
            return {};

        const std::string libraryExtension = sharedLibraryExtension();
        if (libraryExtension.empty())
            return {};

        const std::vector<std::filesystem::path> searchRoots = {
            buildDirectory / "Release",
            buildDirectory / "RelWithDebInfo",
            buildDirectory / "MinSizeRel",
            buildDirectory,
            buildDirectory / "Debug",
        };

        const std::vector<std::string> moduleNames = {
            std::string("GameModule") + libraryExtension,
            std::string("libGameModule") + libraryExtension};

        for (const auto &searchRoot : searchRoots)
        {
            if (!std::filesystem::exists(searchRoot))
                continue;

            for (const auto &moduleName : moduleNames)
            {
                const auto candidatePath = searchRoot / moduleName;
                if (std::filesystem::exists(candidatePath) && std::filesystem::is_regular_file(candidatePath))
                    return candidatePath;
            }
        }

        std::filesystem::path bestMatch;
        int bestPreference = std::numeric_limits<int>::max();

        for (const auto &entry : std::filesystem::recursive_directory_iterator(buildDirectory))
        {
            if (!entry.is_regular_file())
                continue;

            const auto path = entry.path();
            if (path.extension() != libraryExtension)
                continue;

            const std::string stem = path.stem().string();
            if (stem == "GameModule" || stem == "libGameModule")
            {
                const int candidatePreference = buildArtifactPreference(path);
                if (candidatePreference < bestPreference ||
                    (candidatePreference == bestPreference && path.string() < bestMatch.string()))
                {
                    bestPreference = candidatePreference;
                    bestMatch = path;
                }
                continue;
            }

            if (path.filename().string().find("GameModule") != std::string::npos)
            {
                const int candidatePreference = buildArtifactPreference(path);
                if (candidatePreference < bestPreference ||
                    (candidatePreference == bestPreference && path.string() < bestMatch.string()))
                {
                    bestPreference = candidatePreference;
                    bestMatch = path;
                }
            }
        }

        return bestMatch;
    }

    bool tryLoadProjectScriptsModule(elix::editor::Project &project,
                                     elix::engine::ScriptsRegister *&outScriptsRegister,
                                     std::string &outModulePath)
    {
        outScriptsRegister = nullptr;
        outModulePath.clear();

        const std::filesystem::path buildDirectory = project.buildDir.empty()
                                                         ? std::filesystem::path(project.fullPath) / "build"
                                                         : std::filesystem::path(project.buildDir);

        const std::filesystem::path moduleLibraryPath = findGameModuleLibraryPath(buildDirectory);
        if (moduleLibraryPath.empty())
        {
            elix::engine::ScriptsRegister::setActiveRegister(nullptr);
            VX_EDITOR_WARNING_STREAM("GameModule not found in build directory. Scripts will remain unavailable until module is built.\n");
            return false;
        }

        if (project.projectLibrary)
        {
            elix::engine::ScriptsRegister::setActiveRegister(nullptr);
            elix::engine::PluginLoader::closeLibrary(project.projectLibrary);
            project.projectLibrary = nullptr;
        }

        project.projectLibrary = elix::engine::PluginLoader::loadLibrary(moduleLibraryPath.string());
        if (!project.projectLibrary)
        {
            elix::engine::ScriptsRegister::setActiveRegister(nullptr);
            VX_EDITOR_ERROR_STREAM("Failed to load game module on startup: " << moduleLibraryPath << '\n');
            return false;
        }

        auto getScriptsRegisterFunction = elix::engine::PluginLoader::getFunction<elix::engine::ScriptsRegister &(*)()>(
            "getScriptsRegister",
            project.projectLibrary);
        if (!getScriptsRegisterFunction)
        {
            elix::engine::PluginLoader::closeLibrary(project.projectLibrary);
            project.projectLibrary = nullptr;
            elix::engine::ScriptsRegister::setActiveRegister(nullptr);
            VX_EDITOR_ERROR_STREAM("Game module loaded but getScriptsRegister symbol is missing: " << moduleLibraryPath << '\n');
            return false;
        }

        outScriptsRegister = &getScriptsRegisterFunction();
        outModulePath = moduleLibraryPath.string();
        elix::engine::ScriptsRegister::setActiveRegister(outScriptsRegister);

        VX_EDITOR_INFO_STREAM("Loaded " << outScriptsRegister->getScripts().size() << " script(s) from " << outModulePath << '\n');
        return true;
    }
} // namespace

ELIX_NESTED_NAMESPACE_BEGIN(editor)

EditorRuntime::EditorRuntime(const engine::ApplicationConfig &config)
{
    if (config.getArgsSize() < 2)
    {
        VX_DEV_ERROR_STREAM("Usage: Velix <project-path>\n");
        throw std::runtime_error("Failed to init editor runtime");
    }

    m_projectPath = std::filesystem::absolute(config.getArgs()[1]).string();
}

bool EditorRuntime::init()
{
    m_project = ProjectLoader::loadProject(m_projectPath);

    if (!m_project)
    {
        VX_DEV_ERROR_STREAM("Failed to load project\n");
        return false;
    }

    engine::ScriptsRegister *startupScriptsRegister = nullptr;
    std::string startupModulePath;
    tryLoadProjectScriptsModule(*m_project, startupScriptsRegister, startupModulePath);

    m_editorScene = std::make_shared<engine::Scene>();
    if (!m_editorScene->loadSceneFromFile(m_project->entryScene))
        throw std::runtime_error("Failed to load scene");

    m_activeScene = m_editorScene;
    engine::scripting::setActiveScene(m_activeScene.get());

    m_shaderHotReloader = std::make_unique<engine::shaders::ShaderHotReloader>("./resources/shaders");
    m_shaderHotReloader->setPollIntervalSeconds(0.35);
    m_shaderHotReloader->prime();

    m_editor = std::make_shared<Editor>();

    m_renderGraph = std::make_unique<engine::renderGraph::RenderGraph>(true, true);

    m_gBufferRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::GBufferRenderGraphPass>();
    m_shadowRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::ShadowRenderGraphPass>();

    m_ssaoRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::SSAORenderGraphPass>(
        m_gBufferRenderGraphPass->getDepthTextureHandler(),
        m_gBufferRenderGraphPass->getNormalTextureHandlers());

    m_lightingRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::LightingRenderGraphPass>(
        m_shadowRenderGraphPass->getDirectionalShadowHandler(),
        m_gBufferRenderGraphPass->getDepthTextureHandler(),
        m_shadowRenderGraphPass->getCubeShadowHandler(),
        m_shadowRenderGraphPass->getSpotShadowHandler(),
        m_gBufferRenderGraphPass->getAlbedoTextureHandlers(),
        m_gBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gBufferRenderGraphPass->getMaterialTextureHandlers(),
        m_gBufferRenderGraphPass->getTangentAnisoTextureHandlers(),
        &m_ssaoRenderGraphPass->getAOHandlers());

    m_contactShadowRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::ContactShadowRenderGraphPass>(
        m_lightingRenderGraphPass->getOutput(),
        m_gBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gBufferRenderGraphPass->getDepthTextureHandler());

    m_ssrRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::SSRRenderGraphPass>(
        m_contactShadowRenderGraphPass->getOutput(),
        m_gBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gBufferRenderGraphPass->getMaterialTextureHandlers(),
        m_gBufferRenderGraphPass->getDepthTextureHandler());

    m_skyLightRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::SkyLightRenderGraphPass>(
        m_ssrRenderGraphPass->getOutput(),
        m_gBufferRenderGraphPass->getDepthTextureHandler());

    m_glassRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::GlassRenderGraphPass>(
        m_skyLightRenderGraphPass->getOutput(),
        m_gBufferRenderGraphPass->getDepthTextureHandler());
    m_glassRenderGraphPass->setScene(m_activeScene.get());

    m_particleRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::ParticleRenderGraphPass>(
        m_glassRenderGraphPass->getHandlers(),
        &m_gBufferRenderGraphPass->getDepthTextureHandler());
    m_particleRenderGraphPass->setScene(m_activeScene.get());

    m_bloomRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::BloomRenderGraphPass>(
        m_particleRenderGraphPass->getHandlers());

    m_tonemapRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::TonemapRenderGraphPass>(
        m_particleRenderGraphPass->getHandlers());

    m_bloomCompositeRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::BloomCompositeRenderGraphPass>(
        m_tonemapRenderGraphPass->getHandlers(),
        m_bloomRenderGraphPass->getHandlers());

    m_fxaaRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::FXAARenderGraphPass>(
        m_bloomCompositeRenderGraphPass->getHandlers());

    m_smaaRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::SMAAPassRenderGraphPass>(
        m_fxaaRenderGraphPass->getHandlers());

    m_cinematicEffectsRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::CinematicEffectsRenderGraphPass>(
        m_smaaRenderGraphPass->getHandlers());

    m_selectionOverlayRenderGraphPass = m_renderGraph->addPass<SelectionOverlayRenderGraphPass>(
        m_editor,
        m_cinematicEffectsRenderGraphPass->getHandlers(),
        m_gBufferRenderGraphPass->getObjectTextureHandler());

    m_uiRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::UIRenderGraphPass>(
        m_selectionOverlayRenderGraphPass->getHandlers());

    m_editorBillboardRenderGraphPass = m_renderGraph->addPass<EditorBillboardRenderGraphPass>(
        m_activeScene,
        m_uiRenderGraphPass->getHandlers());
    m_editorBillboardRenderGraphPass->setCameraIconTexturePath("./resources/textures/velix_logo.tex.elixasset");
    m_editorBillboardRenderGraphPass->setLightIconTexturePath("./resources/textures/velix_logo.tex.elixasset");
    m_editorBillboardRenderGraphPass->setAudioIconTexturePath("./resources/textures/velix_logo.tex.elixasset");

    m_imGuiRenderGraphPass = m_renderGraph->addPass<ImGuiRenderGraphPass>(
        m_editor,
        m_editorBillboardRenderGraphPass->getHandlers(),
        m_gBufferRenderGraphPass->getObjectTextureHandler());

    VkExtent2D previewExtent{.width = 256, .height = 256};
    m_previewAssetsRenderGraphPass = m_renderGraph->addPass<PreviewAssetsRenderGraphPass>(previewExtent);

    m_renderGraph->setup();
    m_renderGraph->createRenderGraphResources();

    m_gameViewportRenderGraph = std::make_unique<engine::renderGraph::RenderGraph>(false, false);

    m_gameGBufferRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::GBufferRenderGraphPass>();
    m_gameShadowRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::ShadowRenderGraphPass>();

    m_gameSSAORenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::SSAORenderGraphPass>(
        m_gameGBufferRenderGraphPass->getDepthTextureHandler(),
        m_gameGBufferRenderGraphPass->getNormalTextureHandlers());

    m_gameLightingRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::LightingRenderGraphPass>(
        m_gameShadowRenderGraphPass->getDirectionalShadowHandler(),
        m_gameGBufferRenderGraphPass->getDepthTextureHandler(),
        m_gameShadowRenderGraphPass->getCubeShadowHandler(),
        m_gameShadowRenderGraphPass->getSpotShadowHandler(),
        m_gameGBufferRenderGraphPass->getAlbedoTextureHandlers(),
        m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gameGBufferRenderGraphPass->getMaterialTextureHandlers(),
        m_gameGBufferRenderGraphPass->getTangentAnisoTextureHandlers(),
        &m_gameSSAORenderGraphPass->getAOHandlers());

    m_gameContactShadowRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::ContactShadowRenderGraphPass>(
        m_gameLightingRenderGraphPass->getOutput(),
        m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gameGBufferRenderGraphPass->getDepthTextureHandler());

    m_gameSSRRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::SSRRenderGraphPass>(
        m_gameContactShadowRenderGraphPass->getOutput(),
        m_gameGBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gameGBufferRenderGraphPass->getMaterialTextureHandlers(),
        m_gameGBufferRenderGraphPass->getDepthTextureHandler());

    m_gameSkyLightRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::SkyLightRenderGraphPass>(
        m_gameSSRRenderGraphPass->getOutput(),
        m_gameGBufferRenderGraphPass->getDepthTextureHandler());

    m_gameGlassRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::GlassRenderGraphPass>(
        m_gameSkyLightRenderGraphPass->getOutput(),
        m_gameGBufferRenderGraphPass->getDepthTextureHandler());
    m_gameGlassRenderGraphPass->setScene(m_activeScene.get());

    m_gameParticleRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::ParticleRenderGraphPass>(
        m_gameGlassRenderGraphPass->getHandlers(),
        &m_gameGBufferRenderGraphPass->getDepthTextureHandler());
    m_gameParticleRenderGraphPass->setScene(m_activeScene.get());

    m_gameBloomRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::BloomRenderGraphPass>(
        m_gameParticleRenderGraphPass->getHandlers());

    m_gameTonemapRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::TonemapRenderGraphPass>(
        m_gameParticleRenderGraphPass->getHandlers());

    m_gameBloomCompositeRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::BloomCompositeRenderGraphPass>(
        m_gameTonemapRenderGraphPass->getHandlers(),
        m_gameBloomRenderGraphPass->getHandlers());

    m_gameFXAARenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::FXAARenderGraphPass>(
        m_gameBloomCompositeRenderGraphPass->getHandlers());

    m_gameSMAARenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::SMAAPassRenderGraphPass>(
        m_gameFXAARenderGraphPass->getHandlers());

    m_gameCinematicEffectsRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::CinematicEffectsRenderGraphPass>(
        m_gameSMAARenderGraphPass->getHandlers());

    m_gameUIRenderGraphPass = m_gameViewportRenderGraph->addPass<engine::renderGraph::UIRenderGraphPass>(
        m_gameCinematicEffectsRenderGraphPass->getHandlers());

    m_gameViewportRenderGraph->setup();
    m_gameViewportRenderGraph->createRenderGraphResources();

    m_iblManager.createFallback();
    m_lightingRenderGraphPass->setIBLManager(&m_iblManager);
    m_gameLightingRenderGraphPass->setIBLManager(&m_iblManager);

    m_editor->addOnViewportChangedCallback([this](uint32_t width, uint32_t height)
                                           { applyEditorViewportExtent(width, height); });
    m_editor->addOnGameViewportChangedCallback([this](uint32_t width, uint32_t height)
                                               { applyGameViewportExtent(width, height); });

    applyEditorViewportExtent(m_editor->getViewportX(), m_editor->getViewportY());
    applyGameViewportExtent(m_editor->getGameViewportX(), m_editor->getGameViewportY());

    m_editor->setScene(m_activeScene);
    m_editor->setProject(m_project);
    m_editor->setProjectScriptsRegister(startupScriptsRegister, startupModulePath);

    m_editorRenderCamera = m_editor->getCurrentCamera();
    if (m_editorRenderCamera)
        m_editorRenderCamera->setPosition({0.0f, 1.0f, 5.0f});

    auto forEachScriptComponent = [](const std::shared_ptr<engine::Scene> &scene, auto &&function)
    {
        if (!scene)
            return;

        std::vector<engine::Entity::SharedPtr> entitiesSnapshot;
        entitiesSnapshot.reserve(scene->getEntities().size());
        for (const auto &entity : scene->getEntities())
            entitiesSnapshot.push_back(entity);

        for (const auto &entity : entitiesSnapshot)
        {
            if (!entity)
                continue;

            for (auto *scriptComponent : entity->getComponents<engine::ScriptComponent>())
                function(scriptComponent);
        }
    };

    auto switchActiveScene = [this](const std::shared_ptr<engine::Scene> &scene)
    {
        if (!scene)
            return;

        m_activeScene = scene;
        engine::scripting::setActiveScene(m_activeScene.get());
        m_editor->setScene(m_activeScene);

        if (m_particleRenderGraphPass)
            m_particleRenderGraphPass->setScene(m_activeScene.get());
        if (m_glassRenderGraphPass)
            m_glassRenderGraphPass->setScene(m_activeScene.get());
        if (m_gameGlassRenderGraphPass)
            m_gameGlassRenderGraphPass->setScene(m_activeScene.get());
        if (m_gameParticleRenderGraphPass)
            m_gameParticleRenderGraphPass->setScene(m_activeScene.get());
        if (m_editorBillboardRenderGraphPass)
            m_editorBillboardRenderGraphPass->setScene(m_activeScene);

        m_gameRenderCamera = nullptr;
        for (const auto &entity : m_activeScene->getEntities())
        {
            if (!entity || !entity->isEnabled())
                continue;

            if (auto *cameraComponent = entity->getComponent<engine::CameraComponent>())
            {
                m_gameRenderCamera = cameraComponent->getCamera();
                break;
            }
        }
    };

    switchActiveScene(m_editorScene);

    m_editor->addOnModeChangedCallback([this, switchActiveScene, forEachScriptComponent](Editor::EditorMode mode)
                                       {
                                           if (mode == Editor::EditorMode::PLAY)
                                           {
                                               if (!m_isPlaySessionActive)
                                               {
                                                   m_playScene = m_editorScene ? m_editorScene->copy() : nullptr;
                                                   if (!m_playScene)
                                                   {
                                                       VX_EDITOR_ERROR_STREAM("Failed to create play scene copy.\n");
                                                       m_shouldUpdate = false;
                                                       return;
                                                   }

                                                   switchActiveScene(m_playScene);
                                                   forEachScriptComponent(m_activeScene, [](engine::ScriptComponent *scriptComponent)
                                                                          {
                                                                              if (scriptComponent)
                                                                                  scriptComponent->onAttach();
                                                                          });
                                                   m_isPlaySessionActive = true;
                                               }

                                               m_shouldUpdate = true;
                                           }
                                           else if (mode == Editor::EditorMode::PAUSE)
                                           {
                                               m_shouldUpdate = false;
                                           }
                                           else if (mode == Editor::EditorMode::EDIT)
                                           {
                                               if (m_isPlaySessionActive)
                                               {
                                                   forEachScriptComponent(m_activeScene, [](engine::ScriptComponent *scriptComponent)
                                                                          {
                                                                              if (scriptComponent)
                                                                                  scriptComponent->onDetach();
                                                                          });

                                                   m_playScene.reset();
                                                   m_isPlaySessionActive = false;
                                               }

                                               switchActiveScene(m_editorScene);
                                               m_shouldUpdate = false;
                                           } });

    return true;
}

void EditorRuntime::tick(float deltaTime)
{
    if (!m_activeScene || !m_editor || !m_renderGraph)
        return;

    if (m_shouldUpdate)
        m_activeScene->update(deltaTime);
    else
    {
        m_editor->updateAnimationPreview(deltaTime);

        if (!m_isPlaySessionActive)
        {
            for (const auto &entity : m_activeScene->getEntities())
            {
                if (!entity || !entity->isEnabled())
                    continue;

                for (auto *particleSystemComponent : entity->getComponents<engine::ParticleSystemComponent>())
                {
                    if (!particleSystemComponent)
                        continue;

                    particleSystemComponent->update(deltaTime);
                }
            }
        }
    }

    m_previewAssetsRenderGraphPass->clearJobs();
    for (const auto &previewJob : m_editor->getRequestedPreviewJobs())
    {
        PreviewAssetsRenderGraphPass::PreviewJob job;
        job.material = previewJob.material;
        job.mesh = previewJob.mesh;
        job.modelTransform = previewJob.modelTransform;
        m_previewAssetsRenderGraphPass->addPreviewJob(job);
    }

    const uint32_t editorViewportWidth = m_editor->getViewportX();
    const uint32_t editorViewportHeight = m_editor->getViewportY();
    applyEditorViewportExtent(editorViewportWidth, editorViewportHeight);
    if (m_editorRenderCamera && editorViewportWidth > 0 && editorViewportHeight > 0)
        m_editorRenderCamera->setAspect(static_cast<float>(editorViewportWidth) / static_cast<float>(editorViewportHeight));

    m_gameRenderCamera = nullptr;
    for (const auto &entity : m_activeScene->getEntities())
    {
        if (!entity || !entity->isEnabled())
            continue;

        if (auto *cameraComponent = entity->getComponent<engine::CameraComponent>())
        {
            m_gameRenderCamera = cameraComponent->getCamera();
            break;
        }
    }

    const uint32_t gameViewportWidth = m_editor->getGameViewportX();
    const uint32_t gameViewportHeight = m_editor->getGameViewportY();
    applyGameViewportExtent(gameViewportWidth, gameViewportHeight);
    if (m_gameRenderCamera && gameViewportWidth > 0 && gameViewportHeight > 0)
        m_gameRenderCamera->setAspect(static_cast<float>(gameViewportWidth) / static_cast<float>(gameViewportHeight));

    engine::ui::UIRenderData uiData;
    for (const auto &text : m_activeScene->getUITexts())
        if (text && text->isEnabled())
            uiData.texts.push_back(text.get());
    for (const auto &button : m_activeScene->getUIButtons())
        if (button && button->isEnabled())
            uiData.buttons.push_back(button.get());
    for (const auto &billboard : m_activeScene->getBillboards())
        if (billboard && billboard->isEnabled())
            uiData.billboards.push_back(billboard.get());

    if (m_uiRenderGraphPass)
        m_uiRenderGraphPass->setRenderData(uiData);
    if (m_gameUIRenderGraphPass)
        m_gameUIRenderGraphPass->setRenderData(uiData);

    if (m_shadowRenderGraphPass)
        m_shadowRenderGraphPass->syncQualitySettings();
    if (m_gameShadowRenderGraphPass)
        m_gameShadowRenderGraphPass->syncQualitySettings();

    const bool shouldRenderGameViewport = m_isPlaySessionActive || m_editor->isGameViewportVisible();

    if (m_gameViewportRenderGraph && m_gameRenderCamera && shouldRenderGameViewport)
    {
        m_gameViewportRenderGraph->prepareFrame(m_gameRenderCamera, m_activeScene.get(), deltaTime);
        m_gameViewportRenderGraph->draw();

        if (m_imGuiRenderGraphPass && m_gameUIRenderGraphPass)
            m_imGuiRenderGraphPass->setGameViewportImages(
                m_gameUIRenderGraphPass->getOutputImageViews(),
                true,
                m_gameViewportRenderGraph->getCurrentImageIndex());
    }
    else if (m_imGuiRenderGraphPass)
    {
        m_imGuiRenderGraphPass->setGameViewportImages({}, m_gameRenderCamera != nullptr, 0u);
    }

    m_renderGraph->prepareFrame(m_editorRenderCamera, m_activeScene.get(), deltaTime);
    m_editor->setRenderGraphProfilingData(m_renderGraph->getLastFrameProfilingData());
    m_renderGraph->draw();
    m_editor->processPendingObjectSelection();

    m_editor->setDonePreviewJobs(m_previewAssetsRenderGraphPass->getRenderedImages());
}

void EditorRuntime::applyEditorViewportExtent(uint32_t width, uint32_t height)
{
    const VkExtent2D extent = makeScaledRenderExtent(width, height);
    if (extent.width == m_lastEditorRenderExtent.width && extent.height == m_lastEditorRenderExtent.height)
        return;

    m_gBufferRenderGraphPass->setExtent(extent);
    m_ssaoRenderGraphPass->setExtent(extent);
    m_lightingRenderGraphPass->setExtent(extent);
    m_ssrRenderGraphPass->setExtent(extent);
    m_skyLightRenderGraphPass->setExtent(extent);
    m_bloomRenderGraphPass->setExtent(extent);
    m_tonemapRenderGraphPass->setExtent(extent);
    m_bloomCompositeRenderGraphPass->setExtent(extent);
    m_fxaaRenderGraphPass->setExtent(extent);
    m_smaaRenderGraphPass->setExtent(extent);
    m_contactShadowRenderGraphPass->setExtent(extent);
    m_cinematicEffectsRenderGraphPass->setExtent(extent);
    m_selectionOverlayRenderGraphPass->setExtent(extent);
    m_uiRenderGraphPass->setExtent(extent);
    m_editorBillboardRenderGraphPass->setExtent(extent);
    m_particleRenderGraphPass->setExtent(extent);
    m_glassRenderGraphPass->setExtent(extent);

    m_lastEditorRenderExtent = extent;
}

void EditorRuntime::applyGameViewportExtent(uint32_t width, uint32_t height)
{
    const VkExtent2D extent = makeScaledRenderExtent(width, height);
    if (extent.width == m_lastGameRenderExtent.width && extent.height == m_lastGameRenderExtent.height)
        return;

    m_gameGBufferRenderGraphPass->setExtent(extent);
    m_gameSSAORenderGraphPass->setExtent(extent);
    m_gameLightingRenderGraphPass->setExtent(extent);
    m_gameSSRRenderGraphPass->setExtent(extent);
    m_gameSkyLightRenderGraphPass->setExtent(extent);
    m_gameBloomRenderGraphPass->setExtent(extent);
    m_gameTonemapRenderGraphPass->setExtent(extent);
    m_gameBloomCompositeRenderGraphPass->setExtent(extent);
    m_gameFXAARenderGraphPass->setExtent(extent);
    m_gameSMAARenderGraphPass->setExtent(extent);
    m_gameContactShadowRenderGraphPass->setExtent(extent);
    m_gameCinematicEffectsRenderGraphPass->setExtent(extent);
    m_gameUIRenderGraphPass->setExtent(extent);
    m_gameParticleRenderGraphPass->setExtent(extent);
    m_gameGlassRenderGraphPass->setExtent(extent);

    m_lastGameRenderExtent = extent;
}

void EditorRuntime::shutdown()
{
    engine::ScriptsRegister::setActiveRegister(nullptr);

    if (m_renderGraph)
        m_renderGraph->cleanResources();

    if (m_gameViewportRenderGraph)
        m_gameViewportRenderGraph->cleanResources();

    engine::scripting::setActiveScene(nullptr);
    engine::scripting::setActiveWindow(nullptr);

    if (m_project && m_project->projectLibrary)
    {
        engine::PluginLoader::closeLibrary(m_project->projectLibrary);
        m_project->projectLibrary = nullptr;
    }

    if (m_project)
        m_project->clearCache();
}

ELIX_NESTED_NAMESPACE_END
