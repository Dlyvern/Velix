#include "Engine/RenderGraph.hpp"
#include "Core/VulkanContext.hpp"

#include <iostream>
#include <array>
#include <cstring>
#include <stdexcept>

#include "Core/RenderPass.hpp"
#include "Core/Shader.hpp"
#include "Core/VulkanHelpers.hpp"

#include "Engine/Render/GraphPasses/OffscreenRenderGraphPass.hpp"

#include "Engine/Render/GraphPasses/BaseRenderGraphPass.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/PushConstant.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"

#include "Engine/ShaderFamily.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

RenderGraph::RenderGraph(VkDevice device, core::SwapChain::SharedPtr swapchain, Scene::SharedPtr scene) : m_device(device), 
m_swapchain(swapchain), m_scene(scene), m_resourceCompiler(device, core::VulkanContext::getContext()->getPhysicalDevice(), swapchain)
{
    m_physicalDevice = core::VulkanContext::getContext()->getPhysicalDevice();

    m_commandPool = core::CommandPool::create(m_device, core::VulkanContext::getContext()->getGraphicsFamily());

    m_commandBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
    m_secondaryCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    int imageCount = swapchain->getImages().size();

    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(imageCount);
    m_imagesInFlight.resize(imageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) 
    {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create synchronization objects for a frame!");
        }
    }

    for (int i = 0; i < imageCount; ++i)
    {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create renderFinished semaphore for image!");
    }

    elix::engine::engineShaderFamilies::initEngineShaderFamilies();

    m_cameraSetLayout = engineShaderFamilies::staticMeshCameraLayout;
    m_directionalLightSetLayout = engineShaderFamilies::staticMeshLightLayout;
    m_materialSetLayout = engineShaderFamilies::staticMeshMaterialLayout;
}

void RenderGraph::createDataFromScene()
{
    VkQueue graphicsQueue = core::VulkanContext::getContext()->getGraphicsQueue();

    for(const auto& entity : m_scene->getEntities())
    {
        if(auto staticMeshComponent = entity->getComponent<StaticMeshComponent>())
        {
            const auto& meshes = staticMeshComponent->getMeshes();
            std::vector<GPUMesh::SharedPtr> gpuMeshes;

            size_t hashData{0};

            for(const auto& mesh : meshes)
            {
                auto gpuMesh = GPUMesh::createFromMesh(m_device, m_physicalDevice, mesh, graphicsQueue, m_commandPool);

                if(mesh.material.albedoTexture.empty())
                    gpuMesh->material = Material::getDefaultMaterial();
                else
                {
                    auto textureImage = std::make_shared<TextureImage>();
                    textureImage->load(mesh.material.albedoTexture, m_commandPool);

                    auto material = Material::create(getDescriptorPool(), textureImage);
                    gpuMesh->material = material;
                }

                gpuMeshes.push_back(gpuMesh);
            }

            GPUEntity gpuEntity
            {
                .meshes = gpuMeshes,
                .transform = entity->hasComponent<Transform3DComponent>() ? entity->getComponent<Transform3DComponent>()->getMatrix() : glm::mat4(1.0f),
            };

            m_perFrameData.meshes[entity] = gpuEntity;
        }
    }
}

void RenderGraph::prepareFrame(Camera::SharedPtr camera)
{
    // static auto startTime = std::chrono::high_resolution_clock::now();
    // auto currentTime = std::chrono::high_resolution_clock::now();
    // float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
    // CameraUBO cameraUBO{};
    // cameraUBO.model = glm::rotate(model, time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f,s 1.0f));

    m_perFrameData.swapChainViewport = m_swapchain->getViewport();
    m_perFrameData.swapChainScissor = m_swapchain->getScissor();
    m_perFrameData.lightDescriptorSet = m_directionalLightDescriptorSets[m_currentFrame];
    m_perFrameData.cameraDescriptorSet = m_cameraDescriptorSets[m_currentFrame];

    for(const auto& entity : m_scene->getEntities())
    {
        auto en = m_perFrameData.meshes.find(entity);

        if(en == m_perFrameData.meshes.end())
        {
            std::cerr << "Failed to find entity" << std::endl;
            continue;
        }

        m_perFrameData.meshes[entity].transform = entity->hasComponent<Transform3DComponent>() ? entity->getComponent<Transform3DComponent>()->getMatrix() 
        : glm::mat4(1.0f);
    }

    CameraUBO cameraUBO{};

    cameraUBO.view = camera->getViewMatrix();
    cameraUBO.projection = camera->getProjectionMatrix();
    cameraUBO.projection[1][1] *= -1;

    m_perFrameData.projection = cameraUBO.projection;
    m_perFrameData.view = camera->getViewMatrix();

    std::memcpy(m_cameraMapped[m_currentFrame], &cameraUBO, sizeof(CameraUBO));

    //TODO NEEDS TO BE REDESIGNED
    // size_t requiredSize = sizeof(LightData) * lights.size();

    // if (requiredSize > m_lightSSBOs[m_currentFrame]->getSize()) {
    //     m_lightSSBOs[m_currentFrame] = core::Buffer::create(
    //         requiredSize * 2, // Overallocate by 2x
    //         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    //         0,
    //         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    //     );
        
    //     // Update descriptor set with new buffer
    //     VkDescriptorBufferInfo bufferInfo{};
    //     bufferInfo.buffer = m_lightSSBOs[frameIndex]->vkBuffer();
    //     bufferInfo.offset = 0;
    //     bufferInfo.range = VK_WHOLE_SIZE;

    //     VkWriteDescriptorSet descriptorWrite{};
    //     descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    //     descriptorWrite.dstSet = m_lightingDescriptorSets[frameIndex];
    //     descriptorWrite.dstBinding = 0;
    //     descriptorWrite.dstArrayElement = 0;
    //     descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    //     descriptorWrite.descriptorCount = 1;
    //     descriptorWrite.pBufferInfo = &bufferInfo;

    //     vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
    // }

    auto lights = m_scene->getLights();

    if(lights.empty())
    {
        std::cerr << "NO LIGHTS" << std::endl;
        return;
    }

    size_t requiredSize = sizeof(LightData) * (lights.size() * sizeof(LightData));

    void* mapped;
    vkMapMemory(m_device, m_lightSSBOs[m_currentFrame]->vkDeviceMemory(), 0, requiredSize, 0, &mapped);

    LightSSBO* ssboData = static_cast<LightSSBO*>(mapped);
    ssboData->lightCount = static_cast<int>(lights.size());

    for(size_t i = 0; i < lights.size(); ++i)
    {
        auto lightComponent = lights[i];

        ssboData->lights[i].position = glm::vec4(lightComponent->position, 0.0f);
        ssboData->lights[i].parameters = glm::vec4(1.0f);
        ssboData->lights[i].colorStrength = glm::vec4(lightComponent->color, lightComponent->strength);

        if(auto directionalLight = dynamic_cast<DirectionalLight*>(lightComponent.get()))
        {
            ssboData->lights[i].direction = glm::vec4{glm::normalize(directionalLight->direction), 0.0f};
            ssboData->lights[i].parameters.w = 0;
        }
        else if(auto pointLight = dynamic_cast<PointLight*>(lightComponent.get()))
        {
            ssboData->lights[i].parameters.z = pointLight->radius;
            ssboData->lights[i].parameters.w = 2;
        }
        else if(auto spotLight = dynamic_cast<SpotLight*>(lightComponent.get()))
        {
            ssboData->lights[i].direction = glm::vec4(glm::normalize(spotLight->direction), 0.0f);
            ssboData->lights[i].parameters.w = 1;
            ssboData->lights[i].parameters.x = glm::cos(glm::radians(spotLight->innerAngle));
            ssboData->lights[i].parameters.y = glm::cos(glm::radians(spotLight->outerAngle));
        }
    }

    vkUnmapMemory(m_device, m_lightSSBOs[m_currentFrame]->vkDeviceMemory());

    for(size_t i = 0; i < lights.size(); ++i)
    {
        auto light = lights[i];

        if(auto directionalLight = dynamic_cast<DirectionalLight*>(light.get()))
        {
            LightSpaceMatrixUBO lightSpaceMatrixUBO{};

            glm::vec3 lightDirection = glm::normalize(directionalLight->direction);

            glm::vec3 lightTarget{0.0f};
            float nearPlane = 50.0f;
            float farPlane = 1000.0f;
            //Position 'light camera' 20 units away from the target 
            glm::vec3 lightPosition = lightTarget - lightDirection * 20.0f;

            glm::mat4 lightView = glm::lookAt(lightPosition, lightTarget, glm::vec3(0.0f, 1.0f, 0.0f));

            glm::mat4 lightProjection = glm::ortho(-30.0f, 30.0f, -30.0f, 30.0f, 1.0f, 40.0f);

            // glm::mat4 lightProjection = glm::perspective(glm::radians(45.0f), 1.0f, nearPlane, farPlane);

            glm::mat4 lightMatrix = lightProjection * lightView;

            lightSpaceMatrixUBO.lightSpaceMatrix = lightMatrix;

            std::memcpy(m_lightMapped[m_currentFrame], &lightSpaceMatrixUBO, sizeof(LightSpaceMatrixUBO));

            // m_lightSpaceMatrixUniformObjects[m_currentFrame]->update(&lightSpaceMatrixUBO);

            m_perFrameData.lightSpaceMatrix = lightMatrix;

            //!Only one directional light. Fix it later
            break;
        }
    }
}

void RenderGraph::createDescriptorSetPool()
{
    const std::vector<VkDescriptorPoolSize> poolSizes
    {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000}
    };

    VkDescriptorPoolCreateInfo descriptorPoolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptorPoolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolCI.pPoolSizes = poolSizes.data();
    descriptorPoolCI.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if(vkCreateDescriptorPool(m_device, &descriptorPoolCI, nullptr, &m_descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor pool");
}

void RenderGraph::begin()
{    
    // auto& lock = m_syncObject->getSync(m_currentFrame);

    // vkWaitForFences(m_device, 1, &lock.inFlightFence, VK_TRUE, UINT64_MAX);

    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    // VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain->vk(), UINT64_MAX, lock.imageAvailableSemaphore, VK_NULL_HANDLE, &m_imageIndex);
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain->vk(), UINT64_MAX, m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &m_imageIndex);

    if(result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        m_swapchain->recreate();
        m_resourceCompiler.onSwapChainResize(m_resourceBuilder, m_resourceStorage);

        for(const auto& renderPass : m_renderGraphPasses)
        {
            if(auto s = dynamic_cast<OffscreenRenderGraphPass*>(renderPass.second.get()))
            {
                m_perFrameData.viewportImageViews = s->getImageViews();
                m_perFrameData.isViewportImageViewsDirty = true;
                break;
            }
        }
    }
    else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Failed to acquire swap chain image");

    //Remove
    if (m_imagesInFlight[m_imageIndex] != VK_NULL_HANDLE)
        vkWaitForFences(m_device, 1, &m_imagesInFlight[m_imageIndex], VK_TRUE, UINT64_MAX);

    m_imagesInFlight[m_imageIndex] = m_inFlightFences[m_currentFrame];
    //

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    // vkResetFences(m_device, 1, &lock.inFlightFence);

    // Create a pool of secondary command buffers per frame: maybe 2–4 per frame (depending on how many threads you want to use).

    const auto& primaryCommandBuffer = m_commandBuffers.at(m_currentFrame);

    primaryCommandBuffer->reset();
    primaryCommandBuffer->begin();


    for (size_t passIdx = 0; passIdx < m_renderGraphPasses.size(); ++passIdx)
        m_secondaryCommandBuffers[m_currentFrame][passIdx]->reset();

    // std::vector<VkCommandBuffer> vkSecondaries; 
    // vkSecondaries.reserve(m_renderGraphPasses.size());

    size_t passIndex = 0;

    std::vector<IRenderGraphPass::SharedPtr> shadowPasses;
    std::vector<IRenderGraphPass::SharedPtr> otherPasses;

    for (const auto& [_, pass] : m_renderGraphPasses) {
        if (dynamic_cast<ShadowRenderGraphPass*>(pass.get()))
            shadowPasses.push_back(pass);
        else
            otherPasses.push_back(pass);
    }

    std::vector<std::shared_ptr<IRenderGraphPass>> orderedPasses;
    orderedPasses.reserve(shadowPasses.size() + otherPasses.size());
    orderedPasses.insert(orderedPasses.end(), shadowPasses.begin(), shadowPasses.end());
    orderedPasses.insert(orderedPasses.end(), otherPasses.begin(), otherPasses.end());

    RenderGraphPassContext frameData
    {
        .currentFrame = m_currentFrame,
        .currentImageIndex = m_imageIndex
    };

    for(const auto& renderGraphPass : orderedPasses)
    {
        renderGraphPass->update(frameData);

        VkRenderPassBeginInfo beginRenderPassInfo;
        renderGraphPass->getRenderPassBeginInfo(beginRenderPassInfo);

        const auto& secCB = m_secondaryCommandBuffers[m_currentFrame][passIndex];

        VkCommandBufferInheritanceInfo inherit{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
        inherit.renderPass = beginRenderPassInfo.renderPass;
        inherit.subpass = 0;
        inherit.framebuffer = beginRenderPassInfo.framebuffer;

        vkCmdBeginRenderPass(primaryCommandBuffer->vk(), &beginRenderPassInfo, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

        secCB->begin(VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, &inherit);

        renderGraphPass->execute(secCB, m_perFrameData);

        secCB->end();

        // vkSecondaries.push_back(secCB->vk());

        VkCommandBuffer vkSec = secCB->vk();
        vkCmdExecuteCommands(primaryCommandBuffer->vk(), 1, &vkSec);

        ++passIndex;

        vkCmdEndRenderPass(primaryCommandBuffer->vk());

        renderGraphPass->endBeginRenderPass(primaryCommandBuffer);
    }

    // if(!vkSecondaries.empty())
    //     vkCmdExecuteCommands(primaryCommandBuffer->vk(), static_cast<uint32_t>(vkSecondaries.size()), vkSecondaries.data());

    primaryCommandBuffer->end();

    m_perFrameData.isViewportImageViewsDirty = false;
}

void RenderGraph::end()
{
    const auto& currentCommandBuffer = m_commandBuffers.at(m_currentFrame);
    const std::vector<VkSemaphore> waitSemaphores = {m_imageAvailableSemaphores[m_currentFrame]};
    const std::vector<VkPipelineStageFlags> waitStages = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    const std::vector<VkSemaphore> signalSemaphores = {m_renderFinishedSemaphores[m_imageIndex]};
    const std::vector<VkSwapchainKHR> swapChains = {m_swapchain->vk()};

    currentCommandBuffer->submit(core::VulkanContext::getContext()->getGraphicsQueue(), waitSemaphores, waitStages, signalSemaphores,
    m_inFlightFences[m_currentFrame]);
    
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
    presentInfo.pWaitSemaphores = signalSemaphores.data(); 
    presentInfo.swapchainCount = static_cast<uint32_t>(swapChains.size());
    presentInfo.pSwapchains = swapChains.data();
    presentInfo.pImageIndices = &m_imageIndex;
    presentInfo.pResults = nullptr;

    VkResult result = vkQueuePresentKHR(core::VulkanContext::getContext()->getPresentQueue(), &presentInfo);

    if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        m_swapchain->recreate();
        m_resourceCompiler.onSwapChainResize(m_resourceBuilder, m_resourceStorage);
        for(const auto& renderPass : m_renderGraphPasses)
        {
            if(auto s = dynamic_cast<OffscreenRenderGraphPass*>(renderPass.second.get()))
            {
                m_perFrameData.viewportImageViews = s->getImageViews();
                m_perFrameData.isViewportImageViewsDirty = true;
                break;
            }
        }
    }
    else if(result != VK_SUCCESS)
        throw std::runtime_error("Failed to present swap chain image");

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void RenderGraph::draw()
{
    begin();
    end();
}

void RenderGraph::setup()
{
    for(const auto& pass : m_renderGraphPasses)
        pass.second->setup(m_resourceBuilder);

    compile();
}

void RenderGraph::createDirectionalLightDescriptorSets()
{
    m_lightSSBOs.reserve(MAX_FRAMES_IN_FLIGHT);
    m_directionalLightDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);

    static constexpr uint8_t INIT_LIGHTS_COUNT = 2;
    static constexpr VkDeviceSize INITIAL_SIZE = sizeof(LightData) * (INIT_LIGHTS_COUNT * sizeof(LightData));

    for(size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto ssboBuffer = m_lightSSBOs.emplace_back(core::Buffer::create(INITIAL_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

        m_directionalLightDescriptorSets[i] = DescriptorSetBuilder::begin()
        .addBuffer(ssboBuffer, VK_WHOLE_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .build(m_device, m_descriptorPool, m_directionalLightSetLayout->vk());
    }
}

void RenderGraph::createCameraDescriptorSets(VkSampler sampler, VkImageView imageView)
{
    m_cameraDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    m_cameraMapped.resize(MAX_FRAMES_IN_FLIGHT);
    m_lightMapped.resize(MAX_FRAMES_IN_FLIGHT);

    m_lightSpaceMatrixUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);
    m_cameraUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);

    for(size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto& cameraBuffer = m_cameraUniformObjects.emplace_back(core::Buffer::create(sizeof(CameraUBO),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
        
        auto& lightBuffer = m_lightSpaceMatrixUniformObjects.emplace_back(core::Buffer::create(sizeof(LightSpaceMatrixUBO),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

        cameraBuffer->map(m_cameraMapped[i]);
        lightBuffer->map(m_lightMapped[i]);

        m_cameraDescriptorSets[i] = DescriptorSetBuilder::begin()
        .addBuffer(cameraBuffer, sizeof(CameraUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .addBuffer(lightBuffer, sizeof(LightSpaceMatrixUBO), 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .addImage(imageView, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
        .build(m_device, m_descriptorPool, m_cameraSetLayout->vk());
    }
}

void RenderGraph::createRenderGraphResources()
{
    for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
    {
        m_commandBuffers.emplace_back(core::CommandBuffer::create(m_device, m_commandPool->vk()));

        m_secondaryCommandBuffers[frame].resize(m_renderGraphPasses.size());
        
        for (size_t pass = 0; pass < m_renderGraphPasses.size(); ++pass)
            m_secondaryCommandBuffers[frame][pass] = core::CommandBuffer::create(m_device, m_commandPool->vk(), VK_COMMAND_BUFFER_LEVEL_SECONDARY);
    }
}

void RenderGraph::compile()
{
    m_resourceCompiler.compile(m_resourceBuilder, m_resourceStorage);
    
    for(const auto& pass : m_renderGraphPasses)
        pass.second->compile(m_resourceStorage);

    for(const auto& renderPass : m_renderGraphPasses)
    {
        if(auto s = dynamic_cast<OffscreenRenderGraphPass*>(renderPass.second.get()))
        {
            m_perFrameData.viewportImageViews = s->getImageViews();
            m_perFrameData.isViewportImageViewsDirty = true;
            break;
        }
    }
}

void RenderGraph::cleanResources()
{
    vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
    vkDeviceWaitIdle(m_device);

    for(const auto& primary : m_commandBuffers)
        primary->destroyVk();
    for(const auto& secondary : m_secondaryCommandBuffers)
        for(const auto& cb : secondary)
            cb->destroyVk();

    for(const auto& renderPass : m_renderGraphPasses)
        renderPass.second->cleanup();

    for(const auto& light : m_lightSpaceMatrixUniformObjects)
        light->destroyVk();
    
    for(const auto& camera : m_cameraUniformObjects)
        camera->destroyVk();

    m_perFrameData.meshes.clear();

    m_resourceStorage.cleanup();
    m_resourceBuilder.cleanup();
}

ELIX_NESTED_NAMESPACE_END