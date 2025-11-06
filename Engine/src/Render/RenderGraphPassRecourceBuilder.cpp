#include "Engine/Render/RenderGraphPassResourceBuilder.hpp"
#include "Engine/Hash.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

std::size_t RenderGraphPassRecourceBuilder::createTexture(const RGPRDTexture& description, const ResourceUserData& userData)
{
    std::size_t data{0};

    for(const auto& c : description.name)
        hashing::hash(data, static_cast<uint8_t>(c));

    hashing::hash(data, description.arrayLayers);
    hashing::hash(data, static_cast<uint64_t>(description.format));
    hashing::hash(data, static_cast<uint64_t>(description.initialLayout));
    hashing::hash(data, static_cast<uint64_t>(description.samples));
    hashing::hash(data, description.size.height);
    hashing::hash(data, static_cast<int>(description.size.scale));
    hashing::hash(data, description.size.width);
    hashing::hash(data, static_cast<uint16_t>(description.size.type));
    hashing::hash(data, static_cast<uint64_t>(description.usage));
    hashing::hash(data, static_cast<uint64_t>(description.aspect));

    if(m_textureHashes.find(data) == m_textureHashes.end())
        m_textureHashes[data] = description;

    if(userData.access != ResourceAccess::NONE && userData.user)
        m_textureUsers[data].push_back(userData);

    return data;
}

std::size_t RenderGraphPassRecourceBuilder::createRenderPass(const RGPRDRenderPass& renderPassDescription)
{
    std::size_t data{0};

    for(const auto& c : renderPassDescription.name)
        hashing::hash(data, static_cast<uint8_t>(c));

    if(m_renderPasHashes.find(data) == m_renderPasHashes.end())
        m_renderPasHashes[data] = renderPassDescription;
    
    return data;
}

std::size_t RenderGraphPassRecourceBuilder::createFramebuffer(const RGPRDFramebuffer& framebufferDescription)
{
    std::size_t data{0};

    for(const auto& c : framebufferDescription.name)
        hashing::hash(data, static_cast<uint8_t>(c));

    for(const auto& attachmentHash : framebufferDescription.attachmentsHash)
        hashing::hash(data, attachmentHash);

    hashing::hash(data, framebufferDescription.size.height);
    hashing::hash(data, static_cast<int>(framebufferDescription.size.scale));
    hashing::hash(data, framebufferDescription.size.width);
    hashing::hash(data, static_cast<uint16_t>(framebufferDescription.size.type));
    hashing::hash(data, framebufferDescription.layers);

    if(m_framebufferHashes.find(data) == m_framebufferHashes.end())
        m_framebufferHashes[data] = framebufferDescription;

    return data;
}

std::size_t RenderGraphPassRecourceBuilder::createBuffer(const RGPRDBuffer& bufferDescription)
{
    std::size_t data{0};

    for(const auto& c : bufferDescription.name)
        hashing::hash(data, static_cast<uint8_t>(c));

    if(m_bufferHashes.find(data) == m_bufferHashes.end())
        m_bufferHashes[data] = bufferDescription;

    m_bufferHashes[data] = bufferDescription;

    return data;
}

std::size_t RenderGraphPassRecourceBuilder::createGraphicsPipeline(const RGPRDGraphicsPipeline& graphicsPipelineDescription)
{
    std::size_t data{0};

    for(const auto& c : graphicsPipelineDescription.name)
        hashing::hash(data, static_cast<uint8_t>(c));

    hashing::hash(data, graphicsPipelineDescription.inputAssembly.flags);
    hashing::hash(data, graphicsPipelineDescription.inputAssembly.primitiveRestartEnable);
    hashing::hash(data, graphicsPipelineDescription.inputAssembly.topology);

    hashing::hash(data, graphicsPipelineDescription.rasterizer.cullMode);
    hashing::hash(data, static_cast<int>(graphicsPipelineDescription.rasterizer.depthBiasClamp));
    hashing::hash(data, static_cast<int>(graphicsPipelineDescription.rasterizer.depthBiasConstantFactor));
    hashing::hash(data, graphicsPipelineDescription.rasterizer.depthBiasEnable);
    hashing::hash(data, static_cast<int>(graphicsPipelineDescription.rasterizer.depthBiasSlopeFactor));
    hashing::hash(data, graphicsPipelineDescription.rasterizer.depthClampEnable);
    hashing::hash(data, graphicsPipelineDescription.rasterizer.frontFace);
    hashing::hash(data, static_cast<int>(graphicsPipelineDescription.rasterizer.lineWidth));
    hashing::hash(data, graphicsPipelineDescription.rasterizer.polygonMode);
    hashing::hash(data, graphicsPipelineDescription.rasterizer.rasterizerDiscardEnable);

    hashing::hash(data, graphicsPipelineDescription.multisampling.alphaToCoverageEnable);
    hashing::hash(data, graphicsPipelineDescription.multisampling.alphaToOneEnable);
    hashing::hash(data, static_cast<int>(graphicsPipelineDescription.multisampling.minSampleShading));
    hashing::hash(data, graphicsPipelineDescription.multisampling.rasterizationSamples);
    hashing::hash(data, graphicsPipelineDescription.multisampling.sampleShadingEnable);

    m_graphicsPipelineHashes[data] = graphicsPipelineDescription;

    // VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    // VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    // VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    // VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    // VkPipelineDynamicStateCreateInfo dynamicState{};
    // std::vector<VkDynamicState> dynamicStates;

    // std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions;
    // std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;

    // VkViewport viewport;
    // VkRect2D scissor;

    // VkPipelineLayout layout = VK_NULL_HANDLE;
    // VkRenderPass renderPass = VK_NULL_HANDLE;
    // uint32_t subpass = 0;

    // std::vector<VkPipelineShaderStageCreateInfo> shaderStages;


    return data;
}

void RenderGraphPassRecourceBuilder::forceTextureCache(std::size_t hash, const RGPRDTexture& textureDescription)
{
    m_textureHashes[hash] = textureDescription;
}

void RenderGraphPassRecourceBuilder::forceFramebufferCache(std::size_t hash, const RGPRDFramebuffer& framebufferDescription)
{
    m_framebufferHashes[hash] = framebufferDescription;
}

const std::unordered_map<std::size_t, RenderGraphPassRecourceBuilder::RGPRDRenderPass>& RenderGraphPassRecourceBuilder::getRenderPassHashes() const
{
    return m_renderPasHashes;
}

const std::unordered_map<std::size_t, RenderGraphPassRecourceBuilder::RGPRDTexture>& RenderGraphPassRecourceBuilder::getTextureHashes() const
{
    return m_textureHashes;
}

const std::unordered_map<std::size_t, RenderGraphPassRecourceBuilder::RGPRDBuffer>& RenderGraphPassRecourceBuilder::getBufferHashes() const
{
    return m_bufferHashes;
}

const std::unordered_map<std::size_t, RenderGraphPassRecourceBuilder::RGPRDFramebuffer>& RenderGraphPassRecourceBuilder::getFramebufferHashes() const
{
    return m_framebufferHashes;
}

const std::unordered_map<std::size_t, RenderGraphPassRecourceBuilder::RGPRDGraphicsPipeline>& RenderGraphPassRecourceBuilder::getGraphicsPipelineHashes() const
{
    return m_graphicsPipelineHashes;
}

void RenderGraphPassRecourceBuilder::cleanup()
{
    m_bufferHashes.clear();
    m_framebufferHashes.clear();
    m_graphicsPipelineHashes.clear();
    m_renderPasHashes.clear();
    m_textureHashes.clear();
    m_textureUsers.clear();
}

ELIX_NESTED_NAMESPACE_END