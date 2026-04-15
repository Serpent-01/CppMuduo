#include <iostream>
#include <thread>

// 普通全局变量 - 所有线程共享
int global_counter = 0;

// thread_local变量 - 每个线程独立
thread_local int tls_counter = 0;

void thread_func(const char* name) {
    global_counter++;      // 线程间会互相影响
    tls_counter++;         // 每个线程独立增加
    
    std::cout << name 
              << ": global=" << global_counter 
              << ", tls=" << tls_counter << std::endl;
}

int main() {
    std::thread t1(thread_func, "线程A");
    std::thread t2(thread_func, "线程B");
    std::thread t3(thread_func, "线程C");
    
    t1.join();
    t2.join();
    t3.join();
    
    return 0;
}
