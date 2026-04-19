#include "EventLoop.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <mutex>

#include "Channel.h"
#include "CurrentThread.h"
#include "Poller.h"
#include "Timer.h"
#include "TimerQueue.h"
#include "Timestamp.h"
// 每个线程各自拥有一份 t_loopInThisThread。
// 它的意义是：保证 one loop per thread。
namespace {
    thread_local EventLoop* t_loopInThisThread = nullptr;
}

EventLoop* EventLoop::getEventLoopOfCurrentThread() noexcept {
    return t_loopInThisThread;
}

EventLoop::EventLoop()
    : threadId_(CurrentThread::tid()),
      poller_(std::make_unique<Poller>(this)) {
    std::cout << "[EventLoop] created " << this
              << " in thread " << threadId_ << '\n';

    // 同一个线程里不允许创建第二个 EventLoop
    if (t_loopInThisThread != nullptr) {
        std::cerr << "[EventLoop] fatal: another EventLoop("
                  << t_loopInThisThread
                  << ") already exists in this thread\n";
        std::abort();
    }

    t_loopInThisThread = this;
}

EventLoop::~EventLoop() {
    // 退出前必须保证 loop 已经停止
    assert(!looping_);
    t_loopInThisThread = nullptr;
}

void EventLoop::loop() {
    // 不允许重复进入 loop
    assert(!looping_);

    // EventLoop 只能在它所属线程里运行
    assertInLoopThread();

    looping_ = true;
    quit_ = false;

    // 这就是最小的“真正事件循环”：
    // 1. poll 等待事件
    // 2. 收集活跃 Channel
    // 3. 依次处理 Channel 上的事件
    while (!quit_) {
        activeChannels_.clear();

        // timeout 这里给 10 秒，方便演示。
        // 没有事件时，poll 会阻塞等待。
        pollReturnTime_ = poller_->poll(10'000, &activeChannels_);
        std::cerr << pollReturnTime_.toString() << std::endl;
        // 遍历本轮活跃 Channel，分发事件
        for (Channel* channel : activeChannels_) {
            assert(channel != nullptr);
            channel->handleEvent();
        }
        doPendingFunctors();
    }

    looping_ = false;
    std::cout << "[EventLoop] " << this << " stopped looping\n";
}

void EventLoop::quit() noexcept {
    quit_ = true;
    if(!isInLoopThread()){
        wakeup();
    }
}

void EventLoop::updateChannel(Channel* channel) {
    assert(channel != nullptr);

    // 只有所属线程才能改 Poller 里的监听状态
    assertInLoopThread();

    // 真正干活的是 Poller
    poller_->updateChannel(channel);
}

void EventLoop::assertInLoopThread() const {
    if (!isInLoopThread()) {
        abortNotInLoopThread();
    }
}

bool EventLoop::isInLoopThread() const noexcept {
    return threadId_ == CurrentThread::tid();
}

TimerId EventLoop::runAt(const Timestamp& time,const TimerCallback& cb){
    //interval 这里传的是 0 表示不仅周期触发
    return timerQueue_->addTimer(cb, time, 0);
}
TimerId EventLoop::runAfter(double delay,const TimerCallback& cb){
    Timestamp time(addTime(Timestamp::now(),delay));
    return runAt(time, cb);
}

TimerId EventLoop::runEvery(double interval, const TimerCallback &cb){
    Timestamp time(addTime(Timestamp::now(), interval));
    return timerQueue_->addTimer(cb, time, interval);
}

void EventLoop::runInLoop(Functor cb){
    if(isInLoopThread()){
        cb();
    }else{
        queueInLoop(std::move(cb));
    }
}



// 把任务加入 EventLoop 的待执行队列。
//
// 这个函数可能被其他线程调用，因此访问 pendingFunctors_ 时要加锁。
// 如果当前不是 loop 所在线程，说明 EventLoop 可能正阻塞在 poll/epoll，
// 此时需要调用 wakeup() 把它唤醒，让它尽快执行新加入的任务。
//
// 即使当前就是 loop 线程，但如果正在执行 doPendingFunctors()，
// 也仍然需要 wakeup()，避免新加入的任务被拖到太久以后才处理。
void EventLoop::queueInLoop(Functor cb){
    {
        //只要离开作用域，锁会自动释放，即使中间抛异常也没事。
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }
    if(!isInLoopThread() || callingPendingFunctors_){
        wakeup();
    }
}


// 执行待处理任务队列中的所有回调。
//
// 为什么这里要先定义一个局部 vector functors，再和 pendingFunctors_ 交换？
// 原因有两个：
//
// 1. 缩短加锁时间
//    pendingFunctors_ 可能会被其他线程通过 queueInLoop() 并发加入新任务，
//    所以访问它时必须加锁。
//    但真正执行 functor() 可能比较慢，不能一直持有锁，否则其他线程会被阻塞。
//
// 2. 避免一边遍历，一边修改同一个 vector
//    某个 functor() 执行过程中，可能再次调用 queueInLoop()，
//    从而往 pendingFunctors_ 里继续加入新任务。
//    如果我们直接遍历 pendingFunctors_，就可能导致容器在遍历期间被修改，逻辑混乱。
//
// 因此这里的做法是：
// - 先加锁
// - 用 swap() 把 pendingFunctors_ 当前这批任务整体取出来
// - 立即解锁
// - 然后在无锁状态下执行这批任务
//
// swap 之后：
// - functors 拿到旧的待执行任务
// - pendingFunctors_ 变为空，后续新任务可以继续安全加入
//
// callingPendingFunctors_ 用来表示当前是否正在执行这批待处理任务。
// 如果执行期间又有新任务进入，可以配合 queueInLoop() 中的 wakeup()，
// 让 EventLoop 尽快处理下一批任务。
void EventLoop::doPendingFunctors(){
    //局部临时队列：用于接管当前这一批待执行任务
    std::vector<Functor> fucntors;
    //当前正在执行 pendingFunctors_
    callingPendingFunctors_ = true;
    {   
        //加锁保护共享队列  pendingFunctors_
        std::lock_guard<std::mutex> lock(mutex_);
        // 把共享队列中的任务整体交换到本地 functors 中
        // 这样 pendingFunctors_ 立刻变空，其他线程可以继续往里放新任务
        fucntors.swap(pendingFunctors_);
    }
    // 在无锁状态下执行任务，避免长时间持有锁
    for(const Functor & functor : fucntors){
        functor();
    }
    callingPendingFunctors_ = false;
}



[[noreturn]] void EventLoop::abortNotInLoopThread() const {
    std::cerr << "[EventLoop] wrong thread: loop was created in thread "
              << threadId_
              << ", current thread is "
              << CurrentThread::tid()
              << '\n';
    std::abort();
}