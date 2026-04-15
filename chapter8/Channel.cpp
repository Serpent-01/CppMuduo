#include "Channel.h"

#include <cassert>
#include <iostream>

#include "EventLoop.h"

// 有些平台可能没有定义 POLLRDHUP，这里兜一下。
// Linux 下通常有。
#ifndef POLLRDHUP
#define POLLRDHUP 0
#endif

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop), fd_(fd) {
    assert(loop_ != nullptr);
}

void Channel::update() {
    assert(loop_ != nullptr);

    // 这就是你前面一直在问的那条调用链：
    // Channel::update()
    // -> EventLoop::updateChannel(this)
    // -> Poller::updateChannel(this)
    loop_->updateChannel(this);
}

void Channel::handleEvent() {
    // 和书里的顺序保持一致：
    // 1. 先处理非法 fd / 错误
    // 2. 再处理读
    // 3. 再处理写

    if (revents_ & POLLNVAL) {
        std::cerr << "[Channel] warning: fd=" << fd_
                  << " got POLLNVAL\n";
    }

    if (revents_ & (POLLERR | POLLNVAL)) {
        if (errorCallback_) {
            errorCallback_();
        }
    }

    if (revents_ & (POLLIN | POLLPRI | POLLRDHUP)) {
        if (readCallback_) {
            readCallback_();
        }
    }

    if (revents_ & POLLOUT) {
        if (writeCallback_) {
            writeCallback_();
        }
    }
}