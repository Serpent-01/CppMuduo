#pragma once

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
//Timestamp的核心使命：
/*
    作为一个极其轻量级的绝对时间标志。
    用于记录网络事件发生的精确时刻，以及作为定时器队列(TimerQueue)的排序依据
*/
class Timestamp{
public:
    using MicroSeconds = std::int64_t;
    using Clock = std::chrono::system_clock;
    //默认构造函数:创建一个无效的时间戳(值为 0，即1970-01-01)
    //在网络库中，经常用 Timestamp() 表示当前值为 0 ，此时表示无效时间，没有定时任务或者事件尚未发生。
    constexpr Timestamp() = default;

    //显示构造，根据指定的微妙创建时间戳
    // 必须加 explicit 防止把普通数字隐式转换成时间戳
    // explicit 防止 Timestamp t = 1000，这种情况
    constexpr explicit Timestamp (MicroSeconds miscroSecondsSinceEpoch) noexcept 
    : microSecondsSinceEpoch_(miscroSecondsSinceEpoch){}
    
    //检查时间戳是否有效 
    [[nodiscard]] constexpr bool valid() const noexcept{
        return microSecondsSinceEpoch_ > 0;
    }
    //返回微妙数
    [[nodiscard]] constexpr MicroSeconds microSecondsSinceEpoch() const noexcept{
        return microSecondsSinceEpoch_;
    }
    //将数字，转成字符串
    //返回格式如 : 2026-04-14 13:29:49.123456
    [[nodiscard]] std::string toString() const{
        /*
            std::time_t
                本质：它通常是一个 long int 
        */
        std::time_t seconds = static_cast<std::time_t>(microSecondsSinceEpoch_ / kMicroSecondsPerSecond);
        const int microseconds = static_cast<int>(microSecondsSinceEpoch_ % kMicroSecondsPerSecond);
        /*
            std::tm 是一个结构体
            里面包含了 tm_year 、tm_mon 、tm_mday 、tm_hour等。
        */
        std::tm tmTime{};
        //localtime_r 线程安全函数，把 time_t 转成 tm
        localtime_r(&seconds,&tmTime);
        
        std::ostringstream oss;
        /*
            <iomanip>
            std::put_time 用来格式化 std::tm的工具 %Y代表四位数年份,%m 代表两位的月份
            setw(6) 的意思是：“微秒这个数字，必须占满 6 个格子的宽度”。
            setfill('0') 的意思是：“如果数字不够 6 个格子，前面给我全部用 0 补齐”。
            组合起来，12 就会被严格格式化为 000012。最终日志输出 ...36.000012。
        */
        oss << std::put_time(&tmTime,"%Y-%m-%d %H:%M:%S")
            <<'.'
            << std::setw(6) << std::setfill('0') << microseconds;
        return oss.str();
    }
    //获取系统当前精确的时间戳（微秒级）
    [[nodiscard]] static Timestamp now() noexcept{
        // 利用 chrono 获取当前时间，并截断到微妙精度
        /*
            Clock::now()：向操作系统内核索要当前精确的时间点（Time Point）。
            time_point_cast<...>(...)：C++ 极其看重“类型转换的明确性”。
            既然操作系统的精度不可控，而我们的 Timestamp 类只想要微秒 (microseconds)。
        */
        const auto now = std::chrono::time_point_cast<std::chrono::microseconds>(Clock::now());
        // count() 提取出底层的纯 int64_t 数字，用于构造 Timestamp
        return Timestamp(now.time_since_epoch().count());
    }
    //公开常量 : 1秒 = 1,000,000 微秒
    static constexpr  MicroSeconds kMicroSecondsPerSecond = 1000000;
private:
    MicroSeconds microSecondsSinceEpoch_{0};
};

inline constexpr bool operator<(Timestamp lhs, Timestamp rhs) noexcept{
    return lhs.microSecondsSinceEpoch() < rhs.microSecondsSinceEpoch();
}
inline constexpr bool operator==(Timestamp lhs,Timestamp rhs) noexcept{
    return lhs.microSecondsSinceEpoch() == rhs.microSecondsSinceEpoch();
}
inline constexpr bool operator<=(Timestamp lhs,Timestamp rhs) noexcept{
    return lhs.microSecondsSinceEpoch() <= rhs.microSecondsSinceEpoch();
}

//计算未来时间点
//场景：定时器要求 3.5 秒后踢到客户端。传入 Timestamp::now() 和 3.5 返回未来的绝对时间点
inline constexpr Timestamp addTime(Timestamp timestamp,double seconds) noexcept{
    auto delta = static_cast<Timestamp::MicroSeconds>(seconds * Timestamp::kMicroSecondsPerSecond);
    return Timestamp(timestamp.microSecondsSinceEpoch() + delta);
}
//时间差测算:计算性能耗时或超时判断
//业务场景：测算一下 epoll_wait 唤醒到回调执行花了多久
//返回高精度的秒数(如返回 0.0015，表示 1.5毫秒)
inline constexpr double timeDifference(Timestamp high ,Timestamp low) noexcept{
    Timestamp::MicroSeconds diff = high.microSecondsSinceEpoch() - low.microSecondsSinceEpoch();
    return static_cast<double>(diff)/1000000.0;
}