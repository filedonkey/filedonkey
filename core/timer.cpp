#include "timer.h"

Timer::~Timer()
{
    stop();
}

void Timer::start(std::function<void()> callback, std::chrono::milliseconds interval)
{
    stop();

    {
        std::lock_guard lock(mutex);
        active = true;
    }

    worker_thread = std::thread([this, callback = std::move(callback), interval]() {
        std::unique_lock lock(mutex);

        while (active)
        {
            if (cv.wait_for(lock, interval, [this] { return !active; }))
            {
                break;
            }

            lock.unlock();
            callback();
            lock.lock();
        }
    });
}

void Timer::stop()
{
    {
        std::lock_guard lock(mutex);
        active = false;
    }

    cv.notify_one();

    if (worker_thread.joinable())
        worker_thread.join();
}
