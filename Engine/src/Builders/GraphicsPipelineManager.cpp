#include "Engine/Builders/GraphicsPipelineManager.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/Logger.hpp"

#include "Engine/Builders/GraphicsPipelineBuilder.hpp"
#include "Engine/Caches/GraphicsPipelineCache.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include "Engine/Vertex.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

core::GraphicsPipeline::SharedPtr GraphicsPipelineManager::getOrCreate(const GraphicsPipelineKey &key)
{
    {
        std::shared_lock lock(m_pipelinesMutex);
        auto it = m_pipelines.find(key);

        if (it != m_pipelines.end())
            return it->second;
    }

    std::unique_lock lock(m_pipelinesMutex);
    auto it = m_pipelines.find(key);
    if (it != m_pipelines.end())
        return it->second;

    auto created = createPipeline(key);
    m_pipelines[key] = created;

    VX_ENGINE_DEBUG_STREAM("Created graphics pipeline (shader="
                           << static_cast<uint32_t>(key.shader)
                           << ", blend=" << static_cast<uint32_t>(key.blend)
                           << ", cull=" << static_cast<uint32_t>(key.cull)
                           << ", pipelines_cached=" << m_pipelines.size() << ")");

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
    lightingRayQueryShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/lighting_rt.frag.spv");

    fxaaShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/fxaa.frag.spv");
    bloomExtractShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/bloom_extract.frag.spv");
    bloomCompositeShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/bloom_composite.frag.spv");
    ssaoShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/ssao.frag.spv");
    smaaShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/smaa.frag.spv");
    contactShadowShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/contact_shadow.frag.spv");
    cinematicEffectsShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/cinematic.frag.spv");
    editorBillboardShader = std::make_shared<core::Shader>("./resources/shaders/editor_billboard.vert.spv", "./resources/shaders/editor_billboard.frag.spv");
    billboardShader = std::make_shared<core::Shader>("./resources/shaders/billboard.vert.spv", "./resources/shaders/billboard.frag.spv");
    uiTextShader = std::make_shared<core::Shader>("./resources/shaders/ui_text.vert.spv", "./resources/shaders/ui_text.frag.spv");
    uiQuadShader = std::make_shared<core::Shader>("./resources/shaders/ui_quad.vert.spv", "./resources/shaders/ui_quad.frag.spv");
    particleShader = std::make_shared<core::Shader>("./resources/shaders/particle.vert.spv", "./resources/shaders/particle.frag.spv");
    glassShader = std::make_shared<core::Shader>("./resources/shaders/glass_mesh.vert.spv", "./resources/shaders/glass.frag.spv");
    rtReflectionsShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/rt_reflections.frag.spv");
    rtaoShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/rt_ao.frag.spv");
    depthPrepassStaticShader  = std::make_shared<core::Shader>("./resources/shaders/gbuffer_static.vert.spv",  "./resources/shaders/empty.frag.spv");
    depthPrepassSkinnedShader = std::make_shared<core::Shader>("./resources/shaders/gbuffer_skinned.vert.spv", "./resources/shaders/empty.frag.spv");
    taaShader = std::make_shared<core::Shader>("./resources/shaders/fullscreen.vert.spv", "./resources/shaders/taa.frag.spv");
    animPreviewShader = std::make_shared<core::Shader>("./resources/shaders/anim_preview.vert.spv",
                                                        "./resources/shaders/shader_simple_textured_mesh.frag.spv");
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
    destroyShader(lightingRayQueryShader);
    destroyShader(fxaaShader);
    destroyShader(bloomExtractShader);
    destroyShader(bloomCompositeShader);
    destroyShader(ssaoShader);
    destroyShader(smaaShader);
    destroyShader(contactShadowShader);
    destroyShader(cinematicEffectsShader);
    destroyShader(editorBillboardShader);
    destroyShader(billboardShader);
    destroyShader(uiTextShader);
    destroyShader(uiQuadShader);
    destroyShader(particleShader);
    destroyShader(glassShader);
    destroyShader(rtReflectionsShader);
    destroyShader(rtaoShader);
    destroyShader(depthPrepassStaticShader);
    destroyShader(depthPrepassSkinnedShader);
    destroyShader(taaShader);
    destroyShader(animPreviewShader);
}

void GraphicsPipelineManager::destroyPipelines()
{
    std::unique_lock lock(m_pipelinesMutex);
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

    core::Shader::SharedPtr tempCustomShader{nullptr};

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
    {
        if (!key.customFragSpvPath.empty())
        {
            tempCustomShader = core::Shader::create("./resources/shaders/gbuffer_static.vert.spv", key.customFragSpvPath);
            if (tempCustomShader)
                stages = tempCustomShader->getShaderStages();
            else
                stages = gBufferStaticShader->getShaderStages();
        }
        else
            stages = gBufferStaticShader->getShaderStages();
        break;
    }
    case ShaderId::GBufferSkinned:
        stages = gBufferSkinnedShader->getShaderStages();
        break;
    case ShaderId::Lighting:
        stages = lightingShader->getShaderStages();
        break;
    case ShaderId::LightingRayQuery:
        stages = lightingRayQueryShader->getShaderStages();
        break;
    case ShaderId::FXAA:
        stages = fxaaShader->getShaderStages();
        break;
    case ShaderId::BloomExtract:
        stages = bloomExtractShader->getShaderStages();
        break;
    case ShaderId::BloomComposite:
        stages = bloomCompositeShader->getShaderStages();
        break;
    case ShaderId::SSAO:
        stages = ssaoShader->getShaderStages();
        break;
    case ShaderId::SMAA:
        stages = smaaShader->getShaderStages();
        break;
    case ShaderId::ContactShadow:
        stages = contactShadowShader->getShaderStages();
        break;
    case ShaderId::CinematicEffects:
        stages = cinematicEffectsShader->getShaderStages();
        break;
    case ShaderId::EditorBillboard:
        stages = editorBillboardShader->getShaderStages();
        break;
    case ShaderId::Billboard:
        stages = billboardShader->getShaderStages();
        break;
    case ShaderId::UIText:
        stages = uiTextShader->getShaderStages();
        break;
    case ShaderId::UIQuad:
        stages = uiQuadShader->getShaderStages();
        break;
    case ShaderId::Particle:
        stages = particleShader->getShaderStages();
        break;
    case ShaderId::Glass:
        stages = glassShader->getShaderStages();
        break;
    case ShaderId::RTReflections:
        stages = rtReflectionsShader->getShaderStages();
        break;
    case ShaderId::RTAO:
        stages = rtaoShader->getShaderStages();
        break;
    case ShaderId::DepthPrepassStatic:
        stages = depthPrepassStaticShader->getShaderStages();
        break;
    case ShaderId::DepthPrepassSkinned:
        stages = depthPrepassSkinnedShader->getShaderStages();
        break;
    case ShaderId::TAA:
        stages = taaShader->getShaderStages();
        break;
    case ShaderId::AnimPreview:
        stages = animPreviewShader->getShaderStages();
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
    rasterizer.depthClampEnable = key.depthClampEnable ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasEnable = key.depthBiasEnable ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = key.depthBiasConstantFactor;
    rasterizer.depthBiasSlopeFactor = key.depthBiasSlopeFactor;
    rasterizer.depthBiasClamp = key.depthBiasClamp;

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

    auto msaa = builders::GraphicsPipelineBuilder::multisamplingCI(key.rasterizationSamples);
    auto depthStencil = builders::GraphicsPipelineBuilder::depthStencilCI(key.depthTest, key.depthWrite, key.depthCompare);

    std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;

    if (key.shader == ShaderId::GBufferSkinned || key.shader == ShaderId::SkinnedShadow ||
        key.shader == ShaderId::DepthPrepassSkinned || key.shader == ShaderId::AnimPreview)
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
    else if (key.shader == ShaderId::ToneMap || key.shader == ShaderId::SelectionOverlay ||
             key.shader == ShaderId::Present || key.shader == ShaderId::Lighting ||
             key.shader == ShaderId::LightingRayQuery ||
             key.shader == ShaderId::FXAA || key.shader == ShaderId::BloomExtract ||
             key.shader == ShaderId::BloomComposite ||
             key.shader == ShaderId::SSAO || key.shader == ShaderId::SMAA ||
             key.shader == ShaderId::TAA ||
             key.shader == ShaderId::ContactShadow || key.shader == ShaderId::CinematicEffects ||
             key.shader == ShaderId::EditorBillboard || key.shader == ShaderId::Billboard ||
             key.shader == ShaderId::Particle || key.shader == ShaderId::RTReflections ||
             key.shader == ShaderId::RTAO)
    {
        // Fullscreen / billboard passes generate vertices procedurally in the vertex shader
        vertexBindingDescriptions = {};
        vertexAttributeDescriptions = {};
    }
    else if (key.shader == ShaderId::UIText || key.shader == ShaderId::UIQuad)
    {
        // UI passes use Vertex2D (vec3 pos + vec2 uv)
        vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::Vertex2D))};
        vertexAttributeDescriptions = vertex::Vertex2D::getAttributeDescriptions();
    }
    else if (key.shader == ShaderId::PreviewMesh)
    {
        vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::Vertex3D))};
        vertexAttributeDescriptions = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex::Vertex3D, position)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex::Vertex3D, textureCoordinates)}};
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
    {
        auto attachment = builders::GraphicsPipelineBuilder::colorBlendAttachmentCI(false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
        if (key.blend == BlendMode::AlphaBlend)
        {
            attachment.blendEnable = VK_TRUE;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.colorBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        colorBlendAttachments.push_back(attachment);
    }

    if (key.shader == ShaderId::GBufferStatic || key.shader == ShaderId::GBufferSkinned)
    {
        if (colorBlendAttachments.size() >= 5)
            colorBlendAttachments.back().colorWriteMask = VK_COLOR_COMPONENT_R_BIT;

        if (key.gbufferOutputMode == GBufferOutputMode::ObjectOnly && colorBlendAttachments.size() >= 5)
        {
            for (size_t attachmentIndex = 0; attachmentIndex + 1 < colorBlendAttachments.size(); ++attachmentIndex)
                colorBlendAttachments[attachmentIndex].colorWriteMask = 0;
        }
    }

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
