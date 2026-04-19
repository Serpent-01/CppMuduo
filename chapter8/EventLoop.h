#pragma once

#include <memory>
#include <thread>
#include <vector>
#include <functional>
#include "Timestamp.h"
#include "Timer.h"
#include <mutex>
#include <atomic>
// 前向声明，避免头文件循环包含
class Channel;
class Poller;
class TimerQueue;
// EventLoop 是当前线程里的“事件循环核心”。
// 你可以把它理解成：
// 1. 一个线程只允许有一个 EventLoop
// 2. EventLoop 内部拥有一个 Poller
// 3. EventLoop 负责：
//    - 调用 poller 等待 I/O 事件
//    - 拿到活跃 Channel
//    - 调用 Channel::handleEvent() 分发事件
class EventLoop {
public:
    using ChannelList = std::vector<Channel*>;
    using Functor = std::function<void()>;
    EventLoop();
    ~EventLoop();

    // EventLoop 不允许拷贝，也不允许移动。
    // 原因：
    // 1. 它和“线程归属”强绑定
    // 2. 内部维护 Poller、activeChannels 等状态
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

    // 进入事件循环
    void loop();

    // 请求退出事件循环
    /*
        设置 quit_ = true
        如果调用线程不是 loop 线程，或者 loop 可能阻塞着
        还要 wakeup()
    */
    void quit() noexcept;

    // 给 Channel 调用的接口：
    // Channel 自己不直接碰 Poller，
    // 而是通过所属的 EventLoop 来更新关注事件。
    void updateChannel(Channel* channel);

    // 线程归属检查
    void assertInLoopThread() const;
    [[nodiscard]] bool isInLoopThread() const noexcept;

    // 获取当前线程里的 EventLoop
    [[nodiscard]] static EventLoop* getEventLoopOfCurrentThread() noexcept;
    //直接执行定时任务
    [[nodiscard]] TimerId runAt(const Timestamp& time,const TimerCallback& cb);
    //延迟执行定时任务
    [[nodiscard]] TimerId runAfter(double delay,const TimerCallback& cb);
    //周期性执行定时任务
    [[nodiscard]] TimerId runEvery(double interval,const TimerCallback& cb);
    
    //跨线程执行任务相关
    // 如果当前就在 loop 线程中，可直接执行
    // 否则通常转为 queueInLoop()
    void runInLoop(Functor cb);

    //把任务放入待执行队列，稍后由 loop线程执行
    void queueInLoop(Functor cb);

private:
    [[noreturn]] void abortNotInLoopThread() const;

    // 处理 wakeupFd_ 的可读事件
    void handleRead();//waked up

    // 执行 pendingFunctors_ 中暂存的任务
    void doPendingFunctors();
    
    //唤醒阻塞中的 EventLoop
    void wakeup();

private:
    bool looping_{false};                 // 当前 loop 是否正在运行
    std::atomic<bool> quit_{false};                   // 是否请求退出
    std::atomic<bool> callingPendingFunctors_{false};  // 当前是否正在执行 pendingFunctors_
    int wakeupFd_{-1};                    // 用于唤醒阻塞中的 EventLoop 的内部 fd
    const std::thread::id threadId_;      // 创建该 EventLoop 的线程 ID
    Timestamp pollReturnTime_{};          // 最近一次 poll 返回的时间
    std::unique_ptr<Poller> poller_;      // EventLoop 直接拥有的 Poller
    ChannelList activeChannels_;          // 本轮 poll 得到的活跃 Channel
    std::unique_ptr<TimerQueue> timerQueue_;
    //用于监听 wakeupFd_ 的Channel
    std::unique_ptr<Channel> wakeupChannel_;
    
    //只有 pendingFunctors_ 会被其他线程访问，因此要加锁保护
    std::mutex mutex_;
    std::vector<Functor> pendingFunctors_;
};
