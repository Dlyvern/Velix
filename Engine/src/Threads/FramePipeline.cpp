#include "Engine/Threads/FramePipeline.hpp"

#include <utility>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

FramePipeline::FramePipeline(Mode mode)
    : m_mode(mode)
{
}

FramePipeline::~FramePipeline()
{
    shutdown();
}

void FramePipeline::start(RenderCallback callback)
{
    m_renderCallback = std::move(callback);

    if (m_mode == Mode::Pipelined)
    {
        m_running.store(true, std::memory_order_release);
        m_renderThread = std::thread(&FramePipeline::renderThreadLoop, this);
    }
    else
    {
        m_running.store(true, std::memory_order_release);
    }
}

void FramePipeline::submitSnapshot(RenderSceneSnapshot &snapshot)
{
    if (m_mode == Mode::SingleThreaded)
    {
        if (m_renderCallback)
            m_renderCallback(snapshot);
        return;
    }



    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_snapshotConsumed.wait(lock, [this]
                                { return !m_hasUnconsumedSnapshot || m_shutdownRequested.load(std::memory_order_acquire); });

        if (m_shutdownRequested.load(std::memory_order_acquire))
            return;

        std::swap(m_pendingSnapshot, snapshot);
        m_hasUnconsumedSnapshot = true;
    }
    m_snapshotReady.notify_one();
}

void FramePipeline::shutdown()
{
    m_shutdownRequested.store(true, std::memory_order_release);


    m_snapshotReady.notify_all();
    m_snapshotConsumed.notify_all();

    if (m_renderThread.joinable())
        m_renderThread.join();

    m_running.store(false, std::memory_order_release);
}

void FramePipeline::renderThreadLoop()
{
    while (!m_shutdownRequested.load(std::memory_order_acquire))
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_snapshotReady.wait(lock, [this]
                                 { return m_hasUnconsumedSnapshot || m_shutdownRequested.load(std::memory_order_acquire); });

            if (m_shutdownRequested.load(std::memory_order_acquire))
                break;



            std::swap(m_renderThreadSnapshot, m_pendingSnapshot);
            m_hasUnconsumedSnapshot = false;
        }
        m_snapshotConsumed.notify_one();

        if (m_renderCallback)
            m_renderCallback(m_renderThreadSnapshot);
    }

    m_running.store(false, std::memory_order_release);
}

ELIX_NESTED_NAMESPACE_END
