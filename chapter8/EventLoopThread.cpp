#include "EventLoopThread.h"
#include "EventLoop.h"
#include <cassert>
#include <mutex>
#include <utility>

// EventLoopThread 的核心流程：
//
// 1. 主线程调用 startLoop()
// 2. startLoop() 启动一个新线程，执行 threadFunc()
// 3. threadFunc() 在子线程中创建局部 EventLoop 对象
// 4. 子线程把 EventLoop 地址写入 loop_，并通知主线程
// 5. 主线程等待到 loop_ 可用后，返回 EventLoop*
// 6. 子线程进入 loop.loop()，正式运行事件循环
//
// 关键点：
// - EventLoop 必须在所属线程中创建
// - loop_ 是非拥有指针，只是指向 threadFunc() 中的局部对象
// - mutex_ + condition_variable 用来保证 startLoop() 返回前 loop_ 已经准备好

EventLoopThread::EventLoopThread(ThreadInitCallback cb,std::string name)
    :callback_(std::move(cb)),
    name_(std::move(name)){}

EventLoopThread::~EventLoopThread(){
    exiting_ = true;
    EventLoop* loop = nullptr;
    {   
        //loop 是共享变量，需要先在锁保护下读取。
        std::lock_guard<std::mutex> lock(mutex_);
        loop = loop_;
    }
    //如果子线程中的 EventLoop还存在，就先请求它退出
    //否则子线程可能还阻塞在 loop.loop()中，join会一直等
    if(loop != nullptr){
        loop->quit();
    }
    //只要线程已经启动，就必须join
    //否则 std::thread 析构时若仍 joinable,会直接terminate
    if(thread_.joinable()){
        thread_.join();
    }
}

EventLoop* EventLoopThread::startLoop(){
  
    /*  
        防止重复启动同一个 EventLoopThread
        如果 thread_.joinable() 为 true
        说明这个 std::thread 已经关联了一个真实线程
        也就是说，这个 EventLoopThread 已经启动过了
        同一个 EventLoopThread 对象只能启动一次。
    */
    assert(!thread_.joinable());

    /*
        创建一个新的线程
        在线程里执行成员函数 EventLoopThread::threadFunc
        this 作为对象指针传进去
        主线程现在把 threadFunc() 扔到另一个线程里运行了。
        注意，这一句执行完后：
            线程已经启动
            但不代表 threadFunc() 已经执行完
            甚至不代表子线程一定已经跑到创建 EventLoop 那一步了
            std::thread 创建成功 ≠ 子线程里的资源已经准备好了
            所以后面必须等待。
    */
    thread_ = std::thread(&EventLoopThread::threadFunc,this);

    /*  
        准备接收最终结果。
        后面会把 loop_ 读出来，放到这个局部变量里，然后返回。
        这么写的好处是：
            返回值更清晰
            不直接在锁保护范围外访问共享变量 loop_
            让代码结构更规范
    */
    EventLoop* loop = nullptr;
    {   
        //这里要加锁，是因为 loop_ 是共享变量。
        /*
            谁会访问它？
                子线程：创建完 EventLoop 后，会写入 loop_
                主线程：在这里等待并读取 loop_
            所以这是典型的线程共享数据，必须同步保护。
            这里用的是 std::unique_lock，而不是 std::lock_guard，原因很重要：
            条件变量 cond_.wait(...) 需要配合 unique_lock 使用。


            因为 wait() 的过程不是简单地“睡着”，它内部会做两件事：
                1、先自动释放锁
                2、被唤醒后再重新加锁
        */
        std::unique_lock<std::mutex> lock(mutex_);
        //主线程先睡在这里，直到子线程把 loop_ 设置好为止。
        cond_.wait(lock,[this]{return loop_ != nullptr;});
        loop = loop_;
    }
    return loop;
}

void EventLoopThread::threadFunc(){
    //EventLoop 必须在所属线程中创建
    //这样它记录的线程归属才是正确的.
    EventLoop loop;

    /*
      如果用户提供了初始化回调
      就是线程进入loop.loop()之前执行。
      这是做线程初始化最合适的时机
      EventLoop 已经创建好了，但事件循环还没开始跑。
    */
    if(callback_){
        callback_(&loop);
    }
    
    {   
        //把子线程中的 EventLoop 地址发布给主线程
        //然后他通知子线程 startLoop();现在可以返回了。
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = &loop;
    }
    //唤醒主线程。
    cond_.notify_one();

    // 子线程正式进入事件循环。
    // 从这里开始，这个线程才真正成为一个 IO 线程。
    loop.loop();

    // loop.loop() 退出后，说明该线程的事件循环结束了。
    // 在离开线程函数前，把共享指针清空，避免外部再访问悬空地址。
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = nullptr;
    }

}