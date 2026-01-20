#include <iostream>

#include "Core/Window.hpp"
#include "Core/VulkanContext.hpp"

#include "Editor/Editor.hpp"
#include "Editor/ImGuiRenderGraphPass.hpp"

#include "Engine/Render/GraphPasses/BaseRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/OffscreenRenderGraphPass.hpp"
#include "Engine/RenderGraph.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Primitives.hpp"
#include "Engine/EngineCamera.hpp"
#include "Engine/Assets/OBJAssetLoader.hpp"
#include "Engine/Assets/FBXAssetLoader.hpp"
#include "Engine/Physics/PhysXCore.hpp"

#include "Engine/ProjectLoader.hpp"

#include "Core/Logger.hpp"

#include <filesystem>

#include <GLFW/glfw3.h>

#include <chrono>

int main(int argc, char **argv)
{
    if (argc < 2)
        throw std::runtime_error("No arguments provided");

    auto start = std::chrono::high_resolution_clock::now();

    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW");

    elix::core::Logger::createDefaultLogger();
    elix::engine::PhysXCore::init();

    elix::engine::AssetsLoader::registerAssetLoader(std::make_shared<elix::engine::OBJAssetLoader>());
    elix::engine::AssetsLoader::registerAssetLoader(std::make_shared<elix::engine::FBXAssetLoader>());

    auto window = elix::platform::Window::create(800, 600, "Velix", elix::platform::Window::WindowFlags::EWINDOW_FLAGS_FULLSCREEN_WINDOWED);
    auto vulkanContext = elix::core::VulkanContext::create(window);

    const std::string projectPath = argv[1];

    auto project = elix::engine::ProjectLoader::loadProject(projectPath);

    if (!project)
        throw std::runtime_error("Failed to load project");

    auto scene = std::make_shared<elix::engine::Scene>();

    if (!scene->loadSceneFromFile(project->entryScene))
        throw std::runtime_error("Failed to load scene");

    auto editor = std::make_shared<elix::editor::Editor>();

    auto renderGraph = new elix::engine::RenderGraph(vulkanContext->getDevice(), vulkanContext->getSwapchain(), scene);
    auto shadowRenderPass = renderGraph->addPass<elix::engine::ShadowRenderGraphPass>(vulkanContext->getDevice());

    renderGraph->createDescriptorSetPool();
    renderGraph->createCameraDescriptorSets(shadowRenderPass->getSampler(), shadowRenderPass->getImageView());
    renderGraph->createDirectionalLightDescriptorSets();

    // TODO: Change order to see if RenderGraph compile works
    renderGraph->addPass<elix::editor::ImGuiRenderGraphPass>(editor);
    // renderGraph->addPass<elix::engine::BaseRenderGraphPass>(vulkanContext->getDevice(), vulkanContext->getSwapchain(), renderGraph->getDescriptorPool());
    auto offscreenRenderGraphPass = renderGraph->addPass<elix::engine::OffscreenRenderGraphPass>(renderGraph->getDescriptorPool());

    renderGraph->createRenderGraphResources();

    // testEntity->getComponent<elix::engine::StaticMeshComponent>()->getMesh(0).material = elix::engine::CPUMaterial{.albedoTexture = "./resources/textures/ConcreteWall.png"};

    elix::engine::Texture::SharedPtr dummyTexture = std::make_shared<elix::engine::Texture>();
    dummyTexture->create(vulkanContext->getDevice(), vulkanContext->getPhysicalDevice(), 0xFFFFFFFF);

    elix::engine::Material::createDefaultMaterial(renderGraph->getDescriptorPool(), dummyTexture);

    renderGraph->createDataFromScene();

    renderGraph->setup();

    editor->addOnViewportChangedCallback([&](float w, float h)
                                         {
        VkViewport viewport{0.0f, 0.0f, w, h,  0.0f, 1.0f};

        VkRect2D scissor{VkOffset2D{0, 0}, VkExtent2D{w, h}}; 
        offscreenRenderGraphPass->setViewport(viewport);
        offscreenRenderGraphPass->setScissor(scissor); });

    float lastFrame = 0.0f;
    float deltaTime = 0.0f;
    const float fixedStep = 1.0f / 60.0f;
    float accumulator = 0.0f;

    auto camera = std::make_shared<elix::engine::Camera>();
    auto engineCamera = std::make_shared<elix::engine::EngineCamera>(camera);

    editor->setScene(scene);
    editor->setProject(project);
    editor->setCamera(engineCamera);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "Engine load took: " << duration.count() << " microseconds\n";
    std::cout << "Engine load took: " << duration.count() / 1000.0 << " milliseconds\n";
    std::cout << "Engine load took: " << duration.count() / 1000000.0 << " seconds\n";

    while (window->isOpen())
    {
        const float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // accumulator += deltaTime;

        window->pollEvents();

        engineCamera->update(deltaTime);
        scene->update(deltaTime);

        // while(accumulator >= fixedStep)
        // {
        //     scene->fixedUpdate(fixedStep);
        //     accumulator -= fixedStep;
        // }

        renderGraph->prepareFrame(camera);
        renderGraph->draw();
    }

    // To clean all needed Vulkan resources before VulkanContex is cleared
    renderGraph->cleanResources();
    delete renderGraph;
    editor.reset();
    scene.reset();
    elix::engine::Material::deleteDefaultMaterial();
    dummyTexture.reset();
    //

    vulkanContext->cleanup();

    glfwTerminate();

    return 0;
}