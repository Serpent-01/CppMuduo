#include "TimerQueue.h"
#include "EventLoop.h"
#include "Channel.h"
#include "Timer.h"
#include "Timestamp.h"

#include <ctime>

#include <sys/timerfd.h>
#include <unistd.h>
#include <cstring>
#include <cassert>

namespace{
    int createTimerfd(){
        //CLOCK_MONOTONIC 对应我们的 steady_clock，绝对单调时间
        //TFD_NONBLOCK :使返回的文件描述符带有非阻塞行为。
        //TFD_CLOEXEC : 在创建时设置 close-on-exec (FD_CLOEXEC) 标志，调用 exec() 系列函数时该 fd会被自动关闭，避免子进程意外继承。
        int timerfd = ::timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK | TFD_CLOEXEC);
        if(timerfd < 0){
            perror("Failed in timerfd_create");
            abort();
        }
        return timerfd;
    }
}
std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now){
    std::vector<Entry> expired;
    
    //构造一个极其特殊的 Entry，时间是 now，地址是内存里的最大值,这样可以把所有的 同一时刻，的所有任务取出来、
    // 因为地址最大，直到之间大于now ，二分搜索才会停止.
    Entry sentry(now,reinterpret_cast<Timer*>(UINTPTR_MAX));
    auto end = timers_.lower_bound(sentry);
    // end == timers_.end() 说明所有的闹钟都过期了。
    // now < it.first 留下来的闹钟一定时间一定大于now
    assert(end == timers_.end() || now < end->first);
    //std::back_inserter 边拷贝,后台边扩容。
    std::copy(timers_.begin(),end,std::back_inserter(expired));
    timers_.erase(timers_.begin(),end);
    return expired;
}

TimerId TimerQueue::addTimer(TimerCallback cb,Timestamp when,double interval){
    //从TimerQueue 生成的流水号
    uint64_t seq = nextId_.fetch_add(1);
    //创建定时器实体
    Timer* timer = new Timer(cb,when,interval,seq);
    /*
        外部业务线程随时可能调用 addTimer。为了绝对的线程安全，
        我们必须把“把闹钟塞进红黑树”这个动作，打包扔给 EventLoop 所在的 IO 线程去执行！
        addTimer只负责转发，交给 addTimerInLoop 完成修改定时器列表的工作
        bind 的作用就是：把“函数 + 对象 + 参数”提前绑好，生成一个以后可以直接调用的无参任务对象。
    */
    //loop_->runInLoop(std::bind(&TimerQueue::addTimerInLoop,this,timer));
    loop_->runInLoop([this,timer](){
        this->addTimerInLoop(timer);
    });
    return TimerId(timer,seq);
}
/*
    1、确保当前操作发生在 TimerQueue 所属的 loop 线程里
    2、把新的定时器插入到内部有序容器里
    3、如果这个新定时器变成了“最早到期的那个”，就更新 timerfd
    为什么要更新 timerfd?
    TimerQueue 内部一般会维护很多定时器，比如：
    5 秒后执行
    10 秒后执行
    20 秒后执行
    但内核那个 timerfd 一次只能代表“下一次什么时候响”。
    所以它通常只设置成：当前所有定时器里，最早到期的那个时间点。
*/
void TimerQueue::addTimerInLoop(Timer* timer){
    //判断是否是所属线程
    loop_->assertInLoopThread();
    // 把新定时器插入内部有序容器
    // 返回值表示：这个新定时器是否成为了“当前最早到期的定时器”
    bool earliestChanged = insert(timer);

    // timerfd 只负责等待“最近一次到期事件”
    // 如果新插入的 timer 变成了最早到期的那个，
    // 就必须重新设置 timerfd 的触发时间
    if(earliestChanged){
        resetTimerfd(timerfd_, timer->expiration());
    }
}

