#include "app/util/TaskQueue.hpp"

namespace sloptube::app {

TaskQueue::TaskQueue(std::size_t workers, std::function<void()> wakeMainThread)
    : m_wakeMainThread(std::move(wakeMainThread)) {
    m_workers.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i) {
        m_workers.emplace_back([this] { workerLoop(); });
    }
}

TaskQueue::~TaskQueue() {
    {
        const std::scoped_lock lock(m_workMutex);
        m_stopping = true;
    }
    m_workReady.notify_all();
    for (auto& worker : m_workers) {
        worker.join();
    }
}

void TaskQueue::run(Task task) {
    {
        const std::scoped_lock lock(m_workMutex);
        m_work.push_back(std::move(task));
    }
    m_workReady.notify_one();
}

void TaskQueue::post(Task task) {
    {
        const std::scoped_lock lock(m_mainMutex);
        m_mainThreadTasks.push_back(std::move(task));
    }
    m_wakeMainThread();
}

bool TaskQueue::drainMainThread() {
    std::vector<Task> ready;
    {
        const std::scoped_lock lock(m_mainMutex);
        ready.swap(m_mainThreadTasks);
    }
    for (auto& task : ready) {
        task();
    }
    return !ready.empty();
}

void TaskQueue::workerLoop() {
    while (true) {
        Task task;
        {
            std::unique_lock lock(m_workMutex);
            m_workReady.wait(lock, [this] { return m_stopping || !m_work.empty(); });
            if (m_stopping) {
                return;
            }
            task = std::move(m_work.front());
            m_work.pop_front();
        }
        task();
    }
}

} // namespace sloptube::app
