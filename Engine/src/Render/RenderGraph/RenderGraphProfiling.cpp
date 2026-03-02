#include "Engine/Render/RenderGraph/RenderGraph.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Runtime/EngineConfig.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

bool RenderGraph::isDetailedProfilingEnabled() const
{
    return EngineConfig::instance().getDetailedRenderProfilingEnabled();
}

void RenderGraph::syncDetailedProfilingMode()
{
    if (isDetailedProfilingEnabled())
    {
        if (m_timestampQueryPool == VK_NULL_HANDLE)
            initTimestampQueryPool();
    }
    else if (m_timestampQueryPool != VK_NULL_HANDLE || m_isGpuTimingAvailable)
    {
        vkDeviceWaitIdle(m_device);
        destroyTimestampQueryPool();
        for (auto &framePassProfilingData : m_passExecutionProfilingDataByFrame)
            framePassProfilingData.clear();
        m_lastFrameProfilingData = {};
    }
}

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

void RenderGraph::initOcclusionQueryPool()
{
    destroyOcclusionQueryPool();

    if (m_device == VK_NULL_HANDLE)
        return;

    m_occlusionQueriesPerFrame = OCCLUSION_QUERIES_PER_FRAME;
    const uint32_t queryCapacity = m_occlusionQueriesPerFrame * MAX_FRAMES_IN_FLIGHT;

    VkQueryPoolCreateInfo queryPoolCI{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    queryPoolCI.queryType = VK_QUERY_TYPE_OCCLUSION;
    queryPoolCI.queryCount = queryCapacity;

    if (vkCreateQueryPool(m_device, &queryPoolCI, nullptr, &m_occlusionQueryPool) != VK_SUCCESS)
    {
        m_occlusionQueryPool = VK_NULL_HANDLE;
        m_occlusionQueriesPerFrame = 0u;
        VX_ENGINE_ERROR_STREAM("Failed to create render graph occlusion query pool\n");
        return;
    }

    const VkDeviceSize readbackSize = static_cast<VkDeviceSize>(m_occlusionQueriesPerFrame) * sizeof(OcclusionQueryReadback);
    for (uint32_t frameIndex = 0u; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
    {
        m_occlusionReadbackBuffers[frameIndex] = core::Buffer::createShared(
            readbackSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU);

        void *mapped = nullptr;
        m_occlusionReadbackBuffers[frameIndex]->map(mapped);
        m_occlusionReadbackMapped[frameIndex] = mapped;
        std::memset(mapped, 0, static_cast<size_t>(readbackSize));

        m_submittedOcclusionQueryKeys[frameIndex].clear();
        m_submittedOcclusionQueryCounts[frameIndex] = 0u;
        m_submittedOcclusionFrameNumbers[frameIndex] = 0u;
        m_hasPendingOcclusionResolve[frameIndex] = false;
    }
}

void RenderGraph::destroyOcclusionQueryPool()
{
    for (uint32_t frameIndex = 0u; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
    {
        if (m_occlusionReadbackBuffers[frameIndex] && m_occlusionReadbackMapped[frameIndex])
            m_occlusionReadbackBuffers[frameIndex]->unmap();

        m_occlusionReadbackMapped[frameIndex] = nullptr;
        m_occlusionReadbackBuffers[frameIndex].reset();
        m_submittedOcclusionQueryKeys[frameIndex].clear();
        m_submittedOcclusionQueryCounts[frameIndex] = 0u;
        m_submittedOcclusionFrameNumbers[frameIndex] = 0u;
        m_hasPendingOcclusionResolve[frameIndex] = false;
    }

    if (m_occlusionQueryPool != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(m_device, m_occlusionQueryPool, nullptr);
        m_occlusionQueryPool = VK_NULL_HANDLE;
    }

    m_occlusionQueriesPerFrame = 0u;
    m_usedOcclusionQueriesByFrame.fill(0u);
    m_occlusionFrameNumbersByFrame.fill(0u);
    for (auto &frameKeys : m_occlusionQueryKeysByFrame)
        frameKeys.clear();
}

void RenderGraph::resolveOcclusionQueries(uint32_t frameIndex)
{
    if (m_occlusionQueryPool == VK_NULL_HANDLE || m_occlusionQueriesPerFrame == 0u)
        return;

    if (!m_hasPendingOcclusionResolve[frameIndex])
        return;

    uint32_t queryCount = m_submittedOcclusionQueryCounts[frameIndex];
    if (queryCount == 0u)
    {
        m_hasPendingOcclusionResolve[frameIndex] = false;
        return;
    }

    const auto &queryKeys = m_submittedOcclusionQueryKeys[frameIndex];
    if (queryKeys.empty())
    {
        m_hasPendingOcclusionResolve[frameIndex] = false;
        return;
    }

    queryCount = std::min(queryCount, static_cast<uint32_t>(queryKeys.size()));
    if (queryCount == 0u)
    {
        m_hasPendingOcclusionResolve[frameIndex] = false;
        return;
    }

    const auto *queryResults = reinterpret_cast<const OcclusionQueryReadback *>(m_occlusionReadbackMapped[frameIndex]);
    if (!queryResults)
    {
        m_hasPendingOcclusionResolve[frameIndex] = false;
        return;
    }

    const uint64_t submittedFrameNumber = m_submittedOcclusionFrameNumbers[frameIndex];
    const uint8_t occlusionConfirmationQueries = static_cast<uint8_t>(
        std::clamp(RenderQualitySettings::getInstance().occlusionOccludedConfirmationQueries, 1, 8));
    for (uint32_t queryIndex = 0u; queryIndex < queryCount; ++queryIndex)
    {
        const uint64_t queryKey = queryKeys[queryIndex];
        auto &state = m_occlusionStates[queryKey];
        if (queryResults[queryIndex].available == 0ull)
            continue;

        const bool isOccludedSample = queryResults[queryIndex].samples == 0ull;
        if (isOccludedSample)
        {
            if (state.occludedConsecutiveResults < UINT8_MAX)
                ++state.occludedConsecutiveResults;
        }
        else
        {
            state.occludedConsecutiveResults = 0u;
        }

        state.hasResult = true;
        state.occluded = state.occludedConsecutiveResults >= occlusionConfirmationQueries;
        state.lastQueryFrame = submittedFrameNumber;
    }

    m_submittedOcclusionQueryCounts[frameIndex] = 0u;
    m_submittedOcclusionQueryKeys[frameIndex].clear();
    m_hasPendingOcclusionResolve[frameIndex] = false;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
