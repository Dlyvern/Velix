#include "Editor/Runtime/EditorRuntime.hpp"

#include "Editor/Editor.hpp"
#include "Editor/ImGuiRenderGraphPass.hpp"
#include "Editor/Project.hpp"
#include "Editor/ProjectLoader.hpp"

#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Components/CameraComponent.hpp"
#include "Engine/Components/ScriptComponent.hpp"
#include "Engine/PluginSystem/PluginLoader.hpp"
#include "Engine/Scripting/VelixAPI.hpp"

#include "Core/Logger.hpp"

#include <filesystem>
#include <vector>

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

    m_scene = std::make_shared<engine::Scene>();
    engine::scripting::setActiveScene(m_scene.get());

    if (!m_scene->loadSceneFromFile(m_project->entryScene))
        throw std::runtime_error("Failed to load scene");

    m_shaderHotReloader = std::make_unique<engine::shaders::ShaderHotReloader>("./resources/shaders");
    m_shaderHotReloader->setPollIntervalSeconds(0.35);
    m_shaderHotReloader->prime();

    m_renderGraph = std::make_unique<engine::renderGraph::RenderGraph>();

    m_editor = std::make_shared<Editor>();

    m_gBufferRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::GBufferRenderGraphPass>();
    m_shadowRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::ShadowRenderGraphPass>();

    m_lightingRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::LightingRenderGraphPass>(
        m_shadowRenderGraphPass->getDirectionalShadowHandler(),
        m_gBufferRenderGraphPass->getDepthTextureHandler(),
        m_shadowRenderGraphPass->getCubeShadowHandler(),
        m_shadowRenderGraphPass->getSpotShadowHandler(),
        m_gBufferRenderGraphPass->getAlbedoTextureHandlers(),
        m_gBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gBufferRenderGraphPass->getMaterialTextureHandlers());

    // SSR reads HDR lighting + G-buffer; outputs modified HDR (pass-through when disabled)
    m_ssrRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::SSRRenderGraphPass>(
        m_lightingRenderGraphPass->getOutput(),
        m_gBufferRenderGraphPass->getNormalTextureHandlers(),
        m_gBufferRenderGraphPass->getMaterialTextureHandlers(),
        m_gBufferRenderGraphPass->getDepthTextureHandler());

    // SkyLight (IBL + skybox) reads SSR-modified HDR
    m_skyLightRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::SkyLightRenderGraphPass>(
        m_ssrRenderGraphPass->getOutput(),
        m_gBufferRenderGraphPass->getDepthTextureHandler());

    // Bloom extract: reads HDR (before tonemap), outputs half-res bright texture
    m_bloomRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::BloomRenderGraphPass>(
        m_skyLightRenderGraphPass->getOutput());

    // Tonemap: HDR → LDR
    m_tonemapRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::TonemapRenderGraphPass>(
        m_skyLightRenderGraphPass->getOutput());

    // Bloom composite: upsample + add bloom to LDR
    m_bloomCompositeRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::BloomCompositeRenderGraphPass>(
        m_tonemapRenderGraphPass->getHandlers(),
        m_bloomRenderGraphPass->getHandlers());

    // FXAA: anti-alias the final LDR image
    m_fxaaRenderGraphPass = m_renderGraph->addPass<engine::renderGraph::FXAARenderGraphPass>(
        m_bloomCompositeRenderGraphPass->getHandlers());

    // Editor overlays read FXAA output
    m_selectionOverlayRenderGraphPass = m_renderGraph->addPass<SelectionOverlayRenderGraphPass>(
        m_editor,
        m_fxaaRenderGraphPass->getHandlers(),
        m_gBufferRenderGraphPass->getObjectTextureHandler());

    m_renderGraph->addPass<ImGuiRenderGraphPass>(
        m_editor,
        m_selectionOverlayRenderGraphPass->getHandlers(),
        m_gBufferRenderGraphPass->getObjectTextureHandler());

    VkExtent2D extent{.width = 256, .height = 256};
    m_previewAssetsRenderGraphPass = m_renderGraph->addPass<PreviewAssetsRenderGraphPass>(extent);

    m_renderGraph->setup();
    m_renderGraph->createRenderGraphResources();

    m_editor->addOnViewportChangedCallback([&](uint32_t w, uint32_t h)
                                           {
                                               VkExtent2D extent{.width = w, .height = h};
                                               m_gBufferRenderGraphPass->setExtent(extent);
                                               m_lightingRenderGraphPass->setExtent(extent);
                                               m_ssrRenderGraphPass->setExtent(extent);
                                               m_skyLightRenderGraphPass->setExtent(extent);
                                               m_bloomRenderGraphPass->setExtent(extent);
                                               m_tonemapRenderGraphPass->setExtent(extent);
                                               m_bloomCompositeRenderGraphPass->setExtent(extent);
                                               m_fxaaRenderGraphPass->setExtent(extent);
                                               m_selectionOverlayRenderGraphPass->setExtent(extent); });

    m_editor->setScene(m_scene);
    m_editor->setProject(m_project);

    m_currentRenderCamera = m_editor->getCurrentCamera();
    if (m_currentRenderCamera)
        m_currentRenderCamera->setPosition({0.0f, 1.0f, 5.0f});

    auto forEachScriptComponent = [&](auto &&function)
    {
        // Scripts can spawn/destroy entities in onStart/onStop.
        // Copy shared_ptrs first to keep iteration stable and objects alive.
        std::vector<engine::Entity::SharedPtr> entitiesSnapshot;
        entitiesSnapshot.reserve(m_scene->getEntities().size());
        for (const auto &entity : m_scene->getEntities())
            entitiesSnapshot.push_back(entity);

        for (const auto &entity : entitiesSnapshot)
        {
            if (!entity)
                continue;

            for (auto *scriptComponent : entity->getComponents<engine::ScriptComponent>())
                function(scriptComponent);
        }
    };

    m_editor->addOnModeChangedCallback([&](Editor::EditorMode mode)
                                       {
                                           if (mode == Editor::EditorMode::PLAY)
                                           {
                                               if (!m_isPlaySessionActive)
                                               {
                                                   m_currentRenderCamera = nullptr;

                                                   for (const auto &entity : m_scene->getEntities())
                                                   {
                                                       if (!entity || !entity->isEnabled())
                                                           continue;

                                                       // Find first camera for play rendering.
                                                       if (auto cameraComponent = entity->getComponent<engine::CameraComponent>())
                                                       {
                                                           m_currentRenderCamera = cameraComponent->getCamera();
                                                           break;
                                                       }
                                                   }

                                                   for (const auto &entity : m_scene->getEntities())
                                                   {
                                                       if (!entity || !entity->isEnabled())
                                                           continue;

                                                       for (auto *scriptComponent : entity->getComponents<engine::ScriptComponent>())
                                                           scriptComponent->onAttach();
                                                   }

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
                                                   forEachScriptComponent([](engine::ScriptComponent *scriptComponent)
                                                                          { scriptComponent->onDetach(); });

                                                   m_isPlaySessionActive = false;
                                               }

                                               m_currentRenderCamera = m_editor->getCurrentCamera();
                                               m_shouldUpdate = false;
                                           } });

    return true;
}

void EditorRuntime::tick(float deltaTime)
{
    // m_shaderHotReloader->update(deltaTime);

    // bool shouldReloadShaders = m_shaderHotReloader->consumeReloadRequest();
    // if (m_editor->consumeShaderReloadRequest())
    //     shouldReloadShaders = true;

    // if (shouldReloadShaders)
    // {
    //     vkDeviceWaitIdle(core::VulkanContext::getContext()->getDevice());
    //     engine::GraphicsPipelineManager::reloadShaders();
    // }

    if (m_shouldUpdate)
        m_scene->update(deltaTime);
    else
        m_editor->updateAnimationPreview(deltaTime);

    m_previewAssetsRenderGraphPass->clearJobs();

    for (const auto &previewJob : m_editor->getRequestedPreviewJobs())
    {
        PreviewAssetsRenderGraphPass::PreviewJob job;
        job.material = previewJob.material;
        job.mesh = previewJob.mesh;
        job.modelTransform = previewJob.modelTransform;
        m_previewAssetsRenderGraphPass->addPreviewJob(job);
    }

    const uint32_t viewportWidth = m_editor->getViewportX();
    const uint32_t viewportHeight = m_editor->getViewportY();
    if (m_currentRenderCamera && viewportWidth > 0 && viewportHeight > 0)
        m_currentRenderCamera->setAspect(static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight));

    m_renderGraph->prepareFrame(m_currentRenderCamera, m_scene.get(), deltaTime);
    m_editor->setRenderGraphProfilingData(m_renderGraph->getLastFrameProfilingData());
    m_renderGraph->draw();
    m_editor->processPendingObjectSelection();

    m_editor->setDonePreviewJobs(m_previewAssetsRenderGraphPass->getRenderedImages());
}

void EditorRuntime::shutdown()
{
    if (m_renderGraph)
        m_renderGraph->cleanResources();

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
