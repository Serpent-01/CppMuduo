#pragma once

#include <memory>
#include <thread>
#include <vector>

#include "Timestamp.h"

// 前向声明，避免头文件循环包含
class Channel;
class Poller;

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

    // 退出事件循环
    // 当前版本是最小骨架版，不带 wakeupFd。
    // 所以 quit() 只是把 quit_ 置为 true，
    // 真正退出发生在下一次 poll 返回之后。
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

private:
    [[noreturn]] void abortNotInLoopThread() const;

private:
    bool looping_{false};                 // 当前 loop 是否正在运行
    bool quit_{false};                    // 是否请求退出
    const std::thread::id threadId_;      // 创建该 EventLoop 的线程 ID
    Timestamp pollReturnTime_{};          // 最近一次 poll 返回的时间
    std::unique_ptr<Poller> poller_;      // EventLoop 直接拥有的 Poller
    ChannelList activeChannels_;          // 本轮 poll 得到的活跃 Channel
};
