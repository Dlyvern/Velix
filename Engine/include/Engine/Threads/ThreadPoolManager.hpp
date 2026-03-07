#ifndef ELIX_THREAD_POOL_MANAGER_HPP
#define ELIX_THREAD_POOL_MANAGER_HPP

#include "Core/Macros.hpp"

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ThreadPoolManager
{
public:
    using RangeTask = std::function<void(std::size_t begin, std::size_t end)>;

    static ThreadPoolManager &instance();

    ThreadPoolManager(const ThreadPoolManager &) = delete;
    ThreadPoolManager &operator=(const ThreadPoolManager &) = delete;

    ~ThreadPoolManager();

    std::size_t getWorkerCount() const;
    std::size_t getMaxThreads() const;

    void parallelFor(std::size_t taskCount, const RangeTask &task, std::size_t maxThreadCount = 0u);

private:
    struct TaskBatchState
    {
        std::mutex mutex;
        std::condition_variable cv;
        std::size_t remainingTasks{0u};
        std::exception_ptr firstException{nullptr};
    };

    ThreadPoolManager();

    void workerLoop();
    static void executeOrCaptureTask(const std::shared_ptr<TaskBatchState> &batchState,
                                     std::function<void()> task);

    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::queue<std::function<void()>> m_tasks;
    std::vector<std::thread> m_workers;
    bool m_stopping{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_THREAD_POOL_MANAGER_HPP
