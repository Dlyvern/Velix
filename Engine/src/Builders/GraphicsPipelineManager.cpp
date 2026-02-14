#include "Engine/Builders/GraphicsPipelineManager.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/Shader.hpp"

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

core::GraphicsPipeline::SharedPtr GraphicsPipelineManager::createPipeline(const GraphicsPipelineKey &key)
{
    std::vector<VkPipelineShaderStageCreateInfo> stages;

    core::Shader staticShader("./resources/shaders/static_mesh.vert.spv", "./resources/shaders/static_mesh.frag.spv");
    core::Shader skeletonShader("./resources/shaders/skeleton_mesh.vert.spv", "./resources/shaders/static_mesh.frag.spv");
    core::Shader wireframeShader("./resources/shaders/wireframe_mesh.vert.spv", "./resources/shaders/debug_red.frag.spv");
    core::Shader stencilShader("./resources/shaders/wireframe_mesh.vert.spv", "./resources/shaders/debug_yellow.frag.spv");
    core::Shader shadowStaticShader("./resources/shaders/static_mesh_shadow.vert.spv",
                                    "./resources/shaders/empty.frag.spv");

    core::Shader previewMeshShader("./resources/shaders/shader_simple_textured_mesh.vert.spv",
                                   "./resources/shaders/shader_simple_textured_mesh.frag.spv");

    switch (key.shader)
    {
    case ShaderId::StaticMesh:
        stages = staticShader.getShaderStages();
        break;
    case ShaderId::SkinnedMesh:
        stages = skeletonShader.getShaderStages();
        break;
    case ShaderId::Wireframe:
        stages = stencilShader.getShaderStages();
        break;
    case ShaderId::Stencil:
        stages = stencilShader.getShaderStages();
        break;
    case ShaderId::StaticShadow:
        stages = shadowStaticShader.getShaderStages();
        break;
    case ShaderId::PreviewMesh:
        stages = previewMeshShader.getShaderStages();
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
    rasterizer.cullMode = (key.cull == CullMode::Back) ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    auto msaa = builders::GraphicsPipelineBuilder::multisamplingCI(); // 1x currently
    auto depthStencil = builders::GraphicsPipelineBuilder::depthStencilCI(key.depthTest, key.depthWrite, key.depthCompare);

    std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;

    if (key.shader == ShaderId::SkinnedMesh)
    {
        vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::VertexSkinned))};
        vertexAttributeDescriptions = vertex::VertexSkinned::getAttributeDescriptions();
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

    auto colorBlending = builders::GraphicsPipelineBuilder::colorBlending(colorBlendAttachments);

    const uint32_t subpass = 0;

    auto pipelineLayout = key.pipelineLayout ? key.pipelineLayout : EngineShaderFamilies::meshShaderFamily.pipelineLayout;

    return core::GraphicsPipeline::createShared(
        core::VulkanContext::getContext()->getDevice(),
        key.renderPass->vk(),
        stages,
        pipelineLayout,
        dynamicState,
        colorBlending,
        msaa,
        rasterizer,
        viewportState,
        inputAssembly,
        vertexInputState,
        subpass,
        depthStencil,
        cache::GraphicsPipelineCache::getDeviceCache(core::VulkanContext::getContext()->getDevice()));
}

ELIX_NESTED_NAMESPACE_END
