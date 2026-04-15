#pragma once
#include <set>
#include <vector>
#include <atomic>
#include <memory>
#include <utility>

#include "Timestamp.h"
#include "Timer.h"
// 由于要操作 Timer*，且无需暴露 Timer 细节给外部，包含 Timer.h 最佳

class EventLoop;
class Channel;
class Timer; 

class TimerQueue {
public:
    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();
    
    TimerQueue(const TimerQueue&) = delete;
    TimerQueue& operator=(const TimerQueue&) = delete;

    // 接口全部换成 Timestamp 和 double
    TimerId addTimer(TimerCallback cb, Timestamp when, double interval);
    void cancel(TimerId timerId);

private:
    // 红黑树的 Key 换成 Timestamp
    using Entry = std::pair<Timestamp, Timer*>;
    using TimerList = std::set<Entry>;
    
    using ActiveTimer = std::pair<Timer*, std::uint64_t>; 
    using ActiveTimerSet = std::set<ActiveTimer>;

    void addTimerInLoop(Timer* timer);
    void cancelInLoop(TimerId timerId);
   
    void handleRead();

    // 统一使用 Timestamp
    std::vector<Entry> getExpired(Timestamp now);
    void reset(const std::vector<Entry>& expired, Timestamp now);

    bool insert(Timer* timer);

private:
    EventLoop* loop_;
    const int timerfd_; 
    std::unique_ptr<Channel> timerfdChannel_;

    TimerList timers_;
    ActiveTimerSet activeTimers_;
    std::atomic_uint64_t nextId_{1};
};