#include "Poller.h"

#include <cassert>
#include <cstdio>

#include "Channel.h"
#include "EventLoop.h"

Poller::Poller(EventLoop* loop)
    : ownerLoop_(loop) {
    assert(ownerLoop_ != nullptr);
}

void Poller::assertInLoopThread() const {
    ownerLoop_->assertInLoopThread();
}

Timestamp Poller::poll(int timeoutMs, ChannelList* activeChannels) {
    assert(activeChannels != nullptr);

    // pollfds_.data() 在 empty() 时不一定适合直接传给 poll，
    // 所以这里显式做一个判空处理。
    const int numEvents = ::poll(
        pollfds_.empty() ? nullptr : pollfds_.data(),
        static_cast<nfds_t>(pollfds_.size()),
        timeoutMs
    );

    const Timestamp now = Timestamp::now();

    if (numEvents > 0) {
        fillActiveChannels(numEvents, activeChannels);
    } else if (numEvents < 0) {
        // 最小版本里直接 perror
        std::perror("poll");
    }

    return now;
}
    //把发生了事件的 fd，对应回它的 Channel，然后放进 activeChannels 
    //把 poll 返回的结果，从 fd 重新映射成 Channel
void Poller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const {
    // 遍历 pollfds_，把 revents > 0 的项找出来
    for (const auto& pfd : pollfds_) {
        if (numEvents <= 0) {
            break;
        }

        if (pfd.revents <= 0) {
            continue;
        }

        --numEvents;

        // 当 channel 处于“无关注事件”状态时，
        // 我们在 updateChannel() 中把 fd 改成 -fd-1 用来忽略。
        // 这类 fd 理论上不会出现在 active list 中，这里做个保护。
        if (pfd.fd < 0) {
            continue;
        }

        auto it = channels_.find(pfd.fd);
        //必须能在 channels_ 里找到这个 fd 对应的 Channel
        assert(it != channels_.end());

        Channel* channel = it->second;
        channel->set_revents(pfd.revents);
        activeChannels->push_back(channel);
    }
}

void Poller::updateChannel(Channel* channel) {
    assert(channel != nullptr);
    assertInLoopThread();

    // index < 0 说明这个 Channel 之前从未加入过 Poller
    if (channel->index() < 0) {
        const int fd = channel->fd();
        const int index = static_cast<int>(pollfds_.size());

        struct pollfd pfd {};
        pfd.fd = fd;
        pfd.events = static_cast<short>(channel->events());
        pfd.revents = 0;

        pollfds_.push_back(pfd);
        channels_[fd] = channel;
        channel->set_index(index);
        return;
    }

    // 走到这里说明这个 Channel 已经在 Poller 里了，
    // 我们只需要更新它在 pollfds_ 里的关心事件即可。
    const int index = channel->index();
    assert(index >= 0);
    assert(static_cast<std::size_t>(index) < pollfds_.size());

    struct pollfd& pfd = pollfds_[static_cast<std::size_t>(index)];

    // 书里 poll 版的一个经典小技巧：
    // 当 channel 当前不关心任何事件时，不把它从 pollfds_ 里物理删除，
    // 而是把 fd 改成负数，这样 poll 会自动忽略它。
    //
    // 为什么写成 -fd - 1？
    // 因为如果 fd 本身是 0，那么 -fd 还是 0，不够安全；
    // 写成 -fd-1 可以确保一定是负数。
    pfd.fd = channel->isNoneEvent() ? (-channel->fd() - 1) : channel->fd();
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;
}