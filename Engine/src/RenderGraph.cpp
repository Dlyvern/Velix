#include "Engine/RenderGraph.hpp"
#include "Core/VulkanContext.hpp"

#include "Engine/Render/GraphPasses/BaseRenderGraphPass.hpp"
#include <iostream>

#include "Core/RenderPass.hpp"
#include <cstring>

#include "Core/Shader.hpp"
#include "Core/VulkanHelpers.hpp"

#include "Engine/GraphicsPipelineBuilder.hpp"

#include "Engine/Render/GraphPasses/BaseRenderGraphPass.hpp"
#include "Engine/Render/GraphPasses/OffscreenRenderGraphPass.hpp"

#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Core/VulkanHelpers.hpp"

#include "Engine/PushConstant.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"

struct ModelPushConstant
{
    glm::mat4 model;
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)

RenderGraph::RenderGraph(VkDevice device, core::SwapChain::SharedPtr swapchain, Scene::SharedPtr scene) : m_device(device), 
m_swapchain(swapchain), m_scene(scene)
{
    //!All engine's proxies should be named with __ELIX__ prefix
    //!Describe proxies data then compile it and then build it via RenderGraphPassBuilder

    m_builder = std::make_shared<RenderGraphPassBuilder>();

    m_syncObject = std::make_unique<core::SyncObject>(m_device, MAX_FRAMES_IN_FLIGHT);

    m_swapChainProxy = m_builder->createProxy<SwapChainRenderGraphProxy>("__ELIX_SWAP_CHAIN_PROXY__");
    m_swapChainProxy->renderPassProxy = m_builder->createProxy<RenderPassRenderGraphProxy>("__ELIX_SWAP_CHAIN_RENDER_PASS_PROXY__");

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchain->getImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = core::helpers::findDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


    VkAttachmentReference colorAttachmentReference{};
    colorAttachmentReference.attachment = 0;
    colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentReference{};
    depthAttachmentReference.attachment = 1;
    depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentReference;
    subpass.pDepthStencilAttachment = &depthAttachmentReference;

    std::vector<VkSubpassDependency> dependency;
    dependency.resize(1);
    dependency[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency[0].dstSubpass = 0;
    dependency[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency[0].srcAccessMask = 0;
    dependency[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::vector<VkAttachmentDescription> attachments{colorAttachment, depthAttachment};

    // m_swapChainProxy->renderPassProxy->attachments.insert(m_swapChainProxy->renderPassProxy->attachments.begin(), attachments.begin(), attachments.end());
    // m_swapChainProxy->renderPassProxy->subpasses.push_back(subpass);
    // m_swapChainProxy->renderPassProxy->dependencies.push_back(dependency);

    //TODO IT SHOULD BE REMOVED IMMEDIATLY
    m_swapChainProxy->renderPassProxy->storage.data = core::RenderPass::create(m_device, attachments, {subpass}, dependency);

    m_swapChainProxy->imageViews.resize(swapchain->getImages().size());
    m_swapChainProxy->storage.data.resize(m_swapChainProxy->imageViews.size());
    m_swapChainProxy->images.resize(swapchain->getImages().size());

    m_depthImageProxy = m_builder->createProxy<ImageRenderGraphProxy>("__ELIX_SWAP_CHAIN_DEPTH_PROXY__");

    m_depthImageProxy->width = m_swapchain->getExtent().width;
    m_depthImageProxy->height = m_swapchain->getExtent().height;
    m_depthImageProxy->usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    m_depthImageProxy->format = core::helpers::findDepthFormat();
    m_depthImageProxy->properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    m_depthImageProxy->aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    m_depthImageProxy->isDependedOnSwapChain = true;
    
    m_depthImageProxy->addOnSwapChainRecretedFunction([this]()
    {     
        m_depthImageProxy->storage.data->transitionImageLayout(core::helpers::findDepthFormat(), VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_commandPool, core::VulkanContext::getContext()->getGraphicsQueue());
    });

    m_swapChainProxy->additionalImages.push_back(m_depthImageProxy);

    m_staticMeshProxy = m_builder->createProxy<StaticMeshRenderGraphProxy>("__ELIX_SCENE_STATIC_MESH_PROXY__");

    auto queueFamilyIndices = core::VulkanContext::findQueueFamilies(core::VulkanContext::getContext()->getPhysicalDevice(), core::VulkanContext::getContext()->getSurface());
    m_commandPool = core::CommandPool::create(m_device, queueFamilyIndices.graphicsFamily.value());

    VkQueue graphicsQueue = core::VulkanContext::getContext()->getGraphicsQueue();

    for(const auto& entity : m_scene->getEntities())
    {
        if(auto staticMeshComponent = entity->getComponent<StaticMeshComponent>())
        {
            const auto& mesh = staticMeshComponent->getMesh();
            size_t hashData{0};

            hashing::hash(hashData, mesh.indices.size());

            for(const auto& indices : mesh.indices)
                hashing::hash(hashData, indices);

            hashing::hash(hashData, mesh.vertices.size());

            auto gpuMesh = GPUMesh::createFromMesh(mesh, graphicsQueue, m_commandPool);
            m_staticMeshProxy->storage.data[hashData] = gpuMesh;
            m_staticMeshProxy->transformationBasedOnMesh[entity] = gpuMesh;
        }
    }

    for(size_t index = 0; index < m_swapchain->getImages().size(); ++index)
    {
        m_swapChainProxy->images[index] = m_swapchain->getImages()[index];

        VkImageViewCreateInfo createInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        createInfo.image = m_swapchain->getImages()[index];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_swapchain->getImageFormat();
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if(vkCreateImageView(m_device, &createInfo, nullptr, &m_swapChainProxy->imageViews[index]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image views");
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

        m_cameraSetLayout = core::DescriptorSetLayout::create(m_device, {uboLayoutBinding});
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
        textureLayoutBinding.binding = 1;
        textureLayoutBinding.descriptorCount = 1;
        textureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureLayoutBinding.pImmutableSamplers = nullptr;
        textureLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding colorLayoutBinding{};
        colorLayoutBinding.binding = 2;
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

void RenderGraph::createSwapChainResources()
{
    for(size_t index = 0; index < m_swapchain->getImages().size(); ++index)
    {
        m_swapChainProxy->images[index] = m_swapchain->getImages()[index];

        VkImageViewCreateInfo createInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        createInfo.image = m_swapchain->getImages()[index];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_swapchain->getImageFormat();
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if(vkCreateImageView(m_device, &createInfo, nullptr, &m_swapChainProxy->imageViews[index]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image views");
    }

    for(auto& depend : m_swapChainProxy->additionalImages)
    {
        depend->height = m_swapchain->getExtent().height;
        depend->width = m_swapchain->getExtent().width;

        m_builder->buildProxy(depend.get(), m_device);

        depend->onSwapChainRecreated();
    }

    for(size_t i = 0; i < m_swapChainProxy->imageViews.size(); ++i)
    {
        std::vector<VkImageView> attachments{m_swapChainProxy->imageViews[i]};

        for(const auto& attachment : m_swapChainProxy->additionalImages)
            attachments.push_back(attachment->imageView);

        VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebufferInfo.renderPass = m_swapChainProxy->renderPassProxy->storage.data->vk();
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = m_swapchain->getExtent().width;
        framebufferInfo.height = m_swapchain->getExtent().height;
        framebufferInfo.layers = 1;

        if(vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapChainProxy->storage.data[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create framebuffer");
    }
}

void RenderGraph::cleanupSwapChainResources()
{
    for(auto& depend : m_swapChainProxy->additionalImages)
    {
        depend->storage.data->destroy();
    
        if(depend->imageView)
            vkDestroyImageView(m_device, depend->imageView, nullptr);
    }
    for (auto view : m_swapChainProxy->imageViews)
        if (view)
            vkDestroyImageView(m_device, view, nullptr);

    for(auto framebuffer : m_swapChainProxy->storage.data)
        if(framebuffer)
            vkDestroyFramebuffer(m_device, framebuffer, nullptr);
}

void RenderGraph::begin()
{
    auto& lock = m_syncObject->getSync(m_currentFrame);

    vkWaitForFences(m_device, 1, &lock.inFlightFence, VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain->vk(), UINT64_MAX, lock.imageAvailableSemaphore, VK_NULL_HANDLE, &m_imageIndex);

    if(result == VK_ERROR_OUT_OF_DATE_KHR || m_rebuildSwapchain)
    {
        m_swapchain->recreate();
        cleanupSwapChainResources();
        createSwapChainResources();
        m_rebuildSwapchain = false;
        m_swapChainProxy->onSwapChainRecreated();
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

    for(const auto& [_, renderGraphPass] : m_renderGraphPasses)
    {
        renderGraphPass->update(m_currentFrame, m_imageIndex, m_swapChainProxy->storage.data[m_imageIndex]);

        VkRenderPassBeginInfo beginRenderPassInfo;
        renderGraphPass->getRenderPassBeginInfo(beginRenderPassInfo);

        auto& secCB = m_secondaryCommandBuffers[m_currentFrame][passIndex];

        VkCommandBufferInheritanceInfo inherit{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
        inherit.renderPass = beginRenderPassInfo.renderPass;
        inherit.subpass = 0;
        inherit.framebuffer = beginRenderPassInfo.framebuffer;

        vkCmdBeginRenderPass(primaryCommandBuffer->vk(), &beginRenderPassInfo, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

        secCB->begin(VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, &inherit);

        renderGraphPass->execute(secCB);

        secCB->end();

        // vkSecondaries.push_back(secCB->vk());

        VkCommandBuffer vkSec = secCB->vk();
        vkCmdExecuteCommands(primaryCommandBuffer->vk(), 1, &vkSec);

        ++passIndex;

        vkCmdEndRenderPass(primaryCommandBuffer->vk());
    }

    // if(!vkSecondaries.empty())
    //     vkCmdExecuteCommands(primaryCommandBuffer->vk(), static_cast<uint32_t>(vkSecondaries.size()), vkSecondaries.data());

    primaryCommandBuffer->end();
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

    if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_rebuildSwapchain)
    {
        m_swapchain->recreate();
        cleanupSwapChainResources();
        createSwapChainResources();
        m_rebuildSwapchain = false;
        m_swapChainProxy->onSwapChainRecreated();
    }
    else if(result != VK_SUCCESS)
        throw std::runtime_error("Failed to present swap chain image");

    m_swapChainProxy->currentImageIndex = m_imageIndex;

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
        pass.second->setup(m_builder);

    compile();
}

void RenderGraph::createGraphicsPipeline()
{
    core::Shader shader("/home/dlyvern/Projects/Velix/resources/shaders/test_vert.spv", "/home/dlyvern/Projects/Velix/resources/shaders/test_frag.spv");

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

    auto bindingDescription = engine::Vertex3D::getBindingDescription();
    auto attributeDescription = engine::Vertex3D::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescription.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescription.data();

    auto viewport = m_swapchain->getViewport();
    auto scissor = m_swapchain->getScissor();

    engine::GraphicsPipelineBuilder graphicsPipelineBuilder;
    graphicsPipelineBuilder.layout = m_pipelineLayout->vk();
    graphicsPipelineBuilder.renderPass = m_swapChainProxy->renderPassProxy->storage.data->vk();
    graphicsPipelineBuilder.viewportState.pViewports = &viewport;
    graphicsPipelineBuilder.viewportState.pScissors = &scissor;
    graphicsPipelineBuilder.shaderStages = shader.getShaderStages();

    m_graphicsPipeline = graphicsPipelineBuilder.build(m_device, vertexInputInfo);
}

void RenderGraph::createDirectionalLightDescriptorSets()
{
    m_lightSSBOs.reserve(MAX_FRAMES_IN_FLIGHT);
    m_directionalLightDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_directionalLightSetLayout->vk());

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();

    if(vkAllocateDescriptorSets(m_device, &allocInfo, m_directionalLightDescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate decsriptor sets");
    
    VkQueue graphicsQueue = core::VulkanContext::getContext()->getGraphicsQueue();

    static constexpr uint8_t INIT_LIGHTS_COUNT = 2;

    for(size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        VkDeviceSize initialSize = sizeof(LightData) * (INIT_LIGHTS_COUNT * sizeof(LightData));

        auto ssboBuffer = m_lightSSBOs.emplace_back(core::Buffer::create(initialSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

        VkDescriptorBufferInfo ssboBufferInfo{};
        ssboBufferInfo.buffer = ssboBuffer->vkBuffer();
        ssboBufferInfo.offset = 0;
        ssboBufferInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 1> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_directionalLightDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &ssboBufferInfo;

        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void RenderGraph::createCameraDescriptorSets()
{
    m_cameraDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    m_cameraUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_cameraSetLayout->vk());

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();

    if(vkAllocateDescriptorSets(m_device, &allocInfo, m_cameraDescriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate decsriptor sets");

    for(size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto uniformObject = m_cameraUniformObjects.emplace_back(UniformBufferObject<CameraUBO>::create(m_device));

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformObject->getBuffer()->vkBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = uniformObject->getDeviceSize();

        std::array<VkWriteDescriptorSet, 1> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_cameraDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

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
    for(auto& [id, proxy] : m_builder->getProxies())
    {
        if(auto image = dynamic_cast<ImageRenderGraphProxy*>(proxy.get()))
            m_builder->buildProxy(image, m_device);
        // else if(auto renderPass = dynamic_cast<RenderPassRenderGraphProxy*>(proxy.get()))
        //     m_builder->buildProxy(renderPass, m_device);
    }

    m_depthImageProxy->storage.data->transitionImageLayout(core::helpers::findDepthFormat(), VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, m_commandPool, core::VulkanContext::getContext()->getGraphicsQueue());

    for(const auto& pass : m_renderGraphPasses)
        pass.second->compile();

    createSwapChainResources();
}


ELIX_NESTED_NAMESPACE_END