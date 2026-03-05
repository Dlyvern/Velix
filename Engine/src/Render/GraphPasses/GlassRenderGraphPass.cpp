#include "Engine/Render/GraphPasses/GlassRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Material.hpp"
#include "Engine/Mesh.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

struct GlassPC
{
    uint32_t baseInstance{0};
    float    ior{1.5f};
    float    frosted{0.0f};
    float    tintR{1.0f};
    float    tintG{1.0f};
    float    tintB{1.0f};
    float    _pad0{0.0f};
    float    _pad1{0.0f};
};
static_assert(sizeof(GlassPC) == 32);

GlassRenderGraphPass::GlassRenderGraphPass(std::vector<RGPResourceHandler> &colorInputHandlers,
                                           RGPResourceHandler              &depthHandler)
    : m_colorInputHandlers(colorInputHandlers),
      m_depthHandler(depthHandler)
{
    setDebugName("Glass render graph pass");
    m_clearValues[0].color = {0.f, 0.f, 0.f, 0.f};
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void GlassRenderGraphPass::setup(RGPResourcesBuilder &builder)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    for (uint32_t i = 0; i < imageCount; ++i)
        builder.read(m_colorInputHandlers[i], RGPTextureUsage::SAMPLED);
    builder.read(m_depthHandler, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this] { return m_extent; });

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_GLASS_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT);
    }

    auto device = core::VulkanContext::getContext()->getDevice();

    // Passthrough pipeline layout (set 0 = single sampler2D, copies input to output)
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding         = 0;
        b.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        m_passthroughLayout = core::DescriptorSetLayout::createShared(device,
            std::vector<VkDescriptorSetLayoutBinding>{b});

        m_passthroughPipelineLayout = core::PipelineLayout::createShared(
            device,
            std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
                *m_passthroughLayout},
            std::vector<VkPushConstantRange>{});
    }

    // Glass pipeline layout:
    //   set 0 = camera  (cameraDescriptorSetLayout)
    //   set 1 = sceneColor + depth  (2-binding)
    //   set 2 = per-object SSBO  (objectDescriptorSetLayout)
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings(2);
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        m_glassInputLayout = core::DescriptorSetLayout::createShared(device, bindings);

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset     = 0;
        pcRange.size       = sizeof(GlassPC);

        m_glassPipelineLayout = core::PipelineLayout::createShared(
            device,
            std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
                *EngineShaderFamilies::cameraDescriptorSetLayout,
                *m_glassInputLayout,
                *EngineShaderFamilies::objectDescriptorSetLayout},
            std::vector<VkPushConstantRange>{pcRange});
    }

    m_sampler      = core::Sampler::createShared(VK_FILTER_LINEAR,  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_depthSampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void GlassRenderGraphPass::compile(RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputRenderTargets.resize(imageCount);
    m_inputRenderTargets.resize(imageCount);
    m_passthroughSets.resize(imageCount, VK_NULL_HANDLE);
    m_glassInputSets.resize(imageCount, VK_NULL_HANDLE);

    m_depthRenderTarget = storage.getTexture(m_depthHandler);

    auto device = core::VulkanContext::getContext()->getDevice();
    auto pool   = core::VulkanContext::getContext()->getPersistentDescriptorPool();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputRenderTargets[i] = storage.getTexture(m_outputHandlers[i]);
        m_inputRenderTargets[i]  = storage.getTexture(m_colorInputHandlers[i]);

        if (!m_descriptorSetsInitialized)
        {
            m_passthroughSets[i] = DescriptorSetBuilder::begin()
                .addImage(m_inputRenderTargets[i]->vkImageView(), m_sampler,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .build(device, pool, m_passthroughLayout);

            m_glassInputSets[i] = DescriptorSetBuilder::begin()
                .addImage(m_inputRenderTargets[i]->vkImageView(), m_sampler,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,        0)
                .addImage(m_depthRenderTarget->vkImageView(),     m_depthSampler,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                .build(device, pool, m_glassInputLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(m_inputRenderTargets[i]->vkImageView(), m_sampler,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .update(device, m_passthroughSets[i]);

            DescriptorSetBuilder::begin()
                .addImage(m_inputRenderTargets[i]->vkImageView(), m_sampler,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,        0)
                .addImage(m_depthRenderTarget->vkImageView(),     m_depthSampler,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1)
                .update(device, m_glassInputSets[i]);
        }
    }

    if (!m_descriptorSetsInitialized)
        m_descriptorSetsInitialized = true;
}

void GlassRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                  const RenderGraphPassPerFrameData &data,
                                  const RenderGraphPassContext &ctx)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(),  0, 1, &m_scissor);

    recordPassthrough(commandBuffer, ctx.currentImageIndex);
    recordGlass(commandBuffer, data, ctx.currentImageIndex);
}

void GlassRenderGraphPass::recordPassthrough(core::CommandBuffer::SharedPtr cmd,
                                             uint32_t imageIndex)
{
    if (imageIndex >= m_passthroughSets.size() || !m_passthroughSets[imageIndex])
        return;

    GraphicsPipelineKey key{};
    key.shader         = ShaderId::Present;
    key.blend          = BlendMode::None;
    key.cull           = CullMode::None;
    key.depthTest      = false;
    key.depthWrite     = false;
    key.depthCompare   = VK_COMPARE_OP_LESS;
    key.polygonMode    = VK_POLYGON_MODE_FILL;
    key.topology       = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats   = {m_format};
    key.pipelineLayout = m_passthroughPipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(cmd->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(cmd->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_passthroughPipelineLayout, 0, 1,
                            &m_passthroughSets[imageIndex], 0, nullptr);

    profiling::cmdDraw(cmd, 3, 1, 0, 0);
}

void GlassRenderGraphPass::recordGlass(core::CommandBuffer::SharedPtr cmd,
                                       const RenderGraphPassPerFrameData &data,
                                       uint32_t imageIndex)
{
    bool anyGlass = false;
    for (const auto &batch : data.drawBatches)
    {
        if (batch.mesh && batch.material &&
            (batch.material->params().flags & Material::EMATERIAL_FLAG_ALPHA_BLEND))
        {
            anyGlass = true;
            break;
        }
    }
    if (!anyGlass)
        return;

    GraphicsPipelineKey key{};
    key.shader         = ShaderId::Glass;
    key.cull           = CullMode::None;
    key.depthTest      = false;
    key.depthWrite     = false;
    key.depthCompare   = VK_COMPARE_OP_LESS;
    key.polygonMode    = VK_POLYGON_MODE_FILL;
    key.topology       = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats   = {m_format};
    key.blend          = BlendMode::AlphaBlend;
    key.pipelineLayout = m_glassPipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(cmd->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkDescriptorSet sets[3] = {
        data.cameraDescriptorSet,
        m_glassInputSets[imageIndex],
        data.perObjectDescriptorSet};
    vkCmdBindDescriptorSets(cmd->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_glassPipelineLayout, 0, 3, sets, 0, nullptr);

    for (const auto &batch : data.drawBatches)
    {
        if (!batch.mesh || !batch.material)
            continue;
        if (!(batch.material->params().flags & Material::EMATERIAL_FLAG_ALPHA_BLEND))
            continue;

        const auto &p = batch.material->params();

        GlassPC pc{};
        pc.baseInstance = batch.firstInstance;
        pc.ior          = p.ior;
        pc.frosted      = p.roughnessFactor;
        pc.tintR        = p.baseColorFactor.r;
        pc.tintG        = p.baseColorFactor.g;
        pc.tintB        = p.baseColorFactor.b;

        vkCmdPushConstants(cmd->vk(), m_glassPipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(GlassPC), &pc);

        VkBuffer     vb      = batch.mesh->vertexBuffer;
        VkDeviceSize offset  = 0;
        vkCmdBindVertexBuffers(cmd->vk(), 0, 1, &vb, &offset);
        vkCmdBindIndexBuffer(cmd->vk(), batch.mesh->indexBuffer, 0, batch.mesh->indexType);

        profiling::cmdDrawIndexed(cmd, batch.mesh->indicesCount,
                                  batch.instanceCount, 0, 0, 0);
    }
}

std::vector<IRenderGraphPass::RenderPassExecution> GlassRenderGraphPass::getRenderPassExecutions(
    const RenderGraphPassContext &ctx) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView   = m_outputRenderTargets[ctx.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue  = m_clearValues[0];

    exec.colorsRenderingItems = {color};
    exec.useDepth             = false;
    exec.colorFormats         = {m_format};
    exec.depthFormat          = VK_FORMAT_UNDEFINED;

    exec.targets[m_outputHandlers[ctx.currentImageIndex]] =
        m_outputRenderTargets[ctx.currentImageIndex];

    return {exec};
}

void GlassRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent   = extent;
    m_viewport = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    m_scissor  = {{0, 0}, extent};
    requestRecompilation();
}

ELIX_NESTED_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END
