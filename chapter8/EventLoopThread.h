#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class EventLoop;
/**
 * EventLoopThread 的作用：
 * 1. 启动一个新线程
 * 2. 在线程函数里创建一个 EventLoop
 * 3. 让这个 EventLoop 在该线程中运行
 * 4. 把这个线程中的 EventLoop* 返回给外部使用
 *
 * 这是 one loop per thread 模型的关键工具类。
 */
class EventLoopThread {
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

     explicit EventLoopThread(ThreadInitCallback cb = ThreadInitCallback(),
                             std::string name = std::string());

    ~EventLoopThread();

     /**
     * 启动新线程，并返回该线程中的 EventLoop*
     *
     * 注意：
     * startLoop() 返回前，必须保证子线程中的 EventLoop
     * 已经创建完成，否则外部拿到的可能是空指针。
     */
    EventLoop* startLoop();

private:
    /**
     * 子线程入口函数。
     *
     * 在这个函数中：
     * 1. 创建局部 EventLoop 对象
     * 2. 可选执行线程初始化回调
     * 3. 把 loop 地址发布给主线程
     * 4. 进入事件循环
     *
     * 为什么 EventLoop 必须在这里创建？
     * 因为 EventLoop 是线程归属对象，
     * 它必须在所属线程中构造。
     */
    void threadFunc();

private:
    // 指向子线程中的 EventLoop。
    // 它只是一个“非拥有指针”，并不负责释放对象。
    // 真正的 EventLoop 对象在 threadFunc() 的栈上创建。
    EventLoop* loop_{nullptr};

    // 析构退出标志
    bool exiting_{false};

    // 真正的线程对象
    std::thread thread_;

    // 保护共享变量 loop_
    std::mutex mutex_;

    // 用于让主线程等待，直到子线程中的 EventLoop 创建完成
    std::condition_variable cond_;

    // 可选：在线程进入 loop.loop() 之前执行初始化逻辑
    ThreadInitCallback callback_;

    // 线程名字（可用于日志/调试）
    std::string name_;
};