#include <iostream>

#include "Core/Window.hpp"
#include "Core/VulkanContext.hpp"

#include "Editor/Editor.hpp"
#include "Editor/ImGuiRenderGraphPass.hpp"

#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/TonemapRenderGraphPass.hpp"
#include "Engine/Render/RenderGraph/RenderGraph.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Primitives.hpp"
#include "Engine/Assets/OBJAssetLoader.hpp"
#include "Engine/Assets/FBXAssetLoader.hpp"
#include "Engine/Assets/MaterialAssetLoader.hpp"
#include "Engine/Physics/PhysXCore.hpp"
#include "Engine/Components/CameraComponent.hpp"
#include "Engine/Caches/GraphicsPipelineCache.hpp"
#include "Editor/RenderGraphPasses/PreviewAssetsRenderGraphPass.hpp"
#include "Editor/RenderGraphPasses/SelectionOverlayRenderGraphPass.hpp"

#include "Engine/Render/GraphPasses/GBufferRenderGraphPass.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Shaders/ShaderHotReloader.hpp"

#include "Engine/Render/GraphPasses/LightingRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/SkyLightRenderGraphPass.hpp"

#include "Editor/FileHelper.hpp"

#include "Editor/ProjectLoader.hpp"

#include "Core/Logger.hpp"

#include <filesystem>

#include <GLFW/glfw3.h>

#include <chrono>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        VX_DEV_ERROR_STREAM("Usage: Velix <project-path>\n");
        return 1;
    }

    const auto projectPath = std::filesystem::absolute(argv[1]).string();
    const auto executableDir = elix::editor::FileHelper::getExecutablePath();

    // Make terminal launches behave like the VSCode launch configuration by resolving resources from the repo root.
    const auto repoRootFromBuild = executableDir.parent_path();
    if (!repoRootFromBuild.empty() && std::filesystem::exists(repoRootFromBuild / "resources"))
        std::filesystem::current_path(repoRootFromBuild);

    auto start = std::chrono::high_resolution_clock::now();

    if (!glfwInit())
    {
        VX_DEV_ERROR_STREAM("Failed to initialize GLFW\n");
        return 1;
    }

    elix::core::Logger::createDefaultLogger();
    elix::engine::PhysXCore::init();

    elix::engine::AssetsLoader::registerAssetLoader(std::make_shared<elix::engine::OBJAssetLoader>());
    elix::engine::AssetsLoader::registerAssetLoader(std::make_shared<elix::engine::FBXAssetLoader>());
    elix::engine::AssetsLoader::registerAssetLoader(std::make_shared<elix::engine::MaterialAssetLoader>());

    auto window = elix::platform::Window::create(800, 600, "Velix", elix::platform::Window::WindowFlags::EWINDOW_FLAGS_DEFAULT);
    auto vulkanContext = elix::core::VulkanContext::create(window);
    auto props = vulkanContext->getPhysicalDevicePoperties();

    const std::string graphicsPipelineCache = elix::editor::FileHelper::getExecutablePath().string() +
                                              "/pipeline_cache_" + std::string(props.deviceName) + "_" + std::to_string(props.vendorID) + '_' +
                                              std::to_string(props.driverVersion) + ".elixgpbin";

    elix::engine::cache::GraphicsPipelineCache::loadCacheFromFile(vulkanContext->getDevice(), graphicsPipelineCache);

    auto project = elix::editor::ProjectLoader::loadProject(projectPath);

    if (!project)
    {
        VX_DEV_ERROR_STREAM("Failed to load project\n");
        return 1;
    }

    auto scene = std::make_shared<elix::engine::Scene>();

    if (!scene->loadSceneFromFile(project->entryScene))
        throw std::runtime_error("Failed to load scene");

    elix::engine::GraphicsPipelineManager::init();
    elix::engine::shaders::ShaderHotReloader shaderHotReloader("./resources/shaders");
    shaderHotReloader.setPollIntervalSeconds(0.35);
    shaderHotReloader.prime();

    auto renderGraph = new elix::engine::renderGraph::RenderGraph(vulkanContext->getDevice(), vulkanContext->getSwapchain());
    auto editor = std::make_shared<elix::editor::Editor>();

    auto shadowRenderPass = renderGraph->addPass<elix::engine::renderGraph::ShadowRenderGraphPass>();
    auto shadowId = renderGraph->getRenderGraphPassId(shadowRenderPass);

    auto gBufferRenderGraphPass = renderGraph->addPass<elix::engine::renderGraph::GBufferRenderGraphPass>();

    auto gbufferId = renderGraph->getRenderGraphPassId(gBufferRenderGraphPass);

    auto lightingRenderGraphPass = renderGraph->addPass<elix::engine::renderGraph::LightingRenderGraphPass>(shadowId, gbufferId, shadowRenderPass->getShadowHandler(),
                                                                                                            gBufferRenderGraphPass->getDepthTextureHandler(), gBufferRenderGraphPass->getAlbedoTextureHandlers(), gBufferRenderGraphPass->getNormalTextureHandlers(),
                                                                                                            gBufferRenderGraphPass->getMaterialTextureHandlers());

    auto lightingId = renderGraph->getRenderGraphPassId(lightingRenderGraphPass);

    auto skyLightRenderGraphPass = renderGraph->addPass<elix::engine::renderGraph::SkyLightRenderGraphPass>(
        lightingId, gbufferId, lightingRenderGraphPass->getOutput(), gBufferRenderGraphPass->getDepthTextureHandler());

    auto skyLightId = renderGraph->getRenderGraphPassId(skyLightRenderGraphPass);

    auto toneMapRenderGraphPass = renderGraph->addPass<elix::engine::renderGraph::TonemapRenderGraphPass>(skyLightId, skyLightRenderGraphPass->getOutput());

    auto toneMapId = renderGraph->getRenderGraphPassId(toneMapRenderGraphPass);

    auto selectionOverlayRenderGraphPass = renderGraph->addPass<elix::editor::SelectionOverlayRenderGraphPass>(
        editor, toneMapId, gbufferId, toneMapRenderGraphPass->getHandlers(), gBufferRenderGraphPass->getObjectTextureHandler());

    auto selectionOverlayId = renderGraph->getRenderGraphPassId(selectionOverlayRenderGraphPass);

    auto imguiRenderGraphPass = renderGraph->addPass<elix::editor::ImGuiRenderGraphPass>(editor, selectionOverlayId, selectionOverlayRenderGraphPass->getHandlers(),
                                                                                         gBufferRenderGraphPass->getObjectTextureHandler());

    VkExtent2D extent{.width = 50, .height = 30};

    auto previewRenderGraphPass = renderGraph->addPass<elix::editor::PreviewAssetsRenderGraphPass>(extent);

    renderGraph->setup();
    renderGraph->createRenderGraphResources();

    elix::engine::Texture::createDefaults();
    elix::engine::Material::createDefaultMaterial(elix::engine::Texture::getDefaultWhiteTexture());

    editor->addOnViewportChangedCallback([&](uint32_t w, uint32_t h)
                                         {
        VkExtent2D extent{.width = w, .height = h};
        gBufferRenderGraphPass->setExtent(extent); 
        lightingRenderGraphPass->setExtent(extent);
        skyLightRenderGraphPass->setExtent(extent);
        toneMapRenderGraphPass->setExtent(extent);
        selectionOverlayRenderGraphPass->setExtent(extent); });

    float lastFrame = 0.0f;
    float deltaTime = 0.0f;
    const float fixedStep = 1.0f / 60.0f;
    float accumulator = 0.0f;

    editor->setScene(scene);
    editor->setProject(project);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    VX_DEV_INFO_STREAM("Engine load took: " << duration.count() << " microseconds\n");
    VX_DEV_INFO_STREAM("Engine load took: " << duration.count() / 1000.0 << " milliseconds\n");
    VX_DEV_INFO_STREAM("Engine load took: " << duration.count() / 1000000.0 << " seconds\n");

    elix::engine::Camera::SharedPtr currentRenderCamera = editor->getCurrentCamera();

    currentRenderCamera->setPosition({0.0f, 1.0f, 5.0f});

    bool shouldUpdate{false};

    editor->addOnModeChangedCallback([&](elix::editor::Editor::EditorMode mode)
                                     {
        if(mode == elix::editor::Editor::EditorMode::PLAY)
        {
            currentRenderCamera = nullptr;
            for(const auto& entity : scene->getEntities())
            {
                //Find first camera(mabye we can make check box to make sure that this camera should be the rendered one)
                if(auto cameraComponent = entity->getComponent<elix::engine::CameraComponent>())
                {   
                    currentRenderCamera = cameraComponent->getCamera();
                    break;
                }
            }

            for(const auto& entity : scene->getEntities())
            {
                for(const auto& [_, component] : entity->getSingleComponents())
                    component->onAttach();
            }

            shouldUpdate = true;
        } 
        else if(mode == elix::editor::Editor::EditorMode::PAUSE)
        {
            shouldUpdate = false;
        }
        else if(mode == elix::editor::Editor::EditorMode::EDIT)
        {
            currentRenderCamera = editor->getCurrentCamera();
            shouldUpdate = false;
        } });

    while (window->isOpen())
    {
        const float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        window->pollEvents();

        shaderHotReloader.update(deltaTime);

        bool shouldReloadShaders = shaderHotReloader.consumeReloadRequest();
        if (editor->consumeShaderReloadRequest())
            shouldReloadShaders = true;

        if (shouldReloadShaders)
        {
            vkDeviceWaitIdle(vulkanContext->getDevice());
            elix::engine::GraphicsPipelineManager::reloadShaders();
        }

        if (shouldUpdate)
            scene->update(deltaTime);
        else
            editor->updateAnimationPreview(deltaTime);

        previewRenderGraphPass->clearJobs();

        for (const auto &previewJob : editor->getRequestedPreviewJobs())
        {
            elix::editor::PreviewAssetsRenderGraphPass::PreviewJob job;
            job.material = previewJob.material;
            job.mesh = previewJob.mesh;
            job.modelTransform = previewJob.modelTransform;
            previewRenderGraphPass->addPreviewJob(job);
        }

        const uint32_t viewportWidth = editor->getViewportX();
        const uint32_t viewportHeight = editor->getViewportY();
        if (currentRenderCamera && viewportWidth > 0 && viewportHeight > 0)
            currentRenderCamera->setAspect(static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight));

        renderGraph->prepareFrame(currentRenderCamera, scene.get(), deltaTime);
        editor->setRenderGraphProfilingData(renderGraph->getLastFrameProfilingData());
        renderGraph->draw();
        editor->processPendingObjectSelection();

        editor->setDonePreviewJobs(previewRenderGraphPass->getRenderedImages());
    }

    elix::engine::cache::GraphicsPipelineCache::saveCacheToFile(vulkanContext->getDevice(), graphicsPipelineCache);
    elix::engine::GraphicsPipelineManager::destroy();
    elix::engine::cache::GraphicsPipelineCache::deleteCache(vulkanContext->getDevice());

    project->clearCache();
    // To clean all needed Vulkan resources before VulkanContex is cleared
    renderGraph->cleanResources();
    delete renderGraph;
    editor.reset();
    scene.reset();
    elix::engine::Material::deleteDefaultMaterial();
    elix::engine::Texture::destroyDefaults();
    //

    vulkanContext->cleanup();

    elix::engine::PhysXCore::shutdown();

    glfwTerminate();

    return 0;
}
