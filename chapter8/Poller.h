#pragma once
#include <map>
#include <poll.h>
#include <vector>

#include "Timestamp.h"

class Channel;
class EventLoop;

// Poller 的职责：
// 1. 封装 poll(2)
// 2. 保存所有被监听的 pollfd
// 3. 把 poll 返回的活跃事件填充到 activeChannels
//  Poller 监听的是 fd，返回的是 Channel
// 注意：Poller 不拥有 Channel 对象。
// 它只是保存 fd -> Channel* 的映射。
class Poller {
public:
    using ChannelList = std::vector<Channel*>;

    explicit Poller(EventLoop* loop);
    ~Poller() = default;

    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    Poller(Poller&&) = delete;
    Poller& operator=(Poller&&) = delete;

    // 调用 poll() 等待事件
    // 返回值：本次 poll 返回的时间点
    [[nodiscard]] Timestamp poll(int timeoutMs, ChannelList* activeChannels);

    // 更新某个 Channel 感兴趣的事件
    void updateChannel(Channel* channel);

    // 保证 Poller 只在所属 loop 线程中使用
    void assertInLoopThread() const;

private:
    // 从 pollfds_ 中提取有事件发生的 Channel
    void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;

private:
    using PollFdList = std::vector<struct pollfd>;
    using ChannelMap = std::map<int, Channel*>;

    EventLoop* ownerLoop_;  // 所属 EventLoop
    PollFdList pollfds_;    // 传给 ::poll() 的数组
    ChannelMap channels_;   // fd -> Channel*
};
