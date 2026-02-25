#include "Engine/Builders/GraphicsPipelineManager.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Builders/GraphicsPipelineBuilder.hpp"
#include "Engine/Caches/GraphicsPipelineCache.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include "Engine/Vertex.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

core::GraphicsPipeline::SharedPtr GraphicsPipelineManager::getOrCreate(GraphicsPipelineKey key)
{
    auto it = m_pipelines.find(key);

    if (it != m_pipelines.end())
        return it->second;

    VX_ENGINE_INFO_STREAM("Created a new key\n");

    auto created = createPipeline(key);

    m_pipelines[key] = created;

    return created;
}

void GraphicsPipelineManager::init()
{
    destroyPipelines();
    destroyShaderModules();
    loadShaderModules();
}

void GraphicsPipelineManager::reloadShaders()
{
    destroyPipelines();
    destroyShaderModules();
    loadShaderModules();

    VX_ENGINE_INFO_STREAM("Shader modules and graphics pipelines were reloaded");
}

void GraphicsPipelineManager::destroy()
{
    destroyPipelines();
    destroyShaderModules();
}

void GraphicsPipelineManager::loadShaderModules()
{
    shadowStaticShader = std::make_shared<core::Shader>("./resources/shaders/static_mesh_shadow.vert.spv",
                                                        "./resources/shaders/empty.frag.spv");
    shadowSkinnedShader = std::make_shared<core::Shader>("./resources/shaders/skinned_mesh_shadow.vert.spv",
                                                         "./resources/shaders/empty.frag.spv");

    previewMeshShader = std::make_shared<core::Shader>("./resources/shaders/shader_simple_textured_mesh.vert.spv",
                                                       "./resources/shaders/shader_simple_textured_mesh.frag.spv");

    skyboxHDRShader = std::make_shared<core::Shader>("./resources/shaders/skybox.vert.spv", "./resources/shaders/skybox_hdr.frag.spv");
    skyboxShader = std::make_shared<core::Shader>("./resources/shaders/skybox.vert.spv", "./resources/shaders/skybox.frag.spv");
    skyLightShader = std::make_shared<core::Shader>("./resources/shaders/dynamic_sky_light.vert.spv", "./resources/shaders/dynamic_sky_light.frag.spv");
    toneMapShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/tonemap.frag.spv");
    selectionOverlayShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/selection_overlay.frag.spv");

    presentShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/present.frag.spv");

    gBufferStaticShader = std::make_shared<core::Shader>("./resources/shaders/gbuffer_static.vert.spv", "./resources/shaders/gbuffer_static.frag.spv");
    gBufferSkinnedShader = std::make_shared<core::Shader>("./resources/shaders/gbuffer_skinned.vert.spv", "./resources/shaders/gbuffer_static.frag.spv");

    lightingShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/lighting.frag.spv");
}

void GraphicsPipelineManager::destroyShaderModules()
{
    auto destroyShader = [](core::Shader::SharedPtr &shader)
    {
        if (shader)
        {
            shader->destroyVk();
            shader.reset();
        }
    };

    destroyShader(shadowStaticShader);
    destroyShader(shadowSkinnedShader);
    destroyShader(previewMeshShader);
    destroyShader(skyboxHDRShader);
    destroyShader(skyboxShader);
    destroyShader(skyLightShader);
    destroyShader(toneMapShader);
    destroyShader(selectionOverlayShader);
    destroyShader(presentShader);
    destroyShader(gBufferStaticShader);
    destroyShader(gBufferSkinnedShader);
    destroyShader(lightingShader);
}

void GraphicsPipelineManager::destroyPipelines()
{
    for (auto &[_, pipeline] : m_pipelines)
    {
        if (pipeline)
            pipeline->destroyVk();
    }

    m_pipelines.clear();
}

core::GraphicsPipeline::SharedPtr GraphicsPipelineManager::createPipeline(const GraphicsPipelineKey &key)
{
    std::vector<VkPipelineShaderStageCreateInfo> stages;

    switch (key.shader)
    {
    case ShaderId::StaticShadow:
        stages = shadowStaticShader->getShaderStages();
        break;
    case ShaderId::SkinnedShadow:
        stages = shadowSkinnedShader->getShaderStages();
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
    case ShaderId::SkyLight:
        stages = skyLightShader->getShaderStages();
        break;
    case ShaderId::ToneMap:
        stages = toneMapShader->getShaderStages();
        break;
    case ShaderId::SelectionOverlay:
        stages = selectionOverlayShader->getShaderStages();
        break;
    case ShaderId::Present:
        stages = presentShader->getShaderStages();
        break;
    case ShaderId::GBufferStatic:
        stages = gBufferStaticShader->getShaderStages();
        break;
    case ShaderId::GBufferSkinned:
        stages = gBufferSkinnedShader->getShaderStages();
        break;
    case ShaderId::Lighting:
        stages = lightingShader->getShaderStages();
        break;
    default:
        throw std::runtime_error("Unknown ShaderId");
    }

    // viewport/scissor dynamically during record(). (values can be dummy)
    VkViewport dummyVp{0, 0, 1, 1, 0, 1};
    VkRect2D dummySc{{0, 0}, {1, 1}};
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    if (key.shader == ShaderId::StaticShadow || key.shader == ShaderId::SkinnedShadow)
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

    if (key.shader == ShaderId::GBufferSkinned || key.shader == ShaderId::SkinnedShadow)
    {
        vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::VertexSkinned))};
        vertexAttributeDescriptions = vertex::VertexSkinned::getAttributeDescriptions();
    }
    else if (key.shader == ShaderId::SkyboxHDR || key.shader == ShaderId::Skybox || key.shader == ShaderId::SkyLight)
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
    else if (key.shader == ShaderId::ToneMap || key.shader == ShaderId::SelectionOverlay || key.shader == ShaderId::Present || key.shader == ShaderId::Lighting)
    {
        vertexBindingDescriptions = {};
        vertexAttributeDescriptions = {};
    }
    else if (key.shader == ShaderId::PreviewMesh)
    {
        vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::Vertex3D))};
        vertexAttributeDescriptions = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex::Vertex3D, position)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex::Vertex3D, textureCoordinates)},
            {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex::Vertex3D, normal)}};
    }
    else
    {
        vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::Vertex3D))};
        vertexAttributeDescriptions = vertex::Vertex3D::getAttributeDescriptions();
    }

    auto vertexInputState = builders::GraphicsPipelineBuilder::vertexInputCI(vertexBindingDescriptions, vertexAttributeDescriptions);

    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
    colorBlendAttachments.reserve(key.colorFormats.size());

    for (size_t i = 0; i < key.colorFormats.size(); ++i)
        colorBlendAttachments.push_back(builders::GraphicsPipelineBuilder::colorBlendAttachmentCI(false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO));

    if (key.shader == ShaderId::GBufferStatic || key.shader == ShaderId::GBufferSkinned)
        colorBlendAttachments[3].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;

    if (key.shader == ShaderId::StaticShadow || key.shader == ShaderId::SkinnedShadow)
    {
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 4.0f;
        rasterizer.depthBiasSlopeFactor = 4.0f;
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
