#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace sloptube::app {

// Background worker pool paired with a mailbox drained by the UI thread. Results computed on
// workers are handed back through post() so all UI state stays single-threaded.
class TaskQueue {
public:
    using Task = std::function<void()>;

    TaskQueue(std::size_t workers, std::function<void()> wakeMainThread);
    ~TaskQueue();

    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    void run(Task task);
    void post(Task task);

    // Runs every task posted so far; returns whether any ran.
    bool drainMainThread();

private:
    void workerLoop();

    std::function<void()> m_wakeMainThread;
    std::mutex m_workMutex;
    std::condition_variable m_workReady;
    std::deque<Task> m_work;
    bool m_stopping = false;
    std::mutex m_mainMutex;
    std::vector<Task> m_mainThreadTasks;
    std::vector<std::thread> m_workers;
};

} // namespace sloptube::app
