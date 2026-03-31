#ifndef ELIX_ASSET_STREAMING_WORKER_HPP
#define ELIX_ASSET_STREAMING_WORKER_HPP

#include "Core/Macros.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class AssetStreamingWorker
{
public:
    struct LoadJob
    {
        std::string path;
        std::function<void()> execute; // deserializes and resolves the handle
    };

    AssetStreamingWorker();
    ~AssetStreamingWorker();

    AssetStreamingWorker(const AssetStreamingWorker &) = delete;
    AssetStreamingWorker &operator=(const AssetStreamingWorker &) = delete;

    // Post a job. Thread-safe. Returns immediately.
    void enqueue(LoadJob job);

    // Number of jobs currently queued or executing.
    uint32_t pendingCount() const;

    void shutdown();

private:
    void workerLoop();

    std::thread m_thread;
    std::queue<LoadJob> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<uint32_t> m_pending{0};
    std::atomic<bool> m_running{true};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASSET_STREAMING_WORKER_HPP
