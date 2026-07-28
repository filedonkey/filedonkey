#ifndef TIMER_H
#define TIMER_H

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

class Timer
{
public:
    Timer() = default;
    ~Timer();

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    void start(std::function<void()> callback,
               std::chrono::milliseconds interval);

    void stop();

private:
    bool active = false;

    std::thread worker_thread;

    std::mutex mutex;
    std::condition_variable cv;
};

#endif // TIMER_H
