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

#include "Engine/GraphicsPipelineBuilder.hpp"
#include "Engine/Render/GraphPasses/BaseRenderGraphPass.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/PushConstant.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct ModelPushConstant
{
    glm::mat4 model;
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)

RenderGraph::RenderGraph(VkDevice device, core::SwapChain::SharedPtr swapchain, Scene::SharedPtr scene) : m_device(device), 
m_swapchain(swapchain), m_scene(scene), m_resourceCompiler(device, core::VulkanContext::getContext()->getPhysicalDevice(), swapchain)
{
    m_syncObject = std::make_unique<core::SyncObject>(m_device, MAX_FRAMES_IN_FLIGHT);

    auto queueFamilyIndices = core::VulkanContext::findQueueFamilies(core::VulkanContext::getContext()->getPhysicalDevice(), core::VulkanContext::getContext()->getSurface());
    m_commandPool = core::CommandPool::create(m_device, queueFamilyIndices.graphicsFamily.value());

    VkQueue graphicsQueue = core::VulkanContext::getContext()->getGraphicsQueue();

    for(const auto& entity : m_scene->getEntities())
    {
        if(auto staticMeshComponent = entity->getComponent<StaticMeshComponent>())
        {
            const auto& mesh = staticMeshComponent->getMesh();
            size_t hashData{0};

            hashing::hash(hashData, (mesh.indices.size()));

            for(const auto& indices : mesh.indices)
                hashing::hash(hashData, (indices));

            hashing::hash(hashData, (mesh.vertices.size()));

            auto gpuMesh = GPUMesh::createFromMesh(m_device, core::VulkanContext::getContext()->getPhysicalDevice(), mesh, graphicsQueue, m_commandPool);

            GPUEntity gpuEntity
            {
                .mesh = gpuMesh,
                .transform = entity->hasComponent<Transform3DComponent>() ? entity->getComponent<Transform3DComponent>()->getMatrix() : glm::mat4(1.0f),
                .materialDescriptorSet = VK_NULL_HANDLE
            };

            m_perFrameData.transformationBasedOnMesh[entity] = gpuMesh;
            m_perFrameData.meshes[entity] = gpuEntity;
        }
    }

    m_commandBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
    m_secondaryCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
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

        VkDescriptorSet materialDst{VK_NULL_HANDLE};

        if(auto staticMesh = entity->getComponent<StaticMeshComponent>())
            if(auto material = staticMesh->getMaterial())
                materialDst = material->getDescriptorSet(m_currentFrame);
            else
                materialDst = Material::getDefaultMaterial()->getDescriptorSet(m_currentFrame);

        m_perFrameData.meshes[entity].materialDescriptorSet = materialDst;
    }

    CameraUBO cameraUBO{};

    cameraUBO.view = camera->getViewMatrix();
    cameraUBO.projection = camera->getProjectionMatrix();
    cameraUBO.projection[1][1] *= -1;

    m_cameraUniformObjects[m_currentFrame]->update(&cameraUBO);
    
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

            //Position 'light camera' 20 units away from the target 
            glm::vec3 lightPosition = lightTarget - lightDirection * 20.0f;

            glm::mat4 lightView = glm::lookAt(lightPosition, lightTarget, glm::vec3(0.0f, 1.0f, 0.0f));

            glm::mat4 lightProjection = glm::ortho(-30.0f, 30.0f, -30.0f, 30.0f, 1.0f, 40.0f);


            // glm::mat4 clipCorrection = glm::mat4(
            //     1,  0,   0, 0,
            //     0, -1,   0, 0,
            //     0,  0, 0.5, 0,
            //     0,  0, 0.5, 1
            // );

            // glm::mat4 lightMatrix = clipCorrection * lightProjection * lightView;
            glm::mat4 lightMatrix = lightProjection * lightView;

            lightSpaceMatrixUBO.lightSpaceMatrix = lightMatrix;

            m_lightSpaceMatrixUniformObjects[m_currentFrame]->update(&lightSpaceMatrixUBO);

            m_perFrameData.lightSpaceMatrix = lightMatrix;

            //!Only one directional light. Fix it later
            break;
        }
    }
}

void RenderGraph::createDescriptorSetLayouts()
{
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding lightSpaceBinding{};
        lightSpaceBinding.binding = 1;
        lightSpaceBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        lightSpaceBinding.descriptorCount = 1;
        lightSpaceBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        lightSpaceBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding lightMapBinding{};
        lightMapBinding.binding = 2;
        lightMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        lightMapBinding.descriptorCount = 1;
        lightMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        lightMapBinding.pImmutableSamplers = nullptr;

        m_cameraSetLayout = core::DescriptorSetLayout::create(m_device, {uboLayoutBinding, lightSpaceBinding, lightMapBinding});
    }

    {
        VkDescriptorSetLayoutBinding lightSSBOLayoutBinding{};
        lightSSBOLayoutBinding.binding = 0;
        lightSSBOLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lightSSBOLayoutBinding.descriptorCount = 1;
        lightSSBOLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        lightSSBOLayoutBinding.pImmutableSamplers = nullptr;

        m_directionalLightSetLayout = core::DescriptorSetLayout::create(m_device, {lightSSBOLayoutBinding});
    }

    {
        VkDescriptorSetLayoutBinding textureLayoutBinding{};
        textureLayoutBinding.binding = 0;
        textureLayoutBinding.descriptorCount = 1;
        textureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureLayoutBinding.pImmutableSamplers = nullptr;
        textureLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding colorLayoutBinding{};
        colorLayoutBinding.binding = 1;
        colorLayoutBinding.descriptorCount = 1;
        colorLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        colorLayoutBinding.pImmutableSamplers = nullptr;
        colorLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        m_materialSetLayout = core::DescriptorSetLayout::create(m_device, {textureLayoutBinding, colorLayoutBinding});
    }
}

void RenderGraph::createDescriptorSetPool()
{
    const std::vector<VkDescriptorPoolSize> poolSizes{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(1000)},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(1000)},
        { VK_DESCRIPTOR_TYPE_SAMPLER,                  16 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,            16 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,           16 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,     16 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,     16 }
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
    auto& lock = m_syncObject->getSync(m_currentFrame);

    vkWaitForFences(m_device, 1, &lock.inFlightFence, VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain->vk(), UINT64_MAX, lock.imageAvailableSemaphore, VK_NULL_HANDLE, &m_imageIndex);

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

    vkResetFences(m_device, 1, &lock.inFlightFence);

    // Create a pool of secondary command buffers per frame: maybe 2–4 per frame (depending on how many threads you want to use).

    const auto& primaryCommandBuffer = m_commandBuffers.at(m_currentFrame);

    primaryCommandBuffer->reset();
    primaryCommandBuffer->begin();


    for (size_t passIdx = 0; passIdx < m_renderGraphPasses.size(); ++passIdx)
        m_secondaryCommandBuffers[m_currentFrame][passIdx]->reset();

    // std::vector<VkCommandBuffer> vkSecondaries; 
    // vkSecondaries.reserve(m_renderGraphPasses.size());

    size_t passIndex = 0;

    std::vector<std::shared_ptr<IRenderGraphPass>> shadowPasses;
    std::vector<std::shared_ptr<IRenderGraphPass>> otherPasses;

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

    RenderGraphPassContext frameData{
        .currentFrame = m_currentFrame,
        .currentImageIndex = m_imageIndex
    };

    for(const auto& renderGraphPass : orderedPasses)
    {
        renderGraphPass->update(frameData);

        VkRenderPassBeginInfo beginRenderPassInfo;
        renderGraphPass->getRenderPassBeginInfo(beginRenderPassInfo);

        auto& secCB = m_secondaryCommandBuffers[m_currentFrame][passIndex];

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
    auto& lock = m_syncObject->getSync(m_currentFrame);

    const auto& currentCommandBuffer = m_commandBuffers.at(m_currentFrame);
    const std::vector<VkSemaphore> waitSemaphores = {lock.imageAvailableSemaphore};
    const std::vector<VkPipelineStageFlags> waitStages = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    const std::vector<VkSemaphore> signalSemaphores = {lock.renderFinishedSemaphore};
    const std::vector<VkSwapchainKHR> swapChains = {m_swapchain->vk()};

    currentCommandBuffer->submit(core::VulkanContext::getContext()->getGraphicsQueue(), waitSemaphores, waitStages, signalSemaphores, lock.inFlightFence);
    
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

void RenderGraph::createGraphicsPipeline()
{
    const std::vector<VkPushConstantRange> pushConstants
    {
        PushConstant<ModelPushConstant>::getRange(VK_SHADER_STAGE_VERTEX_BIT)
    };
    
    const std::vector<core::DescriptorSetLayout::SharedPtr> setLayouts
    {
        m_cameraSetLayout,
        m_materialSetLayout,
        m_directionalLightSetLayout
    };

    m_pipelineLayout = core::PipelineLayout::create(m_device, setLayouts, pushConstants);
}

void RenderGraph::createDirectionalLightDescriptorSets()
{
    m_lightSSBOs.reserve(MAX_FRAMES_IN_FLIGHT);
    m_directionalLightDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);

    static constexpr uint8_t INIT_LIGHTS_COUNT = 2;
    static constexpr VkDeviceSize INITIAL_SIZE = sizeof(LightData) * (INIT_LIGHTS_COUNT * sizeof(LightData));

    for(size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto ssboBuffer = m_lightSSBOs.emplace_back(core::Buffer::create(m_device, core::VulkanContext::getContext()->getPhysicalDevice(), INITIAL_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

        m_directionalLightDescriptorSets[i] = DescriptorSetBuilder::begin()
        .addBuffer(ssboBuffer, VK_WHOLE_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .build(m_device, m_descriptorPool, m_directionalLightSetLayout->vk());
    }
}

void RenderGraph::createCameraDescriptorSets(VkSampler sampler, VkImageView imageView)
{
    m_cameraDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    m_cameraUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);
    m_lightSpaceMatrixUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_cameraSetLayout->vk());

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();

    if(vkAllocateDescriptorSets(m_device, &allocInfo, m_cameraDescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate decsriptor sets");

    for(size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto uniformObject = m_cameraUniformObjects.emplace_back(UniformBufferObject<CameraUBO>::create(m_device, core::VulkanContext::getContext()->getPhysicalDevice()));
        auto lightSpaceUniformObject = m_lightSpaceMatrixUniformObjects.emplace_back(UniformBufferObject<LightSpaceMatrixUBO>::create(m_device, core::VulkanContext::getContext()->getPhysicalDevice()));

        // auto s = uniformObject->getBuffer();
        // auto v = lightSpaceUniformObject->getBuffer();

        // m_cameraDescriptorSets[i] = DescriptorSetBuilder::begin()
        // .addBuffer(s, uniformObject->getDeviceSize(), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        // .addBuffer(v, lightSpaceUniformObject->getDeviceSize(), 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        // .addImage(imageView, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
        // .build(m_device, m_descriptorPool, m_cameraSetLayout->vk());

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformObject->getBuffer()->vkBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = uniformObject->getDeviceSize();

        VkDescriptorBufferInfo lightSpaceBufferInfo{};
        lightSpaceBufferInfo.buffer = lightSpaceUniformObject->getBuffer()->vkBuffer();
        lightSpaceBufferInfo.offset = 0;
        lightSpaceBufferInfo.range = lightSpaceUniformObject->getDeviceSize();

        VkDescriptorImageInfo shadowMapImageInfo{};
        shadowMapImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shadowMapImageInfo.imageView = imageView;
        shadowMapImageInfo.sampler = sampler;

        std::array<VkWriteDescriptorSet, 3> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_cameraDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = m_cameraDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &lightSpaceBufferInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = m_cameraDescriptorSets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &shadowMapImageInfo;
        
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
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
    // vkDeviceWaitIdle(m_device);

    for(const auto& primary : m_commandBuffers)
        primary->destroyVk();
    for(const auto& secondary : m_secondaryCommandBuffers)
        for(const auto& cb : secondary)
            cb->destroyVk();

    for(const auto& renderPass : m_renderGraphPasses)
        renderPass.second->cleanup();
}

ELIX_NESTED_NAMESPACE_END