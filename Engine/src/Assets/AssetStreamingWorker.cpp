#include "Engine/Assets/AssetStreamingWorker.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

AssetStreamingWorker::AssetStreamingWorker()
{
    m_thread = std::thread([this]
                           { workerLoop(); });
}

AssetStreamingWorker::~AssetStreamingWorker()
{
    shutdown();
}

void AssetStreamingWorker::enqueue(LoadJob job)
{
    {
        std::lock_guard lock(m_mutex);
        m_pending.fetch_add(1u, std::memory_order_relaxed);
        m_queue.push(std::move(job));
    }
    m_cv.notify_one();
}

uint32_t AssetStreamingWorker::pendingCount() const
{
    return m_pending.load(std::memory_order_relaxed);
}

void AssetStreamingWorker::shutdown()
{
    {
        std::lock_guard lock(m_mutex);
        m_running.store(false, std::memory_order_release);
    }
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void AssetStreamingWorker::workerLoop()
{
    while (true)
    {
        LoadJob job;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this]
                      { return !m_queue.empty() || !m_running.load(std::memory_order_acquire); });

            if (!m_running.load(std::memory_order_acquire) && m_queue.empty())
                return;

            job = std::move(m_queue.front());
            m_queue.pop();
        }

        if (job.execute)
            job.execute();

        m_pending.fetch_sub(1u, std::memory_order_relaxed);
    }
}

ELIX_NESTED_NAMESPACE_END
