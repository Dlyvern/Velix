#include "Editor/RenderGraphPasses/AnimationTreePreviewPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Core/Buffer.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

namespace
{
    struct ModelOnly
    {
        glm::mat4 model{1.0f};
        uint32_t objectId{0u};
        uint32_t padding[3]{0u, 0u, 0u};
    };

    struct PreviewCameraUBO
    {
        glm::mat4 view;
        glm::mat4 projection;
        glm::mat4 invView;
        glm::mat4 invProjection;
    };
}

AnimationTreePreviewPass::AnimationTreePreviewPass()
{
    m_viewport = VkViewport{0.0f, 0.0f, static_cast<float>(PREVIEW_SIZE), static_cast<float>(PREVIEW_SIZE), 0.0f, 1.0f};
    m_scissor = VkRect2D{{0, 0}, {PREVIEW_SIZE, PREVIEW_SIZE}};
    m_clearValues[0].color = {0.12f, 0.12f, 0.12f, 1.0f};
    m_clearValues[1].depthStencil = {1.0f, 0u};

    setDebugName("Animation tree preview pass");
}

void AnimationTreePreviewPass::setup(engine::renderGraph::RGPResourcesBuilder &builder)
{
    const auto device = core::VulkanContext::getContext()->getDevice();
    const VkExtent2D extent{PREVIEW_SIZE, PREVIEW_SIZE};
    m_depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());

    engine::renderGraph::RGPTextureDescription colorDesc(VK_FORMAT_R8G8B8A8_SRGB, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
    colorDesc.setExtent(extent);
    colorDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    colorDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    colorDesc.setAliasable(false);
    colorDesc.setDebugName("__ELIX_ANIMTREE_PREVIEW_COLOR__");
    m_colorHandler = builder.createTexture(colorDesc);

    engine::renderGraph::RGPTextureDescription depthDesc(
        m_depthFormat,
        engine::renderGraph::RGPTextureUsage::DEPTH_STENCIL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    depthDesc.setExtent(extent);
    depthDesc.setAliasable(false);
    depthDesc.setDebugName("__ELIX_ANIMTREE_PREVIEW_DEPTH__");
    m_depthHandler = builder.createTexture(depthDesc);

    // Minimal camera descriptor set layout — only binding 0 (camera UBO).
    // The preview shader (shader_simple_textured_mesh) only reads binding 0,
    // so we don't need the light-space and light SSBO bindings.
    {
        VkDescriptorSetLayoutBinding cameraBinding{};
        cameraBinding.binding         = 0;
        cameraBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        cameraBinding.descriptorCount = 1;
        cameraBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        m_cameraDescriptorSetLayout = core::DescriptorSetLayout::createShared(
            device, std::vector<VkDescriptorSetLayoutBinding>{cameraBinding});
    }

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
            *m_cameraDescriptorSetLayout,
            *engine::EngineShaderFamilies::materialDescriptorSetLayout},
        std::vector<VkPushConstantRange>{
            engine::PushConstant<ModelOnly>::getRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                            VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void AnimationTreePreviewPass::compile(engine::renderGraph::RGPResourcesStorage &storage)
{
    m_colorTarget = storage.getTexture(m_colorHandler);
    m_depthTarget = storage.getTexture(m_depthHandler);

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_cameraUBOs.resize(imageCount);
    m_cameraDescriptorSets.resize(imageCount, VK_NULL_HANDLE);
    const auto device = core::VulkanContext::getContext()->getDevice();
    const auto pool = core::VulkanContext::getContext()->getPersistentDescriptorPool();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_cameraUBOs[i] = core::Buffer::createShared(
            sizeof(PreviewCameraUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU);

        // Initialize with a sensible default
        PreviewCameraUBO defaultUBO{};
        defaultUBO.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        defaultUBO.projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.05f, 100.0f);
        defaultUBO.projection[1][1] *= -1.0f;
        defaultUBO.invView = glm::inverse(defaultUBO.view);
        defaultUBO.invProjection = glm::inverse(defaultUBO.projection);
        m_cameraUBOs[i]->upload(&defaultUBO, sizeof(PreviewCameraUBO));

        if (!m_cameraDescriptorSetsInitialized || m_cameraDescriptorSets[i] == VK_NULL_HANDLE)
        {
            m_cameraDescriptorSets[i] = engine::DescriptorSetBuilder::begin()
                .addBuffer(m_cameraUBOs[i], sizeof(PreviewCameraUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .build(device,
                       pool,
                       m_cameraDescriptorSetLayout);
        }
        else
        {
            engine::DescriptorSetBuilder::begin()
                .addBuffer(m_cameraUBOs[i], sizeof(PreviewCameraUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .update(device, m_cameraDescriptorSets[i]);
        }
    }

    m_cameraDescriptorSetsInitialized = true;
}

void AnimationTreePreviewPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                      const engine::RenderGraphPassPerFrameData &data,
                                      const engine::RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    if (!m_pendingData.hasData || m_pendingData.meshes.empty())
        return;

    // Upload this frame's orbit view/projection to the per-frame UBO
    const uint32_t imageIndex = renderContext.currentImageIndex;
    if (imageIndex < m_cameraUBOs.size() && m_cameraUBOs[imageIndex])
    {
        PreviewCameraUBO ubo{};
        ubo.view = m_pendingData.viewMatrix;
        ubo.projection = m_pendingData.projMatrix;
        ubo.invView = glm::inverse(ubo.view);
        ubo.invProjection = glm::inverse(ubo.projection);
        m_cameraUBOs[imageIndex]->upload(&ubo, sizeof(PreviewCameraUBO));
    }

    engine::GraphicsPipelineKey key{};
    key.shader = engine::ShaderId::PreviewMesh;
    key.cull = engine::CullMode::None;
    key.depthTest = true;
    key.depthWrite = true;
    key.depthCompare = VK_COMPARE_OP_LESS;
    key.polygonMode = VK_POLYGON_MODE_FILL;
    key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats = {VK_FORMAT_R8G8B8A8_SRGB};
    key.depthFormat = m_depthFormat;
    key.pipelineLayout = m_pipelineLayout;

    auto pipeline = engine::GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    for (size_t i = 0; i < m_pendingData.meshes.size(); ++i)
    {
        auto *mesh = m_pendingData.meshes[i];
        if (!mesh)
            continue;

        VkBuffer vertexBuf[] = {mesh->vertexBuffer};
        VkDeviceSize offset[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuf, offset);
        vkCmdBindIndexBuffer(commandBuffer, mesh->indexBuffer, 0, mesh->indexType);

        const glm::mat4 model =
            (i < m_pendingData.meshModelMatrices.size()) ? m_pendingData.meshModelMatrices[i]
                                                         : m_pendingData.modelMatrix;

        ModelOnly pushConstant{};
        pushConstant.model = model;
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(ModelOnly), &pushConstant);

        auto *mat = (i < m_pendingData.materials.size()) ? m_pendingData.materials[i] : nullptr;
        VkDescriptorSet matDS = mat
                                    ? mat->getDescriptorSet(renderContext.currentFrame)
                                    : engine::Material::getDefaultMaterial()->getDescriptorSet(renderContext.currentFrame);

        VkDescriptorSet cameraDS = (imageIndex < m_cameraDescriptorSets.size() && m_cameraDescriptorSets[imageIndex] != VK_NULL_HANDLE)
            ? m_cameraDescriptorSets[imageIndex]
            : data.previewCameraDescriptorSet;

        std::array<VkDescriptorSet, 2> sets{cameraDS, matDS};

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                0, static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

        engine::renderGraph::profiling::cmdDrawIndexed(commandBuffer, mesh->indicesCount, 1, 0, 0, 0);
    }
}

std::vector<engine::renderGraph::IRenderGraphPass::RenderPassExecution>
AnimationTreePreviewPass::getRenderPassExecutions(const engine::RenderGraphPassContext &) const
{
    RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = {PREVIEW_SIZE, PREVIEW_SIZE};

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = m_colorTarget->vkImageView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue = m_clearValues[0];
    exec.colorsRenderingItems = {colorAttachment};

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView = m_depthTarget->vkImageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue = m_clearValues[1];
    exec.depthRenderingItem = depthAttachment;

    exec.colorFormats = {VK_FORMAT_R8G8B8A8_SRGB};
    exec.depthFormat = m_depthFormat;
    exec.useDepth = true;

    exec.targets[m_colorHandler] = m_colorTarget;
    exec.targets[m_depthHandler] = m_depthTarget;

    return {exec};
}

void AnimationTreePreviewPass::freeResources()
{
    m_colorTarget = nullptr;
    m_depthTarget = nullptr;
    m_cameraUBOs.clear();
    for (auto &ds : m_cameraDescriptorSets) ds = VK_NULL_HANDLE;
    m_cameraDescriptorSets.clear();
    m_cameraDescriptorSetsInitialized = false;
}

VkImageView AnimationTreePreviewPass::getOutputImageView() const
{
    return m_colorTarget ? m_colorTarget->vkImageView() : VK_NULL_HANDLE;
}

VkSampler AnimationTreePreviewPass::getOutputSampler() const
{
    return m_sampler ? m_sampler->vk() : VK_NULL_HANDLE;
}

ELIX_NESTED_NAMESPACE_END
