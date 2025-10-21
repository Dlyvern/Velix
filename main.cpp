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

#include <GLFW/glfw3.h>

#include <chrono>

int main(int argc, char** argv)
{
    elix::engine::AssetsLoader::registerAssetLoader(std::make_shared<elix::engine::OBJAssetLoader>());
    elix::engine::AssetsLoader::registerAssetLoader(std::make_shared<elix::engine::FBXAssetLoader>());

    auto start = std::chrono::high_resolution_clock::now();

    auto window = elix::platform::Window::create(1280, 720, "Window");
    auto vulkanContext = elix::core::VulkanContext::create(window);

    auto scene = std::make_shared<elix::engine::Scene>();
    auto testEntity = scene->addEntity("test");
    auto secondTestEntity = scene->addEntity("test2");
    auto testPlane = scene->addEntity("plane");

    elix::engine::Mesh3D meshModel = elix::engine::AssetsLoader::loadModel("./resources/models/concrete_wall.obj");
    elix::engine::Mesh3D mesh{elix::engine::cube::vertices, elix::engine::cube::indices};

    testEntity->addComponent<elix::engine::StaticMeshComponent>(meshModel);
    secondTestEntity->addComponent<elix::engine::StaticMeshComponent>(mesh);
    testPlane->addComponent<elix::engine::StaticMeshComponent>(mesh);

    secondTestEntity->getComponent<elix::engine::Transform3DComponent>()->setPosition({0.8f, 1.8f, -2.0f});

    testPlane->getComponent<elix::engine::Transform3DComponent>()->setPosition({0.0f, 0.0f, 0.0f});
    testPlane->getComponent<elix::engine::Transform3DComponent>()->setScale({10.0f, 0.2f, 10.0f});

    testEntity->getComponent<elix::engine::Transform3DComponent>()->setPosition({1.0f, 2.0f, 1.0f});

    //TEST MORE THAN 2 Lights
    testEntity->addComponent<elix::engine::LightComponent>(elix::engine::LightComponent::LightType::DIRECTIONAL);
    // secondTestEntity->addComponent<elix::engine::LightComponent>(elix::engine::LightComponent::LightType::POINT);

    auto editor = std::make_shared<elix::editor::Editor>();
    editor->setScene(scene);

    elix::engine::RenderGraph renderGraph(vulkanContext->getDevice(), vulkanContext->getSwapchain(), scene);
    auto shadowRenderPass = renderGraph.addPass<elix::engine::ShadowRenderGraphPass>(vulkanContext->getDevice());
    
    renderGraph.createDescriptorSetPool();
    renderGraph.createDescriptorSetLayouts();
    renderGraph.createCameraDescriptorSets(shadowRenderPass->getSampler(), shadowRenderPass->getImageView());
    renderGraph.createDirectionalLightDescriptorSets();
    renderGraph.createGraphicsPipeline();

    //TODO: Change order to see if RenderGraph compile works
    renderGraph.addPass<elix::editor::ImGuiRenderGraphPass>(editor);
    renderGraph.addPass<elix::engine::BaseRenderGraphPass>(vulkanContext->getDevice(), vulkanContext->getSwapchain(), renderGraph.getPipelineLayout());
    renderGraph.addPass<elix::engine::OffscreenRenderGraphPass>(vulkanContext->getDevice(), renderGraph.getPipelineLayout());

    renderGraph.createRenderGraphResources();

    auto textureImage = std::make_shared<elix::engine::TextureImage>();
    textureImage->load(vulkanContext->getDevice(), vulkanContext->getPhysicalDevice(), "./resources/textures/ConcreteWall.png", renderGraph.getCommandPool(), vulkanContext->getGraphicsQueue());

    auto material = elix::engine::Material::create(vulkanContext->getDevice(), vulkanContext->getPhysicalDevice(), renderGraph.getDescriptorPool(),
    renderGraph.MAX_FRAMES_IN_FLIGHT, textureImage, renderGraph.getMaterialDescriptorSetLayout());

    testEntity->getComponent<elix::engine::StaticMeshComponent>()->setMaterial(material);

    elix::engine::TextureImage::SharedPtr dummyTexture = std::make_shared<elix::engine::TextureImage>();
    dummyTexture->create(vulkanContext->getDevice(), vulkanContext->getPhysicalDevice(), renderGraph.getCommandPool(), vulkanContext->getGraphicsQueue());

    elix::engine::Material::createDefaultMaterial(vulkanContext->getDevice(), vulkanContext->getPhysicalDevice(), renderGraph.getDescriptorPool(),
    renderGraph.MAX_FRAMES_IN_FLIGHT, dummyTexture, renderGraph.getMaterialDescriptorSetLayout());

    renderGraph.setup();
    float lastFrame = 0.0f;
    float deltaTime = 0.0f;
    const float fixedStep = 1.0f / 60.0f;
    float accumulator = 0.0f;

    auto camera = std::make_shared<elix::engine::Camera>();
    auto engineCamera = std::make_shared<elix::engine::EngineCamera>(camera);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Engine load took: " << duration.count() << " microseconds\n";
    std::cout << "Engine load took: " << duration.count() / 1000.0 << " milliseconds\n";
    std::cout << "Engine load took: " << duration.count() / 1000000.0 << " seconds\n";

    while(window->isOpen())
    {
        const float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        accumulator += deltaTime;

        window->pollEvents();

        engineCamera->update(deltaTime);
        scene->update(deltaTime);

        while(accumulator >= fixedStep)
        {
            scene->fixedUpdate(fixedStep);
            accumulator -= fixedStep;
        }

        renderGraph.prepareFrame(camera);
        renderGraph.draw();
    }

    renderGraph.cleanResources();
    vulkanContext->cleanup();
}