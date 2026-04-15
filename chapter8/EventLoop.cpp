#include "EventLoop.h"

#include <cassert>
#include <cstdlib>
#include <iostream>

#include "Channel.h"
#include "CurrentThread.h"
#include "Poller.h"

// 每个线程各自拥有一份 t_loopInThisThread。
// 它的意义是：保证 one loop per thread。
namespace {
    thread_local EventLoop* t_loopInThisThread = nullptr;
}

EventLoop* EventLoop::getEventLoopOfCurrentThread() noexcept {
    return t_loopInThisThread;
}

EventLoop::EventLoop()
    : threadId_(CurrentThread::tid()),
      poller_(std::make_unique<Poller>(this)) {
    std::cout << "[EventLoop] created " << this
              << " in thread " << threadId_ << '\n';

    // 同一个线程里不允许创建第二个 EventLoop
    if (t_loopInThisThread != nullptr) {
        std::cerr << "[EventLoop] fatal: another EventLoop("
                  << t_loopInThisThread
                  << ") already exists in this thread\n";
        std::abort();
    }

    t_loopInThisThread = this;
}

EventLoop::~EventLoop() {
    // 退出前必须保证 loop 已经停止
    assert(!looping_);
    t_loopInThisThread = nullptr;
}

void EventLoop::loop() {
    // 不允许重复进入 loop
    assert(!looping_);

    // EventLoop 只能在它所属线程里运行
    assertInLoopThread();

    looping_ = true;
    quit_ = false;

    // 这就是最小的“真正事件循环”：
    // 1. poll 等待事件
    // 2. 收集活跃 Channel
    // 3. 依次处理 Channel 上的事件
    while (!quit_) {
        activeChannels_.clear();

        // timeout 这里给 10 秒，方便演示。
        // 没有事件时，poll 会阻塞等待。
        pollReturnTime_ = poller_->poll(10'000, &activeChannels_);
        std::cerr << pollReturnTime_.toString() << std::endl;
        // 遍历本轮活跃 Channel，分发事件
        for (Channel* channel : activeChannels_) {
            assert(channel != nullptr);
            channel->handleEvent();
        }
    }

    looping_ = false;
    std::cout << "[EventLoop] " << this << " stopped looping\n";
}

void EventLoop::quit() noexcept {
    quit_ = true;
}

void EventLoop::updateChannel(Channel* channel) {
    assert(channel != nullptr);

    // 只有所属线程才能改 Poller 里的监听状态
    assertInLoopThread();

    // 真正干活的是 Poller
    poller_->updateChannel(channel);
}

void EventLoop::assertInLoopThread() const {
    if (!isInLoopThread()) {
        abortNotInLoopThread();
    }
}

bool EventLoop::isInLoopThread() const noexcept {
    return threadId_ == CurrentThread::tid();
}

[[noreturn]] void EventLoop::abortNotInLoopThread() const {
    std::cerr << "[EventLoop] wrong thread: loop was created in thread "
              << threadId_
              << ", current thread is "
              << CurrentThread::tid()
              << '\n';
    std::abort();
}