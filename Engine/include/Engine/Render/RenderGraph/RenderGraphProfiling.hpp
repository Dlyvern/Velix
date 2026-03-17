#ifndef ELIX_RENDER_GRAPH_PROFILING_HPP
#define ELIX_RENDER_GRAPH_PROFILING_HPP

#include "Core/Macros.hpp"

#include "Engine/Render/RenderGraph/RenderGraphProfilingData.hpp"

#include "volk.h"

#include <chrono>
#include <string_view>
#include <type_traits>
#include <utility>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class RenderGraphProfiling
{
public:
    RenderGraphProfiling(uint32_t renderGraphPassSize);

    enum class CpuStage
    {
        WaitForFence,
        AcquireImage,
        Recompile,
        Submit,
        Present,
        CommandPoolReset,
        PrimaryCommandBufferEnd,
        ResolveProfiling
    };

    struct PassExecutionProfilingData
    {
        std::string passName;
        uint32_t drawCalls{0};
        double cpuTimeMs{0.0};
        uint32_t startQueryIndex{UINT32_MAX};
        uint32_t endQueryIndex{UINT32_MAX};
    };

    struct FrameQueryRange
    {
        uint32_t startQueryIndex{UINT32_MAX};
        uint32_t endQueryIndex{UINT32_MAX};
    };

    struct FrameCpuStageProfilingData
    {
        double waitForFenceMs{0.0};
        double acquireImageMs{0.0};
        double recompileMs{0.0};
        double submitMs{0.0};
        double presentMs{0.0};
        double commandPoolResetMs{0.0};
        double primaryCbEndMs{0.0};
        double resolveProfilingMs{0.0};
    };

    uint32_t &getTimestampQueriesPerFrame()
    {
        return m_timestampQueriesPerFrame;
    }

    uint32_t &getTimestampQueryBase()
    {
        return m_timestampQueryBase;
    }

    uint32_t &getUsedTimestampQueries()
    {
        return m_usedTimestampQueries;
    }

    void setTimestampQueryBase(uint32_t value)
    {
        m_timestampQueryBase = value;
    }

    void setUsedTimestampQueries(uint32_t value)
    {
        m_usedTimestampQueries = value;
    }

    bool isGPUTimingAvailable() const
    {
        return m_isGpuTimingAvailable;
    }

    VkQueryPool getTimestampQueryPool()
    {
        return m_timestampQueryPool;
    }

    FrameQueryRange &getFrameQueryRangesByFrame(uint32_t frameIndex)
    {
        return m_frameQueryRangesByFrame[frameIndex];
    }

    void resetFrameQueryRangesByFrame(uint32_t frameIndex)
    {
        m_frameQueryRangesByFrame[frameIndex] = FrameQueryRange{};
    }

    FrameCpuStageProfilingData &getCpuStageProfilingDataByFrame(uint32_t frameIndex)
    {
        return m_cpuStageProfilingByFrame[frameIndex];
    }

    void setCpuFrameTimesByFrameMs(uint32_t frameIndex, double value)
    {
        m_cpuFrameTimesByFrameMs[frameIndex] = value;
    }

    void resetCpuStageProfilingDataByFrame(uint32_t frameIndex)
    {
        m_cpuStageProfilingByFrame[frameIndex] = FrameCpuStageProfilingData{};
    }

    void initTimestampQueryPool();
    void destroyTimestampQueryPool();

    void syncDetailedProfilingMode();
    bool isDetailedProfilingEnabled() const;
    void resolveFrameProfilingData(uint32_t frameIndex);
    void beginFrameCpuProfiling(uint32_t frameIndex, double waitForFenceMs);
    void onFenceWaitFailure(uint32_t frameIndex, double waitForFenceMs);
    void recordCpuStage(uint32_t frameIndex, CpuStage stage, double milliseconds);

    template <typename Fn>
    auto measureCpuStage(uint32_t frameIndex, CpuStage stage, Fn &&fn) -> decltype(fn())
    {
        const auto start = std::chrono::high_resolution_clock::now();

        if constexpr (std::is_void_v<decltype(fn())>)
        {
            std::forward<Fn>(fn)();
            const auto end = std::chrono::high_resolution_clock::now();
            recordCpuStage(frameIndex, stage, std::chrono::duration<double, std::milli>(end - start).count());
        }
        else
        {
            auto result = std::forward<Fn>(fn)();
            const auto end = std::chrono::high_resolution_clock::now();
            recordCpuStage(frameIndex, stage, std::chrono::duration<double, std::milli>(end - start).count());
            return result;
        }
    }

    template <typename Fn>
    void measureFrameCpuTime(uint32_t frameIndex, Fn &&fn)
    {
        const auto start = std::chrono::high_resolution_clock::now();
        std::forward<Fn>(fn)();
        const auto end = std::chrono::high_resolution_clock::now();
        setCpuFrameTimesByFrameMs(frameIndex, std::chrono::duration<double, std::milli>(end - start).count());
    }

    void beginFrameGpuProfiling(VkCommandBuffer primaryCommandBuffer, uint32_t frameIndex);
    void beginPassProfiling(VkCommandBuffer primaryCommandBuffer, uint32_t frameIndex,
                            PassExecutionProfilingData &executionProfilingData, std::string_view passName);
    void endPassProfiling(VkCommandBuffer primaryCommandBuffer, uint32_t frameIndex,
                          PassExecutionProfilingData &&executionProfilingData);
    void endFrameGpuProfiling(VkCommandBuffer primaryCommandBuffer, uint32_t frameIndex);
    void markFrameSubmitted(uint32_t frameIndex);

    const RenderGraphFrameProfilingData &getLastFrameProfilingData() const
    {
        return m_lastFrameProfilingData;
    }

    void setUsedTimestampQueriesByFrame(uint32_t frameIndex, uint32_t value)
    {
        m_usedTimestampQueriesByFrame[frameIndex] = value;
    }

    std::vector<PassExecutionProfilingData> &getPassExecutionProfillingDataByFrame(uint32_t frameIndex)
    {
        return m_passExecutionProfilingDataByFrame[frameIndex];
    }

    bool hasPendingProfilingResolveByFrame(uint32_t frameIndex)
    {
        return m_hasPendingProfilingResolve[frameIndex];
    }

    void setPendingProfilingResolveByFrame(uint32_t frameIndex, bool value)
    {
        m_hasPendingProfilingResolve[frameIndex] = value;
    }

private:
    static constexpr uint16_t MAX_FRAMES_IN_FLIGHT = 1;
    static constexpr uint32_t MAX_RENDER_JOBS = 64;

    VkDevice m_device{VK_NULL_HANDLE};

    uint32_t m_renderGraphPassesSize{0};

    VkQueryPool m_timestampQueryPool{VK_NULL_HANDLE};
    uint32_t m_timestampQueryCapacity{0};
    uint32_t m_timestampQueriesPerFrame{0};
    uint32_t m_timestampQueryBase{0};
    uint32_t m_usedTimestampQueries{0};
    std::array<uint32_t, MAX_FRAMES_IN_FLIGHT> m_usedTimestampQueriesByFrame{};
    std::array<FrameQueryRange, MAX_FRAMES_IN_FLIGHT> m_frameQueryRangesByFrame{};
    std::array<double, MAX_FRAMES_IN_FLIGHT> m_cpuFrameTimesByFrameMs{};
    std::array<FrameCpuStageProfilingData, MAX_FRAMES_IN_FLIGHT> m_cpuStageProfilingByFrame{};
    float m_timestampPeriodNs{0.0f};
    bool m_isGpuTimingAvailable{false};
    uint64_t m_profiledFrameIndex{0};
    std::array<std::vector<PassExecutionProfilingData>, MAX_FRAMES_IN_FLIGHT> m_passExecutionProfilingDataByFrame;
    std::array<bool, MAX_FRAMES_IN_FLIGHT> m_hasPendingProfilingResolve{};
    RenderGraphFrameProfilingData m_lastFrameProfilingData;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_GRAPH_PROFILING_HPP
