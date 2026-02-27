#include "Engine/Render/RenderGraph/RenderGraph.hpp"

#include "Core/VulkanContext.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

void RenderGraph::initTimestampQueryPool()
{
    destroyTimestampQueryPool();

    auto context = core::VulkanContext::getContext();
    if (!context)
        return;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context->getPhysicalDevice(), &properties);
    m_timestampPeriodNs = properties.limits.timestampPeriod;

    uint32_t queueFamiliesCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(context->getPhysicalDevice(), &queueFamiliesCount, nullptr);

    if (queueFamiliesCount == 0)
        return;

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamiliesCount);
    vkGetPhysicalDeviceQueueFamilyProperties(context->getPhysicalDevice(), &queueFamiliesCount, queueFamilies.data());

    const uint32_t graphicsFamily = context->getGraphicsFamily();

    if (graphicsFamily >= queueFamiliesCount || queueFamilies[graphicsFamily].timestampValidBits == 0)
        return;

    const uint32_t passCount = std::max<uint32_t>(1u, static_cast<uint32_t>(m_renderGraphPasses.size()));
    const uint32_t maxExecutions = MAX_RENDER_JOBS + passCount + 16u;
    // Two timestamps per pass execution, plus frame start/end timestamps.
    m_timestampQueriesPerFrame = maxExecutions * 2u + 2u;
    m_timestampQueryCapacity = m_timestampQueriesPerFrame * MAX_FRAMES_IN_FLIGHT;

    VkQueryPoolCreateInfo queryPoolCI{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    queryPoolCI.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolCI.queryCount = m_timestampQueryCapacity;

    if (vkCreateQueryPool(m_device, &queryPoolCI, nullptr, &m_timestampQueryPool) == VK_SUCCESS)
    {
        m_isGpuTimingAvailable = true;
    }
    else
    {
        m_timestampQueryPool = VK_NULL_HANDLE;
        m_timestampQueryCapacity = 0;
        m_timestampQueriesPerFrame = 0;
        m_isGpuTimingAvailable = false;
        VX_ENGINE_ERROR_STREAM("Failed to create render graph timestamp query pool\n");
    }
}

void RenderGraph::destroyTimestampQueryPool()
{
    if (m_timestampQueryPool != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(m_device, m_timestampQueryPool, nullptr);
        m_timestampQueryPool = VK_NULL_HANDLE;
    }

    m_timestampQueryCapacity = 0;
    m_timestampQueriesPerFrame = 0;
    m_timestampQueryBase = 0;
    m_usedTimestampQueries = 0;
    m_usedTimestampQueriesByFrame.fill(0);
    m_frameQueryRangesByFrame.fill(FrameQueryRange{});
    m_cpuFrameTimesByFrameMs.fill(0.0);
    m_cpuStageProfilingByFrame.fill(FrameCpuStageProfilingData{});
    m_hasPendingProfilingResolve.fill(false);
    m_isGpuTimingAvailable = false;
}

void RenderGraph::resolveFrameProfilingData(uint32_t frameIndex)
{
    const auto &passExecutionProfilingData = m_passExecutionProfilingDataByFrame[frameIndex];
    const uint32_t usedTimestampQueries = m_usedTimestampQueriesByFrame[frameIndex];
    const uint32_t frameQueryBase = frameIndex * m_timestampQueriesPerFrame;

    RenderGraphFrameProfilingData frameProfilingData{};
    frameProfilingData.frameIndex = ++m_profiledFrameIndex;
    frameProfilingData.totalDrawCalls = 0;
    frameProfilingData.cpuFrameTimeMs = m_cpuFrameTimesByFrameMs[frameIndex];
    frameProfilingData.cpuTotalTimeMs = 0.0;
    frameProfilingData.cpuWaitForFenceMs = 0.0;
    frameProfilingData.cpuAcquireImageMs = 0.0;
    frameProfilingData.cpuRecompileMs = 0.0;
    frameProfilingData.cpuSubmitMs = 0.0;
    frameProfilingData.cpuPresentMs = 0.0;
    frameProfilingData.cpuSyncTimeMs = 0.0;
    frameProfilingData.cpuWasteTimeMs = 0.0;
    frameProfilingData.gpuFrameTimeMs = 0.0;
    frameProfilingData.gpuTotalTimeMs = 0.0;
    frameProfilingData.gpuWasteTimeMs = 0.0;
    frameProfilingData.gpuTimingAvailable = false;
    frameProfilingData.passes.reserve(passExecutionProfilingData.size());

    std::vector<uint64_t> timestampData;
    bool hasGpuData = false;

    if (m_isGpuTimingAvailable && m_timestampQueryPool != VK_NULL_HANDLE && usedTimestampQueries > 0)
    {
        timestampData.resize(usedTimestampQueries);

        const VkResult result = vkGetQueryPoolResults(
            m_device,
            m_timestampQueryPool,
            frameQueryBase,
            usedTimestampQueries,
            timestampData.size() * sizeof(uint64_t),
            timestampData.data(),
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT);

        hasGpuData = result == VK_SUCCESS;

        if (!hasGpuData)
            VX_ENGINE_ERROR_STREAM("Failed to collect timestamp query results for render graph profiling\n");
    }

    frameProfilingData.gpuTimingAvailable = hasGpuData;

    const auto &frameRange = m_frameQueryRangesByFrame[frameIndex];
    if (hasGpuData &&
        frameRange.startQueryIndex >= frameQueryBase &&
        frameRange.endQueryIndex >= frameQueryBase &&
        frameRange.startQueryIndex < frameQueryBase + usedTimestampQueries &&
        frameRange.endQueryIndex < frameQueryBase + usedTimestampQueries)
    {
        const uint64_t start = timestampData[frameRange.startQueryIndex - frameQueryBase];
        const uint64_t end = timestampData[frameRange.endQueryIndex - frameQueryBase];

        if (end >= start)
            frameProfilingData.gpuFrameTimeMs = static_cast<double>(end - start) * static_cast<double>(m_timestampPeriodNs) * 1e-6;
    }

    std::unordered_map<std::string, size_t> passIndexByName;
    passIndexByName.reserve(passExecutionProfilingData.size());

    for (const auto &executionData : passExecutionProfilingData)
    {
        const std::string passName = executionData.passName.empty() ? "Unnamed pass" : executionData.passName;

        size_t passIndex = 0;
        auto passIt = passIndexByName.find(passName);

        if (passIt == passIndexByName.end())
        {
            passIndex = frameProfilingData.passes.size();
            passIndexByName.emplace(passName, passIndex);
            frameProfilingData.passes.push_back(RenderGraphPassProfilingData{.passName = passName});
        }
        else
            passIndex = passIt->second;

        auto &passProfilingData = frameProfilingData.passes[passIndex];
        passProfilingData.executions++;
        passProfilingData.drawCalls += executionData.drawCalls;
        passProfilingData.cpuTimeMs += executionData.cpuTimeMs;

        frameProfilingData.totalDrawCalls += executionData.drawCalls;
        frameProfilingData.cpuTotalTimeMs += executionData.cpuTimeMs;

        if (hasGpuData &&
            executionData.startQueryIndex >= frameQueryBase &&
            executionData.endQueryIndex >= frameQueryBase &&
            executionData.startQueryIndex < frameQueryBase + usedTimestampQueries &&
            executionData.endQueryIndex < frameQueryBase + usedTimestampQueries)
        {
            const uint64_t start = timestampData[executionData.startQueryIndex - frameQueryBase];
            const uint64_t end = timestampData[executionData.endQueryIndex - frameQueryBase];

            if (end >= start)
            {
                const double gpuTimeMs = static_cast<double>(end - start) * static_cast<double>(m_timestampPeriodNs) * 1e-6;
                passProfilingData.gpuTimeMs += gpuTimeMs;
                frameProfilingData.gpuTotalTimeMs += gpuTimeMs;
            }
        }
    }

    const auto &cpuStageProfilingData = m_cpuStageProfilingByFrame[frameIndex];
    frameProfilingData.cpuWaitForFenceMs = cpuStageProfilingData.waitForFenceMs;
    frameProfilingData.cpuAcquireImageMs = cpuStageProfilingData.acquireImageMs;
    frameProfilingData.cpuRecompileMs = cpuStageProfilingData.recompileMs;
    frameProfilingData.cpuSubmitMs = cpuStageProfilingData.submitMs;
    frameProfilingData.cpuPresentMs = cpuStageProfilingData.presentMs;
    frameProfilingData.cpuSyncTimeMs = frameProfilingData.cpuWaitForFenceMs +
                                       frameProfilingData.cpuAcquireImageMs +
                                       frameProfilingData.cpuSubmitMs +
                                       frameProfilingData.cpuPresentMs;

    const double accountedCpuFrameTime = frameProfilingData.cpuTotalTimeMs +
                                         frameProfilingData.cpuRecompileMs +
                                         frameProfilingData.cpuSyncTimeMs;

    frameProfilingData.cpuWasteTimeMs = std::max(0.0, frameProfilingData.cpuFrameTimeMs - accountedCpuFrameTime);
    if (frameProfilingData.gpuTimingAvailable)
        frameProfilingData.gpuWasteTimeMs = std::max(0.0, frameProfilingData.gpuFrameTimeMs - frameProfilingData.gpuTotalTimeMs);

    m_lastFrameProfilingData = std::move(frameProfilingData);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
