#include "TimerQueue.h"
#include "EventLoop.h"
#include "Channel.h"
#include "Timer.h"
#include "Timestamp.h"

#include <ctime>
#include <iterator>
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
