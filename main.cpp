#include <iostream>

#include "Core/Window.hpp"
#include "Core/VulkanContext.hpp"

#include "Editor/Editor.hpp"
#include "Editor/ImGuiRenderGraphPass.hpp"

#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/OffscreenRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/SceneRenderGraphPass.hpp"
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
        std::cerr << "No arguments provided\n";
        return 1;
    }

    auto start = std::chrono::high_resolution_clock::now();

    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    elix::core::Logger::createDefaultLogger();
    elix::engine::PhysXCore::init();

    elix::engine::AssetsLoader::registerAssetLoader(std::make_shared<elix::engine::OBJAssetLoader>());
    elix::engine::AssetsLoader::registerAssetLoader(std::make_shared<elix::engine::FBXAssetLoader>());
    elix::engine::AssetsLoader::registerAssetLoader(std::make_shared<elix::engine::MaterialAssetLoader>());

    auto window = elix::platform::Window::create(800, 600, "Velix", elix::platform::Window::WindowFlags::EWINDOW_FLAGS_FULLSCREEN_WINDOWED);
    auto vulkanContext = elix::core::VulkanContext::create(window);
    auto props = vulkanContext->getPhysicalDevicePoperties();

    const std::string graphicsPipelineCache = elix::editor::FileHelper::getExecutablePath().string() +
                                              "/pipeline_cache_" + std::string(props.deviceName) + "_" + std::to_string(props.vendorID) + '_' +
                                              std::to_string(props.driverVersion) + ".elixgpbin";

    elix::engine::cache::GraphicsPipelineCache::loadCacheFromFile(vulkanContext->getDevice(), graphicsPipelineCache);

    const std::string projectPath = argv[1];

    auto project = elix::editor::ProjectLoader::loadProject(projectPath);

    if (!project)
    {
        std::cerr << "Failed to load project\n";
        return 1;
    }

    auto scene = std::make_shared<elix::engine::Scene>();

    if (!scene->loadSceneFromFile(project->entryScene))
        throw std::runtime_error("Failed to load scene");

    // TODO: Change order to see if RenderGraph compile works

    auto renderGraph = new elix::engine::renderGraph::RenderGraph(vulkanContext->getDevice(), vulkanContext->getSwapchain());
    renderGraph->createDescriptorSetPool();
    auto editor = std::make_shared<elix::editor::Editor>(renderGraph->getDescriptorPool());

    auto shadowRenderPass = renderGraph->addPass<elix::engine::renderGraph::ShadowRenderGraphPass>();
    auto offscreenRenderGraphPass = renderGraph->addPass<elix::engine::renderGraph::OffscreenRenderGraphPass>(renderGraph->getDescriptorPool(),
                                                                                                              shadowRenderPass->getShadowHandler());

    auto imguiRenderGraphPass = renderGraph->addPass<elix::editor::ImGuiRenderGraphPass>(editor, offscreenRenderGraphPass->getColorTextureHandlers(),
                                                                                         offscreenRenderGraphPass->getObjectTextureHandler());

    VkExtent2D extent{.width = 50, .height = 30};

    auto previewRenderGraphPass = renderGraph->addPass<elix::editor::PreviewAssetsRenderGraphPass>(extent);

    // auto sceneRenderGraphPass = renderGraph->addPass<elix::engine::renderGraph::SceneRenderGraphPass>(shadowRenderPass->getShadowHandler());
    renderGraph->setup();

    renderGraph->createCameraDescriptorSets(shadowRenderPass->getSampler(), shadowRenderPass->getImageView());

    renderGraph->createRenderGraphResources();

    if (auto entityModel = elix::engine::AssetsLoader::loadModel("./resources/models/Shadow.fbx"); entityModel.has_value())
    {
        auto model = entityModel.value();

        auto entity = scene->addEntity("texture test");

        auto skeletonComponent = entity->addComponent<elix::engine::SkeletalMeshComponent>(model.meshes, model.skeleton.value());
        // skeletonComponent->getSkeleton().printBonesHierarchy();
        // std::cout << "Added skeleton mesh component\n";

        entity->getComponent<elix::engine::Transform3DComponent>()->setScale({0.003f, 0.003f, 0.003f});
    }
    else
        std::cerr << "Failed to load model\n";

    elix::engine::Texture::SharedPtr dummyTexture = std::make_shared<elix::engine::Texture>();
    dummyTexture->createFromPixels(0xFFFFFFFF);

    elix::engine::Material::createDefaultMaterial(renderGraph->getDescriptorPool(), dummyTexture);

    // editor->addOnViewportChangedCallback([&](float w, float h)
    //                                      {
    //     VkViewport viewport{0.0f, 0.0f, w, h,  0.0f, 1.0f};

    //     VkRect2D scissor{VkOffset2D{0, 0}, VkExtent2D{w, h}};
    //     offscreenRenderGraphPass->setViewport(viewport);
    //     offscreenRenderGraphPass->setScissor(scissor); });

    float lastFrame = 0.0f;
    float deltaTime = 0.0f;
    const float fixedStep = 1.0f / 60.0f;
    float accumulator = 0.0f;

    editor->setScene(scene);
    editor->setProject(project);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "Engine load took: " << duration.count() << " microseconds\n";
    std::cout << "Engine load took: " << duration.count() / 1000.0 << " milliseconds\n";
    std::cout << "Engine load took: " << duration.count() / 1000000.0 << " seconds\n";

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

        if (shouldUpdate)
            scene->update(deltaTime);

        previewRenderGraphPass->clearJobs();

        for (const auto &m : editor->getRequestedMaterialPreviewJobs())
        {
            elix::editor::PreviewAssetsRenderGraphPass::PreviewJob job;
            job.material = m;
            previewRenderGraphPass->addMaterialPreviewJob(job);
        }

        renderGraph->addAdditionalFrameData(editor->getRenderData());
        renderGraph->prepareFrame(currentRenderCamera, scene.get());
        renderGraph->draw();

        editor->setDoneMaterialJobs(previewRenderGraphPass->getRenderedImages());
        editor->clearMaterialPreviewJobs();
    }

    elix::engine::cache::GraphicsPipelineCache::saveCacheToFile(vulkanContext->getDevice(), graphicsPipelineCache);
    // To clean all needed Vulkan resources before VulkanContex is cleared
    renderGraph->cleanResources();
    delete renderGraph;
    editor.reset();
    scene.reset();
    elix::engine::Material::deleteDefaultMaterial();
    dummyTexture.reset();
    //

    vulkanContext->cleanup();

    elix::engine::PhysXCore::shutdown();

    glfwTerminate();

    return 0;
}