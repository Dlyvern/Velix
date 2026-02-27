#ifndef ELIX_RENDER_GRAPH_PROFILING_DATA_HPP
#define ELIX_RENDER_GRAPH_PROFILING_DATA_HPP

#include "Core/Macros.hpp"

#include <cstdint>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

struct RenderGraphPassProfilingData
{
    std::string passName;
    uint32_t executions{0};
    uint32_t drawCalls{0};
    double cpuTimeMs{0.0};
    double gpuTimeMs{0.0};
};

struct RenderGraphFrameProfilingData
{
    uint64_t frameIndex{0};
    uint32_t totalDrawCalls{0};
    double cpuFrameTimeMs{0.0};
    double cpuTotalTimeMs{0.0};
    double cpuWaitForFenceMs{0.0};
    double cpuAcquireImageMs{0.0};
    double cpuRecompileMs{0.0};
    double cpuSubmitMs{0.0};
    double cpuPresentMs{0.0};
    double cpuSyncTimeMs{0.0};
    double cpuWasteTimeMs{0.0};
    double gpuFrameTimeMs{0.0};
    double gpuTotalTimeMs{0.0};
    double gpuWasteTimeMs{0.0};
    bool gpuTimingAvailable{false};
    std::vector<RenderGraphPassProfilingData> passes;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_GRAPH_PROFILING_DATA_HPP
