#pragma once
#include <cstdint>
#include <functional>

// 引入我们自己的时间基石
#include "Timestamp.h" 

using TimerCallback = std::function<void()>;

class Timer; // 前向声明，因为 TimerId 里面要用 Timer*

/*
    TimerId : 定时器的退订小票 
    核心设计：保存裸指针(追求极速定位) + 流水号(防范悬空指针复用)
*/
class TimerId {
public:
    constexpr TimerId() = default;

    // 构造函数：同时接收指针和流水号
    constexpr TimerId(Timer* timer, std::uint64_t seq) noexcept
        : timer_(timer), sequence_(seq) {}

    // 暴露给 TimerQueue 去红黑树里精准狙击的 Getter
    [[nodiscard]] constexpr Timer* timer() const noexcept { return timer_; }
    [[nodiscard]] constexpr std::uint64_t sequence() const noexcept { return sequence_; }

    // 重载 bool：指针不为空即认为初步有效
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return timer_ != nullptr; }

private:
    Timer* timer_{nullptr};        // 物理钥匙：直接指向闹钟所在的内存地址
    std::uint64_t sequence_{0};    // 动态密码：验证这个内存地址有没有被换了灵魂
};

/*
    Timer : 定时器的实体
*/
class Timer {
public:
    // 注意：统一使用 Timestamp 绝对时间，以及 double (秒) 作为间隔
    Timer(TimerCallback cb, Timestamp expiration, double interval, std::uint64_t sequence)
        : callback_(std::move(cb)),
          interval_(interval),
          sequence_(sequence),
          expiration_(expiration) {} 

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
            // 使用 Timestamp.h 提供的高效计算函数
            expiration_ = addTime(now, interval_);
        } else {
            // 产生一个无效的时间戳
            expiration_ = Timestamp(); 
        }
    }

    // Getter 接口
    [[nodiscard]] Timestamp expiration() const noexcept { return expiration_; }
    [[nodiscard]] std::uint64_t sequence() const noexcept { return sequence_; }

private:
    // STL 哲学与底层铁律：不可变属性加 const 锁死
    const TimerCallback callback_;
    const double interval_;        // 时间间隔，连续两次闹钟响铃之间的时间差！(单位：秒)
    const std::uint64_t sequence_; // 闹钟的灵魂编码，出生后绝对不可更改
    
    // 绝对到期时间
    Timestamp expiration_;         // 唯一允许变化的变量（周期性闹钟每次响完会更新）
};