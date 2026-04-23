#ifndef ELIX_PIPELINE_PRE_WARMER_HPP
#define ELIX_PIPELINE_PRE_WARMER_HPP

#include "Core/Macros.hpp"
#include "Engine/Builders/GraphicsPipelineKey.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class PipelinePreWarmer
{
public:
    using CompletionCallback = std::function<void(uint32_t compiledCount, float elapsedMs)>;


    static void submitBatch(std::vector<GraphicsPipelineKey> keys,
                            CompletionCallback onComplete = {});


    static std::pair<uint32_t, uint32_t> queryProgress();


    static bool isIdle();


    static void waitAll();

private:
    static inline std::mutex s_mutex;
    static inline std::condition_variable s_cv;
    static inline std::atomic<uint32_t> s_completed{0};
    static inline std::atomic<uint32_t> s_total{0};
    static inline std::atomic<uint32_t> s_batchesInFlight{0};
};

ELIX_NESTED_NAMESPACE_END

#endif
