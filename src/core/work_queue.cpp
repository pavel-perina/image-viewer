#include "core/work_queue.h"

#include <algorithm>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace iv {

namespace {

// Sorts by descending priority, then submission order.
using QueueKey = std::pair<int, std::uint64_t>;

struct QueuedTask {
    WorkQueue::TaskId id;
    WorkQueue::GroupId group;
    WorkQueue::Task task;
};

struct RunningTask {
    WorkQueue::GroupId group;
    std::stop_source stop;
};

}  // namespace

struct WorkQueue::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable finished;
    std::map<QueueKey, QueuedTask> queued;
    std::unordered_map<TaskId, QueueKey> queuedKeys;
    std::unordered_map<TaskId, RunningTask> running;
    std::uint64_t nextId = 1;
    bool shuttingDown = false;
    std::vector<std::jthread> threads;

    void workerLoop() {
        std::unique_lock lock(mutex);
        while (true) {
            wake.wait(lock, [this] { return shuttingDown || !queued.empty(); });
            if (shuttingDown) {
                return;
            }
            auto node = queued.extract(queued.begin());
            QueuedTask& item = node.mapped();
            queuedKeys.erase(item.id);
            std::stop_source stop;
            running.emplace(item.id, RunningTask{item.group, stop});

            lock.unlock();
            item.task(stop.get_token());
            // Destroy the task, and whatever it captured, before reporting it finished.
            item.task = nullptr;
            lock.lock();

            running.erase(item.id);
            finished.notify_all();
        }
    }

    bool groupBusy(GroupId group) const {
        const auto inGroup = [group](const auto& entry) { return entry.second.group == group; };
        return std::ranges::any_of(queued, inGroup) || std::ranges::any_of(running, inGroup);
    }
};

unsigned WorkQueue::defaultThreadCount() {
    return std::max(1u, std::thread::hardware_concurrency() * 2 / 3);
}

WorkQueue& WorkQueue::global() {
    static WorkQueue queue;
    return queue;
}

WorkQueue::WorkQueue(unsigned threadCount) : m_impl(std::make_unique<Impl>()) {
    threadCount = std::max(1u, threadCount);
    m_impl->threads.reserve(threadCount);
    for (unsigned i = 0; i < threadCount; ++i) {
        m_impl->threads.emplace_back([impl = m_impl.get()] { impl->workerLoop(); });
    }
}

WorkQueue::~WorkQueue() {
    {
        const std::scoped_lock lock(m_impl->mutex);
        m_impl->shuttingDown = true;
        m_impl->queued.clear();
        for (auto& [id, task] : m_impl->running) {
            task.stop.request_stop();
        }
    }
    m_impl->wake.notify_all();
    m_impl->threads.clear();  // joins
}

WorkQueue::GroupId WorkQueue::createGroup() {
    const std::scoped_lock lock(m_impl->mutex);
    return m_impl->nextId++;
}

WorkQueue::TaskId WorkQueue::submit(GroupId group, int priority, Task task) {
    TaskId id;
    {
        const std::scoped_lock lock(m_impl->mutex);
        id = m_impl->nextId++;
        const QueueKey key{-priority, id};
        m_impl->queued.emplace(key, QueuedTask{id, group, std::move(task)});
        m_impl->queuedKeys.emplace(id, key);
    }
    m_impl->wake.notify_one();
    return id;
}

void WorkQueue::cancel(TaskId task) {
    const std::scoped_lock lock(m_impl->mutex);
    if (const auto key = m_impl->queuedKeys.find(task); key != m_impl->queuedKeys.end()) {
        m_impl->queued.erase(key->second);
        m_impl->queuedKeys.erase(key);
        m_impl->finished.notify_all();
    } else if (const auto run = m_impl->running.find(task); run != m_impl->running.end()) {
        run->second.stop.request_stop();
    }
}

void WorkQueue::cancelGroup(GroupId group) {
    const std::scoped_lock lock(m_impl->mutex);
    for (auto it = m_impl->queued.begin(); it != m_impl->queued.end();) {
        if (it->second.group == group) {
            m_impl->queuedKeys.erase(it->second.id);
            it = m_impl->queued.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& [id, task] : m_impl->running) {
        if (task.group == group) {
            task.stop.request_stop();
        }
    }
    m_impl->finished.notify_all();
}

void WorkQueue::waitGroup(GroupId group) {
    std::unique_lock lock(m_impl->mutex);
    m_impl->finished.wait(lock, [&] { return !m_impl->groupBusy(group); });
}

}  // namespace iv
