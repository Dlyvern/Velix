#ifndef ELIX_RENDER_GRAPH_DRAW_PROFILER_HPP
#define ELIX_RENDER_GRAPH_DRAW_PROFILER_HPP

#include "Core/Macros.hpp"
#include "Core/CommandBuffer.hpp"

#include <cstdint>
#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)
ELIX_CUSTOM_NAMESPACE_BEGIN(profiling)

inline thread_local uint32_t *g_currentDrawCounter = nullptr;

inline void beginDrawCallCounting(uint32_t *counter)
{
    g_currentDrawCounter = counter;
}

inline void endDrawCallCounting()
{
    g_currentDrawCounter = nullptr;
}

inline void countDrawCall(uint32_t drawCalls = 1)
{
    if (g_currentDrawCounter)
        *g_currentDrawCounter += drawCalls;
}

class ScopedDrawCallCounter
{
public:
    explicit ScopedDrawCallCounter(uint32_t &counter)
    {
        beginDrawCallCounting(&counter);
    }

    ~ScopedDrawCallCounter()
    {
        endDrawCallCounting();
    }

    ScopedDrawCallCounter(const ScopedDrawCallCounter &) = delete;
    ScopedDrawCallCounter &operator=(const ScopedDrawCallCounter &) = delete;
};

inline void cmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
    vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
    countDrawCall();
}

inline void cmdDraw(core::CommandBuffer::SharedPtr commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
    cmdDraw(commandBuffer->vk(), vertexCount, instanceCount, firstVertex, firstInstance);
}

inline void cmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    countDrawCall();
}

inline void cmdDrawIndexed(core::CommandBuffer::SharedPtr commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    cmdDrawIndexed(commandBuffer->vk(), indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_GRAPH_DRAW_PROFILER_HPP
