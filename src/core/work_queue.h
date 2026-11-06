#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>

namespace iv {

// A thread pool that runs the highest-priority task first and can cancel queued tasks.
// Groups let an owner cancel all its tasks, and wait for them before it is destroyed.
// Tasks must not let exceptions escape.
class WorkQueue {
public:
    using Task = std::move_only_function<void(std::stop_token)>;
    using TaskId = std::uint64_t;
    using GroupId = std::uint64_t;

    // About 2/3 of the logical cores, leaving the rest for the UI and everything else.
    static unsigned defaultThreadCount();

    // The queue shared by the whole application.
    static WorkQueue& global();

    explicit WorkQueue(unsigned threadCount = defaultThreadCount());
    ~WorkQueue();

    WorkQueue(const WorkQueue&) = delete;
    WorkQueue& operator=(const WorkQueue&) = delete;

    GroupId createGroup();

    // Higher priorities run first; equal priorities run in submission order.
    TaskId submit(GroupId group, int priority, Task task);

    // Removes the task if it is still queued, otherwise asks it to stop.
    void cancel(TaskId task);
    void cancelGroup(GroupId group);

    // Blocks until nothing from the group is queued or running.
    void waitGroup(GroupId group);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
