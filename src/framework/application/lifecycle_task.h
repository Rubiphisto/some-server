#pragma once

#include <functional>
#include <thread>
#include <utility>

class LifecycleTask
{
public:
    using Poller = std::function<bool()>;

    static LifecycleTask Completed()
    {
        return LifecycleTask();
    }

    static LifecycleTask Poll(Poller poller)
    {
        return LifecycleTask(std::move(poller));
    }

    bool IsCompleted() const
    {
        return !mPoller || mPoller();
    }

    void Wait() const
    {
        if (!mPoller)
        {
            return;
        }

        while (!mPoller())
        {
            std::this_thread::yield();
        }
    }

private:
    explicit LifecycleTask(Poller poller = {}) : mPoller(std::move(poller)) {}

    Poller mPoller;
};
