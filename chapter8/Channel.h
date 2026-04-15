#pragma once

#include <functional>
#include <poll.h>

class EventLoop;

// Channel 的本质：
// “一个 fd 的事件代理对象”
//
// 它不拥有 fd，只是负责描述：
// 1. 这个 fd 当前关心哪些事件（events_）
// 2. 这次 poll 实际返回了哪些事件（revents_）
// 3. 事件发生后应该调用哪些回调
//
// 对应书里：
// A selectable I/O channel.
// This class doesn't own the file descriptor.
class Channel {
public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd);
    ~Channel() = default;

    Channel(const Channel&) = delete;//禁止拷贝构造
    Channel& operator=(const Channel&) = delete;//禁止拷贝
    /*
        禁止 C++ 的移动语义 std::move
        禁止把一个Channel对象的内部数据 移动到另一个Channel对象。
    */
    Channel(Channel&&) = delete; //禁止移动构造
    Channel& operator=(Channel&&) = delete; //禁止移动赋值

    // 处理本轮返回的事件
    void handleEvent();

    // 设置三类回调：读 / 写 / 错误
    void setReadCallback(EventCallback cb)  { readCallback_  = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] int events() const noexcept { return events_; }

    // Poller 在 poll 返回后会把实际发生的事件写入 revents_
    void set_revents(int revt) noexcept { revents_ = revt; }

    [[nodiscard]] bool isNoneEvent() const noexcept {
        return events_ == kNoneEvent;
    }

    // 开始关心“读事件”
    void enableReading() {
        events_ |= kReadEvent;
        update();
    }

    // 开始关心“写事件”
    void enableWriting() {
        events_ |= kWriteEvent;
        update();
    }

    // 取消关心“写事件”
    void disableWriting() {
        events_ &= ~kWriteEvent;
        update();
    }

    // 取消关心所有事件
    void disableAll() {
        events_ = kNoneEvent;
        update();
    }

    // index_ 给 Poller 用，表示这个 Channel 在 pollfds_ 数组中的位置
    [[nodiscard]] int index() const noexcept { return index_; }
    void set_index(int idx) noexcept { index_ = idx; }

    [[nodiscard]] EventLoop* ownerLoop() const noexcept { return loop_; }

private:
    // Channel 不直接更新 Poller，而是找所属 EventLoop 代理更新
    void update();

private:
    // 用 inline static constexpr 替代旧式的 .cc 里静态常量定义
     static constexpr int kNoneEvent  = 0;
     static constexpr int kReadEvent  = POLLIN | POLLPRI;
     static constexpr int kWriteEvent = POLLOUT;

    EventLoop* loop_;      // 所属 EventLoop
    const int fd_;         // 被代理的文件描述符

    int events_{kNoneEvent}; // 当前感兴趣的事件
    int revents_{0};         // poll 实际返回的事件
    int index_{-1};          // 在 Poller::pollfds_ 中的位置

    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback errorCallback_;
};
