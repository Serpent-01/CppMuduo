#include "TimerQueue.h"
#include "EventLoop.h"
#include "Channel.h"
#include "Timer.h"

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
std::vector<TimerQueue::Entry> TimerQueue::getExpired(TimePoint now){
    std::vector<Entry> expired;
    
}