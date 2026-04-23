#include "Engine/Render/GraphPasses/TerrainRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

TerrainRenderGraphPass::TerrainRenderGraphPass()
{
    setDebugName("Terrain render graph pass");
    m_extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();
}

void TerrainRenderGraphPass::setup(RGPResourcesBuilder & )
{
}

void TerrainRenderGraphPass::compile(RGPResourcesStorage & )
{
}

void TerrainRenderGraphPass::record(core::CommandBuffer::SharedPtr ,
                                    const RenderGraphPassPerFrameData & ,
                                    const RenderGraphPassContext & )
{
}

std::vector<IRenderGraphPass::RenderPassExecution> TerrainRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext & ) const
{
    return {};
}

void TerrainRenderGraphPass::setExtent(VkExtent2D extent)
{
    if (m_extent.width == extent.width && m_extent.height == extent.height)
        return;

    m_extent = extent;
    requestRecompilation();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
