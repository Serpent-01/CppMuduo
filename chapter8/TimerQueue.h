#pragma once
#include <set>
#include <vector>
#include <atomic>
#include <memory>
#include <utility>

// 统一依赖我们自己的时间类和 ID 类
#include "Timestamp.h"
#include "Timer.h"

class EventLoop;
class Channel;
class Timer; 

class TimerQueue{
public:
    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();
    
    // 严禁拷贝与赋值
    TimerQueue(const TimerQueue&) = delete;
    TimerQueue& operator=(const TimerQueue&) = delete;

    // 核心暴露接口(允许跨线程调用)
    TimerId addTimer(TimerCallback cb, Timestamp when, double interval);
    void cancel(TimerId timerId);

private:
    // 核心数据结构，保证时间相同也能排序和去重的红黑树
    using Entry = std::pair<Timestamp, Timer*>;
    using TimerList = std::set<Entry>;
    
    // 核心数据结构，活跃定时器集合
    using ActiveTimer = std::pair<Timer*, std::uint64_t>; 
    using ActiveTimerSet = std::set<ActiveTimer>;

    void addTimerInLoop(Timer* timer);
    void cancelInLoop(TimerId timerId);
   
    // timerfd 触发 EPOLLIN 时的回调事件
    void handleRead();

    // 提取出所有时间 <= now 的到期定时器
    std::vector<Entry> getExpired(Timestamp now);
    
    // 处理周期性定时器的重新入队，以及单次定时器的内存释放
    void reset(const std::vector<Entry>& expired, Timestamp now);

    //重置底层定时器的唤醒时间
    void resetTimerfd(int timerfd, Timestamp expiration);

    // 底层插入逻辑
    bool insert(Timer* timer);

private:
    EventLoop* loop_;
    const int timerfd_; // 统一事件源的句柄
    std::unique_ptr<Channel> timerfdChannel_;

    // 两颗红黑树，维护相同的 Timer* 集合，但排序依据不同
    TimerList timers_;            // 依据 (Timestamp, Address) 排序，处理触发
    ActiveTimerSet activeTimers_; // 依据 (Address, Sequence) 排序，处理撤销
    
    // 多线程安全的全局递增流水号生成器
    std::atomic_uint64_t nextId_{1};
};