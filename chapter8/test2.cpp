#include "EventLoop.h"
#include <thread>

//(负面测试：验证断言崩溃)
EventLoop* g_loop = nullptr;

void threadFunc(){
    g_loop->loop();
}
int main(){
    EventLoop loop;
    g_loop = &loop;

    std::thread t(threadFunc);
    t.join();
    return 0;
}