#include "Engine/Render/RenderGraph/RenderGraph.hpp"
#include "Core/VulkanContext.hpp"

#include <iostream>
#include <array>
#include <cstring>
#include <stdexcept>
#include <algorithm>

#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"

#include "Engine/Shaders/ShaderFamily.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct CameraUBO
{
    glm::mat4 view;
    glm::mat4 projection;
};

struct LightData
{
    glm::vec4 position;
    glm::vec4 direction;
    glm::vec4 colorStrength;
    glm::vec4 parameters;
};

struct LightSSBO
{
    int lightCount;
    glm::vec3 padding{0.0f};
    LightData lights[];
};

struct LightSpaceMatrixUBO
{
    glm::mat4 lightSpaceMatrix;
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

RenderGraph::RenderGraph(VkDevice device, core::SwapChain::SharedPtr swapchain) : m_device(device),
                                                                                  m_swapchain(swapchain)
{
    m_commandBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
    m_secondaryCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_commandPools.reserve(MAX_FRAMES_IN_FLIGHT);

    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled
    fenceInfo.pNext = nullptr;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create synchronization objects for a frame!");
        }
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create renderFinished semaphore for image!");
    }

    EngineShaderFamilies::initEngineShaderFamilies();
}

void RenderGraph::prepareFrameDataFromScene(Scene *scene)
{
    //! Store last scene
    static std::size_t lastEntitiesSize = 0;
    const std::size_t entitiesSize = scene->getEntities().size();

    // Something has changed in the scene
    if (entitiesSize != lastEntitiesSize)
    {
        // Find and delete 'deleted' entity
        if (entitiesSize < lastEntitiesSize)
        {
            std::cout << "Entity was deleted\n";

            const auto &en = scene->getEntities();

            auto it = m_perFrameData.drawItems.begin();

            while (it != m_perFrameData.drawItems.end())
            {
                if (std::find(en.begin(), en.end(), it->first) == en.end())
                {
                    m_perFrameData.drawItems.erase(it);
                    break;
                }
                else
                    ++it;
            }
        }
        else if (entitiesSize > lastEntitiesSize)
        {
            std::cout << "Entity was addded\n";
        }

        lastEntitiesSize = entitiesSize;
    }

    for (const auto &entity : scene->getEntities())
    {
        auto entityIt = m_perFrameData.drawItems.find(entity);

        // Just update transformation
        if (entityIt != m_perFrameData.drawItems.end())
        {
            entityIt->second.transform = entity->hasComponent<Transform3DComponent>() ? entity->getComponent<Transform3DComponent>()->getMatrix() : glm::mat4(1.0f);

            //! if animatios is playing get finalMatrices, bind poses otherwise
            if (auto skeletalComponent = entity->getComponent<SkeletalMeshComponent>())
                entityIt->second.finalBones = skeletalComponent->getSkeleton().getBindPoses();

            glm::mat4 *mapped;
            m_bonesSSBOs[m_currentFrame]->map(reinterpret_cast<void *&>(mapped));
            for (int i = 0; i < entityIt->second.finalBones.size(); ++i)
                mapped[i] = entityIt->second.finalBones.at(i);

            m_bonesSSBOs[m_currentFrame]->unmap();

            m_perFrameData.perObjectDescriptorSet = m_perObjectDescriptorSets[m_currentFrame];

            continue;
        }

        // Create a new GPU mesh
        std::vector<CPUMesh> meshes;

        if (auto component = entity->getComponent<StaticMeshComponent>())
            meshes = component->getMeshes();
        else if (auto component = entity->getComponent<SkeletalMeshComponent>())
            meshes = component->getMeshes();

        if (meshes.empty())
            continue;

        std::vector<GPUMesh::SharedPtr> gpuMeshes;

        for (const auto &mesh : meshes)
        {
            std::size_t hashData{0};

            hashing::hash(hashData, mesh.vertexStride);
            hashing::hash(hashData, mesh.vertexLayoutHash);

            for (const auto &vertex : mesh.vertexData)
                hashing::hash(hashData, vertex);

            for (const auto &index : mesh.indices)
                hashing::hash(hashData, index);

            if (m_meshes.find(hashData) == m_meshes.end())
            {
                m_meshes[hashData] = GPUMesh::createFromMesh(mesh);
                std::cout << "New mesh\n";
            }

            auto gpuMesh = m_meshes[hashData];

            if (mesh.material.albedoTexture.empty())
                gpuMesh->material = Material::getDefaultMaterial();
            else
            {
                auto textureImage = std::make_shared<Texture>();
                textureImage->load(mesh.material.albedoTexture);

                auto material = Material::create(getDescriptorPool(), textureImage);
                gpuMesh->material = material;
            }

            gpuMeshes.push_back(gpuMesh);
        }

        DrawItem drawItem{
            .meshes = gpuMeshes,
            .transform = entity->hasComponent<Transform3DComponent>() ? entity->getComponent<Transform3DComponent>()->getMatrix() : glm::mat4(1.0f),
        };

        //! if animatios is playing get finalMatrices, bind poses otherwise
        if (auto skeletalComponent = entity->getComponent<SkeletalMeshComponent>())
            drawItem.finalBones = skeletalComponent->getSkeleton().getBindPoses();

        glm::mat4 *mapped;
        m_bonesSSBOs[m_currentFrame]->map(reinterpret_cast<void *&>(mapped));
        for (int i = 0; i < drawItem.finalBones.size(); ++i)
            mapped[i] = drawItem.finalBones.at(i);

        m_bonesSSBOs[m_currentFrame]->unmap();

        m_perFrameData.drawItems[entity] = drawItem;

        m_perFrameData.perObjectDescriptorSet = m_perObjectDescriptorSets[m_currentFrame];
    }
}

void RenderGraph::prepareFrame(Camera::SharedPtr camera, Scene *scene)
{
    prepareFrameDataFromScene(scene);

    m_perFrameData.swapChainViewport = m_swapchain->getViewport();
    m_perFrameData.swapChainScissor = m_swapchain->getScissor();
    m_perFrameData.cameraDescriptorSet = m_cameraDescriptorSets[m_currentFrame];
    m_perFrameData.previewCameraDescriptorSet = m_previewCameraDescriptorSets[m_currentFrame];

    CameraUBO cameraUBO{};

    cameraUBO.view = camera ? camera->getViewMatrix() : glm::mat4(1.0f);
    cameraUBO.projection = camera ? camera->getProjectionMatrix() : glm::mat4(1.0f);
    cameraUBO.projection[1][1] *= -1;

    m_perFrameData.projection = cameraUBO.projection;
    m_perFrameData.view = cameraUBO.view;

    std::memcpy(m_cameraMapped[m_currentFrame], &cameraUBO, sizeof(CameraUBO));

    CameraUBO previewCameraUBO{};
    previewCameraUBO.view = glm::lookAt(
        glm::vec3(0, 0, 3),
        glm::vec3(0, 0, 0),
        glm::vec3(0, 1, 0));

    previewCameraUBO.projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 10.0f);
    previewCameraUBO.projection[1][1] *= -1;

    m_perFrameData.previewProjection = previewCameraUBO.projection;
    m_perFrameData.previewView = previewCameraUBO.view;

    std::memcpy(m_previewCameraMapped[m_currentFrame], &previewCameraUBO, sizeof(CameraUBO));

    // TODO NEEDS TO BE REDESIGNED
    //  size_t requiredSize = sizeof(LightData) * lights.size();

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

    auto lights = scene->getLights();

    // size_t requiredSize = sizeof(LightData) * (lights.size() * sizeof(LightData));

    void *mapped;
    m_lightSSBOs[m_currentFrame]->map(mapped);

    LightSSBO *ssboData = static_cast<LightSSBO *>(mapped);
    ssboData->lightCount = static_cast<int>(lights.size());

    for (size_t i = 0; i < lights.size(); ++i)
    {
        auto lightComponent = lights[i];

        ssboData->lights[i].position = glm::vec4(lightComponent->position, 0.0f);
        ssboData->lights[i].parameters = glm::vec4(1.0f);
        ssboData->lights[i].colorStrength = glm::vec4(lightComponent->color, lightComponent->strength);

        if (auto directionalLight = dynamic_cast<DirectionalLight *>(lightComponent.get()))
        {
            ssboData->lights[i].direction = glm::vec4{glm::normalize(directionalLight->direction), 0.0f};
            ssboData->lights[i].parameters.w = 0;
        }
        else if (auto pointLight = dynamic_cast<PointLight *>(lightComponent.get()))
        {
            ssboData->lights[i].parameters.z = pointLight->radius;
            ssboData->lights[i].parameters.w = 2;
        }
        else if (auto spotLight = dynamic_cast<SpotLight *>(lightComponent.get()))
        {
            ssboData->lights[i].direction = glm::vec4(glm::normalize(spotLight->direction), 0.0f);
            ssboData->lights[i].parameters.w = 1;
            ssboData->lights[i].parameters.x = glm::cos(glm::radians(spotLight->innerAngle));
            ssboData->lights[i].parameters.y = glm::cos(glm::radians(spotLight->outerAngle));
        }
    }

    m_lightSSBOs[m_currentFrame]->unmap();

    for (size_t i = 0; i < lights.size(); ++i)
    {
        auto light = lights[i];

        auto directionalLight = dynamic_cast<DirectionalLight *>(light.get());

        if (!directionalLight)
            continue;

        LightSpaceMatrixUBO lightSpaceMatrixUBO{};

        glm::vec3 lightDirection = glm::normalize(directionalLight->direction);

        glm::vec3 lightTarget{0.0f};
        float nearPlane = 50.0f;
        float farPlane = 1000.0f;
        // Position 'light camera' 20 units away from the target
        glm::vec3 lightPosition = lightTarget - lightDirection * 20.0f;

        glm::mat4 lightView = glm::lookAt(lightPosition, lightTarget, glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 lightProjection = glm::ortho(-30.0f, 30.0f, -30.0f, 30.0f, 1.0f, 40.0f);

        // glm::mat4 lightProjection = glm::perspective(glm::radians(45.0f), 1.0f, nearPlane, farPlane);

        glm::mat4 lightMatrix = lightProjection * lightView;

        lightSpaceMatrixUBO.lightSpaceMatrix = lightMatrix;

        std::memcpy(m_lightMapped[m_currentFrame], &lightSpaceMatrixUBO, sizeof(LightSpaceMatrixUBO));

        // m_lightSpaceMatrixUniformObjects[m_currentFrame]->update(&lightSpaceMatrixUBO);

        m_perFrameData.lightSpaceMatrix = lightMatrix;

        //! Only one directional light. Fix it later
        break;
    }
}

void RenderGraph::createDescriptorSetPool()
{
    const std::vector<VkDescriptorPoolSize> poolSizes{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 100},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100}};

    VkDescriptorPoolCreateInfo descriptorPoolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptorPoolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolCI.pPoolSizes = poolSizes.data();
    descriptorPoolCI.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if (vkCreateDescriptorPool(m_device, &descriptorPoolCI, nullptr, &m_descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor pool");
}

bool RenderGraph::begin()
{
    if (VkResult result = vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX); result != VK_SUCCESS)
    {
        std::cerr << "Failed to wait for fences: " << core::helpers::vulkanResultToString(result) << '\n';
        return false;
    }

    if (VkResult result = vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]); result != VK_SUCCESS)
    {
        std::cerr << "Failed to reset fences: " << core::helpers::vulkanResultToString(result) << '\n';
        return false;
    }

    m_commandPools[m_currentFrame]->reset(VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain->vk(), UINT64_MAX, m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &m_imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        m_swapchain->recreate();

        m_renderGraphPassesCompiler.onSwapChainResized(m_renderGraphPassesBuilder, m_renderGraphPassesStorage);

        for (const auto &[id, renderPass] : m_renderGraphPasses)
            renderPass.renderGraphPass->onSwapChainResized(m_renderGraphPassesStorage);

        return false;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        std::cerr << "Failed to acquire swap chain image: " << core::helpers::vulkanResultToString(result) << '\n';
        return false;
    }

    const auto &primaryCommandBuffer = m_commandBuffers.at(m_currentFrame);

    primaryCommandBuffer->begin();

    size_t passIndex = 0;

    size_t secIndex = 0;

    m_passContextData.currentFrame = m_currentFrame;
    m_passContextData.currentImageIndex = m_imageIndex;

    for (const auto &renderGraphPass : m_sortedRenderGraphPasses)
    {
        const auto executions = renderGraphPass->getRenderPassExecutions(m_passContextData);

        for (int recordingIndex = 0; recordingIndex < executions.size(); ++recordingIndex)
        {
            if (secIndex >= m_secondaryCommandBuffers[m_currentFrame].size())
            {
                // Out of preallocated SCBs — allocate more (better to preallocate enough).
                throw std::runtime_error("Not enough secondary command buffers preallocated for frame");
            }

            const auto &execution = executions[recordingIndex];

            VkRenderPassBeginInfo beginRenderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            beginRenderPassInfo.renderPass = execution.renderPass;
            beginRenderPassInfo.framebuffer = execution.framebuffer;
            beginRenderPassInfo.renderArea = execution.renderArea;
            beginRenderPassInfo.clearValueCount = static_cast<uint32_t>(execution.clearValues.size());
            beginRenderPassInfo.pClearValues = execution.clearValues.data();

            const auto &secCB = m_secondaryCommandBuffers[m_currentFrame][secIndex];

            VkCommandBufferInheritanceInfo inherit{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
            inherit.renderPass = beginRenderPassInfo.renderPass;
            inherit.subpass = 0;
            inherit.framebuffer = beginRenderPassInfo.framebuffer;

            vkCmdBeginRenderPass(primaryCommandBuffer->vk(), &beginRenderPassInfo, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

            secCB->begin(VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT, &inherit);

            renderGraphPass->record(secCB, m_perFrameData, m_passContextData);

            secCB->end();

            VkCommandBuffer vkSec = secCB->vk();
            vkCmdExecuteCommands(primaryCommandBuffer->vk(), 1, &vkSec);

            vkCmdEndRenderPass(primaryCommandBuffer->vk());

            renderGraphPass->endBeginRenderPass(primaryCommandBuffer);

            secIndex++;
        }

        ++passIndex;
    }

    primaryCommandBuffer->end();

    return true;
}

void RenderGraph::end()
{
    const auto &currentCommandBuffer = m_commandBuffers.at(m_currentFrame);
    const std::vector<VkSemaphore> waitSemaphores = {m_imageAvailableSemaphores[m_currentFrame]};
    const std::vector<VkPipelineStageFlags> waitStages = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    const std::vector<VkSemaphore> signalSemaphores = {m_renderFinishedSemaphores[m_currentFrame]};
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

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        m_swapchain->recreate();

        m_renderGraphPassesCompiler.onSwapChainResized(m_renderGraphPassesBuilder, m_renderGraphPassesStorage);

        for (const auto &[id, renderPass] : m_renderGraphPasses)
            renderPass.renderGraphPass->onSwapChainResized(m_renderGraphPassesStorage);
    }
    else if (result != VK_SUCCESS)
        throw std::runtime_error("Failed to present swap chain image: " + core::helpers::vulkanResultToString(result));

    m_perFrameData.additionalData.clear();
}

void RenderGraph::draw()
{
    if (begin())
        end();

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void RenderGraph::setup()
{
    for (auto &[id, pass] : m_renderGraphPasses)
    {
        m_renderGraphPassesBuilder.setCurrentPass(&pass.passInfo);
        pass.renderGraphPass->setup(m_renderGraphPassesBuilder);
    }

    compile();
}

void RenderGraph::sortRenderGraphPasses()
{
    auto hasDependencies = [](const RenderGraphPassData &a, const RenderGraphPassData &b)
    {
        for (auto &writesA : a.passInfo.writes)
        {
            for (auto &readsB : b.passInfo.reads)
                if (writesA.resourceId == readsB.resourceId)
                    return true;

            for (auto &writesB : b.passInfo.writes)
                if (writesA.resourceId == writesB.resourceId)
                    return true;
        }

        for (auto &readsA : a.passInfo.reads)
            for (auto &writesB : b.passInfo.writes)
                if (readsA.resourceId == writesB.resourceId)
                    return true;

        return false;
    };

    for (auto &[id, renderGraphPass] : m_renderGraphPasses)
    {
        for (auto &[id, secondRenderGraphPass] : m_renderGraphPasses)
        {
            if (renderGraphPass.id == secondRenderGraphPass.id)
                continue;

            if (hasDependencies(renderGraphPass, secondRenderGraphPass))
            {
                renderGraphPass.outgoing.push_back(secondRenderGraphPass.id);
                secondRenderGraphPass.indegree++;
            }
        }
    }

    std::queue<uint32_t> q;
    std::vector<uint32_t> sorted;

    for (auto &[id, renderGraphPass] : m_renderGraphPasses)
        if (renderGraphPass.indegree <= 0)
            q.push(renderGraphPass.id);

    while (!q.empty())
    {
        uint32_t n = q.front();
        q.pop();
        sorted.push_back(n);

        auto renderGraphPass = findRenderGraphPassById(n);

        if (!renderGraphPass)
        {
            std::cerr << "Failed to find pass. Error...\n";
            continue;
        }

        for (uint32_t dst : renderGraphPass->outgoing)
        {
            auto dstRenderGraphPass = findRenderGraphPassById(dst);

            if (!dstRenderGraphPass)
            {
                std::cerr << "Failed to find dst pass. Error...\n";
                continue;
            }

            if (--dstRenderGraphPass->indegree == 0)
                q.push(dst);
        }
    }

    if (sorted.size() != m_renderGraphPasses.size())
    {
        std::cerr << "Failed to build graph tree\n";
        return;
    }

    for (const auto &sortId : sorted)
    {
        auto renderGraphPass = findRenderGraphPassById(sortId);

        if (!renderGraphPass)
        {
            std::cerr << "Failed to find sorted node\n";
            continue;
        }

        m_sortedRenderGraphPasses.insert(renderGraphPass->renderGraphPass.get());
    }

    for (const auto &renderGraphPass : m_sortedRenderGraphPasses)
        std::cout << "Node: " << renderGraphPass->getDebugName() << '\n';
}

void RenderGraph::compile()
{
    m_renderGraphPassesCompiler.compile(m_renderGraphPassesBuilder, m_renderGraphPassesStorage);

    // std::cout << "Memory before render graphs compile: " << core::VulkanContext::getContext()->getDevice()->getTotalAllocatedVRAM() << '\n';

    for (const auto &[id, pass] : m_renderGraphPasses)
        pass.renderGraphPass->compile(m_renderGraphPassesStorage);

    // std::cout << "Memory after render graphs compile: " << core::VulkanContext::getContext()->getDevice()->getTotalAllocatedVRAM() << '\n';
}

void RenderGraph::createPreviewCameraDescriptorSets()
{
    m_previewCameraMapped.resize(MAX_FRAMES_IN_FLIGHT);
    m_previewCameraUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);
    m_previewCameraDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto &cameraBuffer = m_previewCameraUniformObjects.emplace_back(core::Buffer::createShared(sizeof(CameraUBO),
                                                                                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        cameraBuffer->map(m_previewCameraMapped[i]);

        m_previewCameraDescriptorSets[i] = DescriptorSetBuilder::begin()
                                               .addBuffer(cameraBuffer, sizeof(CameraUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                               .build(m_device, m_descriptorPool, EngineShaderFamilies::cameraDescriptorSetLayout->vk());
    }
}

void RenderGraph::createPerObjectDescriptorSets()
{
    m_perObjectDescriptorSets.resize(RenderGraph::MAX_FRAMES_IN_FLIGHT);

    m_bonesSSBOs.reserve(RenderGraph::MAX_FRAMES_IN_FLIGHT);

    static constexpr VkDeviceSize bonesStructSize = sizeof(glm::mat4);
    static constexpr uint8_t INIT_BONES_COUNT = 100;
    static constexpr VkDeviceSize INITIAL_SIZE = bonesStructSize * (INIT_BONES_COUNT * bonesStructSize);

    for (size_t i = 0; i < RenderGraph::MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto ssboBuffer = m_bonesSSBOs.emplace_back(core::Buffer::createShared(INITIAL_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                               core::memory::MemoryUsage::CPU_TO_GPU));

        m_perObjectDescriptorSets[i] = DescriptorSetBuilder::begin()
                                           .addBuffer(ssboBuffer, VK_WHOLE_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                           .build(core::VulkanContext::getContext()->getDevice(), m_descriptorPool, EngineShaderFamilies::objectDescriptorSetLayout->vk());
    }
}

void RenderGraph::createCameraDescriptorSets(VkSampler sampler, VkImageView imageView)
{
    m_cameraDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    m_cameraMapped.resize(MAX_FRAMES_IN_FLIGHT);
    m_lightMapped.resize(MAX_FRAMES_IN_FLIGHT);

    m_lightSpaceMatrixUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);
    m_cameraUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);
    m_lightSSBOs.reserve(MAX_FRAMES_IN_FLIGHT);

    static constexpr uint8_t INIT_LIGHTS_COUNT = 2;
    static constexpr VkDeviceSize INITIAL_SIZE = sizeof(LightData) * (INIT_LIGHTS_COUNT * sizeof(LightData));

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto &cameraBuffer = m_cameraUniformObjects.emplace_back(core::Buffer::createShared(sizeof(CameraUBO),
                                                                                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        auto &lightBuffer = m_lightSpaceMatrixUniformObjects.emplace_back(core::Buffer::createShared(sizeof(LightSpaceMatrixUBO),
                                                                                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        auto ssboBuffer = m_lightSSBOs.emplace_back(core::Buffer::createShared(INITIAL_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                               core::memory::MemoryUsage::CPU_TO_GPU));

        cameraBuffer->map(m_cameraMapped[i]);
        lightBuffer->map(m_lightMapped[i]);

        m_cameraDescriptorSets[i] = DescriptorSetBuilder::begin()
                                        .addBuffer(cameraBuffer, sizeof(CameraUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                        .addBuffer(lightBuffer, sizeof(LightSpaceMatrixUBO), 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                        .addImage(imageView, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                                        .addBuffer(ssboBuffer, VK_WHOLE_SIZE, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                        .build(m_device, m_descriptorPool, EngineShaderFamilies::cameraDescriptorSetLayout->vk());
    }

    createPreviewCameraDescriptorSets();
    createPerObjectDescriptorSets();
}

void RenderGraph::createRenderGraphResources()
{
    for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
    {
        auto commandPool = m_commandPools.emplace_back(core::CommandPool::createShared(core::VulkanContext::getContext()->getDevice(),
                                                                                       core::VulkanContext::getContext()->getGraphicsFamily()));

        m_commandBuffers.emplace_back(core::CommandBuffer::createShared(commandPool));

        m_secondaryCommandBuffers[frame].resize(MAX_RENDER_JOBS);

        for (size_t job = 0; job < MAX_RENDER_JOBS; ++job)
            m_secondaryCommandBuffers[frame][job] = core::CommandBuffer::createShared(commandPool, VK_COMMAND_BUFFER_LEVEL_SECONDARY);
    }

    sortRenderGraphPasses();
}

void RenderGraph::cleanResources()
{
    vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
    vkDeviceWaitIdle(m_device);

    for (const auto &primary : m_commandBuffers)
        primary->destroyVk();
    for (const auto &secondary : m_secondaryCommandBuffers)
        for (const auto &cb : secondary)
            cb->destroyVk();

    for (const auto &renderPass : m_renderGraphPasses)
        renderPass.second.renderGraphPass->cleanup();

    for (const auto &light : m_lightSpaceMatrixUniformObjects)
    {
        light->unmap();
        light->destroyVk();
    }

    for (const auto &camera : m_cameraUniformObjects)
    {
        camera->unmap();
        camera->destroyVk();
    }

    m_perFrameData.drawItems.clear();
    m_renderGraphPassesStorage.cleanup();

    EngineShaderFamilies::cleanEngineShaderFamilies();
}
ELIX_CUSTOM_NAMESPACE_END

ELIX_NESTED_NAMESPACE_END