#ifndef ELIX_GRAPHICS_PIPELINE_RENDER_GRAPH_PROXY_HPP 
#define ELIX_GRAPHICS_PIPELINE_RENDER_GRAPH_PROXY_HPP

#include "Core/Macros.hpp"

#include "Engine/Render/Proxies/IRenderGraphProxy.hpp"
#include "Engine/GraphicsPipelineBuilder.hpp"

#include "Core/GraphicsPipeline.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class GraphicsPipelineRenderGraphProxy : public IRenderGraphProxy<core::GraphicsPipeline, RenderGraphProxySinglePtrData>
{
public:
    GraphicsPipelineBuilder builder;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_GRAPHICS_PIPELINE_RENDER_GRAPH_PROXY_HPP