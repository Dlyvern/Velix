#include "Engine/Builders/GraphicsPipelineManager.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Builders/GraphicsPipelineBuilder.hpp"
#include "Engine/Caches/GraphicsPipelineCache.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include "Engine/Vertex.hpp"

#include <iostream>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

core::GraphicsPipeline::SharedPtr GraphicsPipelineManager::getOrCreate(GraphicsPipelineKey key)
{
    auto it = m_pipelines.find(key);

    if (it != m_pipelines.end())
        return it->second;

    std::cout << "Created a new key\n";

    auto created = createPipeline(key);

    m_pipelines[key] = created;

    return created;
}

void GraphicsPipelineManager::init()
{
    staticShader = std::make_shared<core::Shader>("./resources/shaders/static_mesh.vert.spv", "./resources/shaders/static_mesh.frag.spv");
    skeletonShader = std::make_shared<core::Shader>("./resources/shaders/skeleton_mesh.vert.spv", "./resources/shaders/static_mesh.frag.spv");
    wireframeShader = std::make_shared<core::Shader>("./resources/shaders/wireframe_mesh.vert.spv", "./resources/shaders/debug_red.frag.spv");
    stencilShader = std::make_shared<core::Shader>("./resources/shaders/wireframe_mesh.vert.spv", "./resources/shaders/debug_yellow.frag.spv");
    shadowStaticShader = std::make_shared<core::Shader>("./resources/shaders/static_mesh_shadow.vert.spv",
                                                        "./resources/shaders/empty.frag.spv");

    previewMeshShader = std::make_shared<core::Shader>("./resources/shaders/shader_simple_textured_mesh.vert.spv",
                                                       "./resources/shaders/shader_simple_textured_mesh.frag.spv");

    skyboxHDRShader = std::make_shared<core::Shader>("./resources/shaders/skybox.vert.spv", "./resources/shaders/skybox_hdr.frag.spv");
    skyboxShader = std::make_shared<core::Shader>("./resources/shaders/skybox.vert.spv", "./resources/shaders/skybox.frag.spv");
}

void GraphicsPipelineManager::destroy()
{
    staticShader->destroyVk();
    skeletonShader->destroyVk();
    wireframeShader->destroyVk();
    stencilShader->destroyVk();
    shadowStaticShader->destroyVk();
    previewMeshShader->destroyVk();
    skyboxHDRShader->destroyVk();
    skyboxShader->destroyVk();

    for (const auto &key : m_pipelines)
        key.second->destroyVk();
}

core::GraphicsPipeline::SharedPtr GraphicsPipelineManager::createPipeline(const GraphicsPipelineKey &key)
{
    std::vector<VkPipelineShaderStageCreateInfo> stages;

    switch (key.shader)
    {
    case ShaderId::StaticMesh:
        stages = staticShader->getShaderStages();
        break;
    case ShaderId::SkinnedMesh:
        stages = skeletonShader->getShaderStages();
        break;
    case ShaderId::Wireframe:
        stages = stencilShader->getShaderStages();
        break;
    case ShaderId::Stencil:
        stages = stencilShader->getShaderStages();
        break;
    case ShaderId::StaticShadow:
        stages = shadowStaticShader->getShaderStages();
        break;
    case ShaderId::PreviewMesh:
        stages = previewMeshShader->getShaderStages();
        break;
    case ShaderId::SkyboxHDR:
        stages = skyboxHDRShader->getShaderStages();
        break;
    case ShaderId::Skybox:
        stages = skyboxShader->getShaderStages();
        break;

    default:
        throw std::runtime_error("Unknown ShaderId");
    }

    // viewport/scissor dynamically during record(). (values can be dummy)
    VkViewport dummyVp{0, 0, 1, 1, 0, 1};
    VkRect2D dummySc{{0, 0}, {1, 1}};
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    if (key.shader == ShaderId::StaticShadow)
    {
        dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
    }

    auto dynamicState = builders::GraphicsPipelineBuilder::dynamic(dynamicStates);
    auto inputAssembly = builders::GraphicsPipelineBuilder::inputAssemblyCI(key.topology);
    auto viewportState = builders::GraphicsPipelineBuilder::viewportCI({dummyVp}, {dummySc});
    auto rasterizer = builders::GraphicsPipelineBuilder::rasterizationCI(key.polygonMode);

    switch (key.cull)
    {
    case CullMode::Back:
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        break;
    case CullMode::Front:
        rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
        break;
    default:
        rasterizer.cullMode = VK_CULL_MODE_NONE;
    }

    auto msaa = builders::GraphicsPipelineBuilder::multisamplingCI(); // 1x currently
    auto depthStencil = builders::GraphicsPipelineBuilder::depthStencilCI(key.depthTest, key.depthWrite, key.depthCompare);

    std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;

    if (key.shader == ShaderId::SkinnedMesh)
    {
        vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::VertexSkinned))};
        vertexAttributeDescriptions = vertex::VertexSkinned::getAttributeDescriptions();
    }
    else if (key.shader == ShaderId::SkyboxHDR || key.shader == ShaderId::Skybox)
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = 3 * sizeof(float);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attributeDescription{};
        attributeDescription.binding = 0;
        attributeDescription.location = 0;
        attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescription.offset = 0;

        vertexBindingDescriptions = {bindingDescription};
        vertexAttributeDescriptions = {attributeDescription};
    }
    else
    {
        vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::Vertex3D))};
        vertexAttributeDescriptions = vertex::Vertex3D::getAttributeDescriptions();
    }

    auto vertexInputState = builders::GraphicsPipelineBuilder::vertexInputCI(vertexBindingDescriptions, vertexAttributeDescriptions);

    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments{
        builders::GraphicsPipelineBuilder::colorBlendAttachmentCI(false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO),
        builders::GraphicsPipelineBuilder::colorBlendAttachmentCI(false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO)};

    colorBlendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;

    if (key.shader == ShaderId::PreviewMesh)
    {
        colorBlendAttachments = {builders::GraphicsPipelineBuilder::colorBlendAttachmentCI(false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO)};
    }

    if (key.shader == ShaderId::StaticShadow)
    {
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 4.0f;
        rasterizer.depthBiasSlopeFactor = 4.0f;

        colorBlendAttachments.clear();
    }

    if (key.shader == ShaderId::SkyboxHDR)
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    auto colorBlending = builders::GraphicsPipelineBuilder::colorBlending(colorBlendAttachments);

    const uint32_t subpass = 0;

    auto pipelineLayout = key.pipelineLayout ? key.pipelineLayout : EngineShaderFamilies::meshShaderFamily.pipelineLayout;

    VkPipelineRenderingCreateInfo pipelineRenderingCI{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    pipelineRenderingCI.colorAttachmentCount = static_cast<uint32_t>(key.colorFormats.size());
    pipelineRenderingCI.pColorAttachmentFormats = key.colorFormats.empty() ? nullptr : key.colorFormats.data();
    pipelineRenderingCI.depthAttachmentFormat = key.depthFormat;
    pipelineRenderingCI.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    auto graphicsPipeline = core::GraphicsPipeline::createShared(
        pipelineRenderingCI,
        stages,
        pipelineLayout,
        dynamicState,
        colorBlending,
        msaa,
        rasterizer,
        viewportState,
        inputAssembly,
        vertexInputState,
        depthStencil,
        subpass,
        cache::GraphicsPipelineCache::getDeviceCache(core::VulkanContext::getContext()->getDevice()));

    return graphicsPipeline;
}

ELIX_NESTED_NAMESPACE_END
