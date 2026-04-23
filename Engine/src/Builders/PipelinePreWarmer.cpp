#include "Engine/Builders/PipelinePreWarmer.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Threads/ThreadPoolManager.hpp"

#include "Core/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void PipelinePreWarmer::submitBatch(std::vector<GraphicsPipelineKey> keys,
                                    CompletionCallback onComplete)
{

    keys.erase(std::remove_if(keys.begin(), keys.end(),
                               [](const GraphicsPipelineKey &key) {
                                   return GraphicsPipelineManager::exists(key);
                               }),
               keys.end());

    if (keys.empty())
    {
        if (onComplete)
            onComplete(0, 0.0f);
        return;
    }

    const uint32_t batchSize = static_cast<uint32_t>(keys.size());
    s_total.fetch_add(batchSize, std::memory_order_relaxed);
    s_batchesInFlight.fetch_add(1, std::memory_order_relaxed);

    VX_ENGINE_INFO_STREAM("[PipelinePreWarmer] Submitting batch of " << batchSize << " pipeline(s) for background compilation");


    std::thread([keys = std::move(keys), batchSize, onComplete = std::move(onComplete)]() mutable {
        const auto startTime = std::chrono::steady_clock::now();

        const std::size_t maxThreads = std::max<std::size_t>(1, ThreadPoolManager::instance().getWorkerCount() / 2);

        ThreadPoolManager::instance().parallelFor(
            keys.size(),
            [&keys](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i)
                    GraphicsPipelineManager::getOrCreate(keys[i]);
            },
            maxThreads);

        s_completed.fetch_add(batchSize, std::memory_order_relaxed);
        s_batchesInFlight.fetch_sub(1, std::memory_order_relaxed);

        const auto endTime = std::chrono::steady_clock::now();
        const float elapsedMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();

        VX_ENGINE_INFO_STREAM("[PipelinePreWarmer] Batch complete: " << batchSize
                              << " pipeline(s) compiled in " << elapsedMs << " ms");

        if (onComplete)
            onComplete(batchSize, elapsedMs);

        s_cv.notify_all();
    }).detach();
}

std::pair<uint32_t, uint32_t> PipelinePreWarmer::queryProgress()
{
    return {s_completed.load(std::memory_order_relaxed),
            s_total.load(std::memory_order_relaxed)};
}

bool PipelinePreWarmer::isIdle()
{
    return s_batchesInFlight.load(std::memory_order_relaxed) == 0;
}

void PipelinePreWarmer::waitAll()
{
    if (isIdle())
        return;

    std::unique_lock lock(s_mutex);
    s_cv.wait(lock, [] { return s_batchesInFlight.load(std::memory_order_relaxed) == 0; });
}

ELIX_NESTED_NAMESPACE_END
