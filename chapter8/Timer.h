#pragma once
#include <chrono>
#include <cstdint>
#include <functional>

using TimerCallback = std::function<void()>;
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point; 
using Duration = Clock::duration;   

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
    // 注意：我把你的 id 参数改名为了 sequence，更符合它“单调递增流水号”的本质
    Timer(TimerCallback cb, TimePoint expiration, Duration interval, std::uint64_t sequence)
        : callback_(std::move(cb)),
          expiration_(expiration),
          interval_(interval),
          sequence_(sequence) {} // 初始化流水号

    void run() const {
        if (callback_) {
            callback_();
        }
    }

    [[nodiscard]] bool repeat() const noexcept {
        return interval_ != Duration::zero();
    }

    void restart(TimePoint now) {
        if (repeat()) {
            expiration_ = now + interval_;
        } else {
            expiration_ = TimePoint::max(); 
        }
    }

    // Getter 接口
    [[nodiscard]] TimePoint expiration() const noexcept { return expiration_; }
    [[nodiscard]] std::uint64_t sequence() const noexcept { return sequence_; }

private:
    // STL 哲学与底层铁律：不可变属性加 const 锁死
    const TimerCallback callback_;
    const Duration interval_; //时间间隔，连续两次闹钟响铃之间的时间差！
    const std::uint64_t sequence_; // 闹钟的灵魂编码，出生后绝对不可更改
    // 绝对到期时间
    TimePoint expiration_; // 唯一允许变化的变量（周期性闹钟每次响完会更新）
};