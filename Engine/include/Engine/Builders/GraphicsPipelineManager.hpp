#ifndef ELIX_GRAPHICS_PIPELINE_MANAGER_HPP
#define ELIX_GRAPHICS_PIPELINE_MANAGER_HPP

#include "Core/GraphicsPipeline.hpp"

#include "Engine/Builders/GraphicsPipelineKey.hpp"

#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class GraphicsPipelineManager
{
public:
    static core::GraphicsPipeline::SharedPtr getOrCreate(GraphicsPipelineKey key);

private:
    static core::GraphicsPipeline::SharedPtr createPipeline(const GraphicsPipelineKey &key);

    static inline std::unordered_map<GraphicsPipelineKey, core::GraphicsPipeline::SharedPtr, GraphicsPipelineKeyHash> m_pipelines;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_GRAPHICS_PIPELINE_MANAGER_HPP