#include "Engine/Render/GraphPasses/TerrainRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

TerrainRenderGraphPass::TerrainRenderGraphPass()
{
    setDebugName("Terrain render graph pass");
    m_extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();
}

void TerrainRenderGraphPass::setup(RGPResourcesBuilder & /*builder*/)
{
}

void TerrainRenderGraphPass::compile(RGPResourcesStorage & /*storage*/)
{
}

void TerrainRenderGraphPass::record(core::CommandBuffer::SharedPtr /*commandBuffer*/,
                                    const RenderGraphPassPerFrameData & /*data*/,
                                    const RenderGraphPassContext & /*renderContext*/)
{
}

std::vector<IRenderGraphPass::RenderPassExecution> TerrainRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext & /*renderContext*/) const
{
    return {};
}

void TerrainRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    requestRecompilation();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
