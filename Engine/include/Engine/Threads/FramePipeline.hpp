#ifndef ELIX_FRAME_PIPELINE_HPP
#define ELIX_FRAME_PIPELINE_HPP

#include "Core/Macros.hpp"

#include "Engine/Render/RenderSceneSnapshot.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

ELIX_NESTED_NAMESPACE_BEGIN(engine)







class FramePipeline
{
public:
    enum class Mode
    {
        SingleThreaded,
        Pipelined
    };

    using RenderCallback = std::function<void(const RenderSceneSnapshot &snapshot)>;

    explicit FramePipeline(Mode mode = Mode::SingleThreaded);
    ~FramePipeline();

    FramePipeline(const FramePipeline &) = delete;
    FramePipeline &operator=(const FramePipeline &) = delete;



    void start(RenderCallback callback);






    void submitSnapshot(RenderSceneSnapshot &snapshot);


    void shutdown();

    bool isRunning() const { return m_running.load(std::memory_order_acquire); }

private:
    void renderThreadLoop();

    Mode m_mode;
    RenderCallback m_renderCallback;

    std::thread m_renderThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_shutdownRequested{false};


    std::mutex m_mutex;
    std::condition_variable m_snapshotReady;
    std::condition_variable m_snapshotConsumed;
    RenderSceneSnapshot m_pendingSnapshot;
    RenderSceneSnapshot m_renderThreadSnapshot;
    bool m_hasUnconsumedSnapshot{false};
};

ELIX_NESTED_NAMESPACE_END

#endif
