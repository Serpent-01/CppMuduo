#include "EventLoop.h"
#include "CurrentThread.h"
#include <thread>
#include <cstdio>
#include <unistd.h>
//正面测试：正常运行并休眠退出
void threadFunc(){
    printf("threadFunc(): pid = %d,tid = %d\n",gettid(),CurrentThread::tid());
    EventLoop loop;
    loop.loop();
}
int main(){
    printf("main(): pid = %d, tid = %d\n", getpid(), CurrentThread::tid());
    EventLoop loop;
    std::thread t(threadFunc);
    loop.loop(); 

    t.join();
    return 0;
}