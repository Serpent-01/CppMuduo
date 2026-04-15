#pragma once
#include <cstdint>
#include <functional>
#include "Timestamp.h" // 引入我们自己的时间基石

using TimerCallback = std::function<void()>;

class Timer; // 前向声明

/*
    TimerId : 定时器的退订小票 
*/
class TimerId {
public:
    constexpr TimerId() = default;

    constexpr TimerId(Timer* timer, std::uint64_t seq) noexcept
        : timer_(timer), sequence_(seq) {}

    [[nodiscard]] constexpr Timer* timer() const noexcept { return timer_; }
    [[nodiscard]] constexpr std::uint64_t sequence() const noexcept { return sequence_; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return timer_ != nullptr; }

private:
    Timer* timer_{nullptr};        
    std::uint64_t sequence_{0};    
};

/*
    Timer : 定时器的实体
*/
class Timer {
public:
    // 注意：把 TimePoint 换成了 Timestamp，Duration 换成了 double (秒)
    Timer(TimerCallback cb, Timestamp expiration, double interval, std::uint64_t sequence)
        : callback_(std::move(cb)),
          expiration_(expiration),
          interval_(interval),
          sequence_(sequence) {}

    void run() const {
        if (callback_) {
            callback_();
        }
    }

    [[nodiscard]] bool repeat() const noexcept {
        return interval_ > 0.0;
    }

    void restart(Timestamp now) {
        if (repeat()) {
            // 使用 Timestamp.h 中提供的 addTime 辅助函数
            expiration_ = addTime(now, interval_);
        } else {
            // 使用默认构造，产生一个 invalid 的时间戳
            expiration_ = Timestamp(); 
        }
    }

    // Getter 接口
    [[nodiscard]] Timestamp expiration() const noexcept { return expiration_; }
    [[nodiscard]] std::uint64_t sequence() const noexcept { return sequence_; }

private:
    const TimerCallback callback_;
    const double interval_;        // Muduo 统一使用 double 表示秒级别的间隔
    const std::uint64_t sequence_; 
    Timestamp expiration_;         // 统一使用 Timestamp
};