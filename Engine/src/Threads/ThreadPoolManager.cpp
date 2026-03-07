#include "Engine/Threads/ThreadPoolManager.hpp"

#include <algorithm>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ThreadPoolManager &ThreadPoolManager::instance()
{
    static ThreadPoolManager manager;
    return manager;
}

ThreadPoolManager::ThreadPoolManager()
{
    const std::size_t hardwareThreads = std::max<std::size_t>(1u, std::thread::hardware_concurrency());
    const std::size_t workerCount = hardwareThreads > 1u ? hardwareThreads - 1u : 0u;

    m_workers.reserve(workerCount);
    for (std::size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
        m_workers.emplace_back([this]()
                               { workerLoop(); });
}

ThreadPoolManager::~ThreadPoolManager()
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stopping = true;
    }

    m_queueCv.notify_all();

    for (auto &worker : m_workers)
    {
        if (worker.joinable())
            worker.join();
    }
}

std::size_t ThreadPoolManager::getWorkerCount() const
{
    return m_workers.size();
}

std::size_t ThreadPoolManager::getMaxThreads() const
{
    return std::max<std::size_t>(1u, m_workers.size() + 1u);
}

void ThreadPoolManager::parallelFor(std::size_t taskCount, const RangeTask &task, std::size_t maxThreadCount)
{
    if (!task || taskCount == 0u)
        return;

    const std::size_t requestedThreadCount = maxThreadCount == 0u ? getMaxThreads()
                                                                  : std::max<std::size_t>(1u, maxThreadCount);
    const std::size_t threadCount = std::max<std::size_t>(1u, std::min(taskCount, requestedThreadCount));

    if (threadCount == 1u)
    {
        task(0u, taskCount);
        return;
    }

    auto batchState = std::make_shared<TaskBatchState>();
    batchState->remainingTasks = threadCount;

    for (std::size_t chunkIndex = 1u; chunkIndex < threadCount; ++chunkIndex)
    {
        const std::size_t begin = (taskCount * chunkIndex) / threadCount;
        const std::size_t end = (taskCount * (chunkIndex + 1u)) / threadCount;

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_tasks.emplace([batchState, task, begin, end]()
                            { executeOrCaptureTask(batchState, [task, begin, end]()
                                                   { task(begin, end); }); });
        }
    }

    m_queueCv.notify_all();

    const std::size_t callerBegin = 0u;
    const std::size_t callerEnd = taskCount / threadCount;
    executeOrCaptureTask(batchState, [task, callerBegin, callerEnd]()
                         { task(callerBegin, callerEnd); });

    std::unique_lock<std::mutex> lock(batchState->mutex);
    batchState->cv.wait(lock, [&batchState]()
                        { return batchState->remainingTasks == 0u; });

    if (batchState->firstException)
        std::rethrow_exception(batchState->firstException);
}

void ThreadPoolManager::workerLoop()
{
    for (;;)
    {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this]()
                           { return m_stopping || !m_tasks.empty(); });

            if (m_stopping && m_tasks.empty())
                return;

            task = std::move(m_tasks.front());
            m_tasks.pop();
        }

        task();
    }
}

void ThreadPoolManager::executeOrCaptureTask(const std::shared_ptr<TaskBatchState> &batchState,
                                             std::function<void()> task)
{
    try
    {
        task();
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(batchState->mutex);
        if (!batchState->firstException)
            batchState->firstException = std::current_exception();
    }

    {
        std::lock_guard<std::mutex> lock(batchState->mutex);
        if (batchState->remainingTasks == 0u)
            throw std::runtime_error("ThreadPoolManager task accounting underflow");

        --batchState->remainingTasks;
    }

    batchState->cv.notify_all();
}

ELIX_NESTED_NAMESPACE_END
