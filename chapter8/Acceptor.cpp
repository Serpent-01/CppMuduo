#include "Acceptor.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include "SocketsOps.h"

#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

Acceptor::Acceptor(EventLoop* loop,const InetAddress& listenAddr,bool reuseport):loop_(loop),
    /**
       * 创建一个非阻塞监听 socket。
       *
       * 为什么要非阻塞？
       * 因为 muduo 是 Reactor + 非阻塞 IO 模型，
       * 所有 fd 都应该尽量工作在非阻塞模式下。
       *
       * listenAddr.family() 一般就是 AF_INET / AF_INET6，
       * 表示根据传入地址选择 IPv4 或 IPv6。
       */
    acceptSocket_(sockets::createNonblockingOrDie(listenAddr.family())),


    /**
       * 为 listenfd 创建对应的 Channel。
       *
       * Channel 的本质是：
       * “fd + 关心的事件 + 事件发生后的回调”
       *
       * 这里这个 Channel 专门对应监听 socket。
       *
       * 注意：
       * Channel 自己不做 epoll_wait/poll，
       * 它只是描述：
       * - 我是哪一个 fd
       * - 我关心什么事件
       * - 事件来了回调谁
       *
       * 真正底层的事件检测，最终还是由 EventLoop/Poller 完成。
       */
    acceptChannel_(loop,acceptSocket_.fd()),


    /**
       * idleFd_ 是 muduo 里一个很经典的小技巧。
       *
       * 它提前打开一个 /dev/null，
       * 平时先占住一个 fd，
       * 当进程 fd 用尽（EMFILE）时，
       * 可以临时把这个 fd 释放出来，
       * 然后 accept 一个已经到来的连接并立刻 close，
       * 这样可以让客户端尽快感知“连接失败”，
       * 而不是一直卡在监听队列里。
       *
       * O_CLOEXEC 表示 exec 新程序时自动关闭，
       * 避免 fd 泄漏到子进程。
       */
    idleFd_(::open("/dev/null",O_RDONLY | O_CLOEXEC)) {

    /**
     * 基本健壮性检查。
     * loop_ 不能为空，因为 Acceptor 必须归属于某个 EventLoop。
     */
    assert(loop_!=nullptr);
    assert(idleFd_ >= 0);
    /**
     * 设置 SO_REUSEADDR。
     *
     * 作用：
     * 让服务器重启时，更容易重新绑定之前使用过的地址。
     * 否则端口可能因为 TIME_WAIT 等状态暂时无法复用。
     */
    acceptSocket_.setReuseAddr(true);



    /**
     * 设置 SO_REUSEPORT（是否开启由外部参数 reuseport 决定）。
     *
     * 作用：
     * 允许多个 socket 绑定到同一个 ip:port。
     *
     * 在多线程/多进程高性能服务里常用于负载分摊。
     * 但不是所有场景都需要，所以交给调用者决定。
     */
    acceptSocket_.setReusePort(reuseport);


    /**
     * 给监听 socket 绑定地址和端口。
     *
     * 到这里为止，相当于完成了：
     *   socket()
     *   setsockopt(...)
     *   bind(...)
     *
     * 但还没有 listen()。
     *
     * 为什么构造函数里只 bind，不直接 listen？
     * 因为 muduo 把“创建对象”和“正式开始监听”分开了：
     * - 构造函数：把对象和资源准备好
     * - listen()：真正让它开始工作
     *
     * 这样更清晰，也更符合“先初始化，再启动”的生命周期设计。
     */
    acceptSocket_.bindAddress(listenAddr);


     /**
     * 给 acceptChannel_ 设置“读事件回调”。
     *
     * 对于 listenfd 来说，“可读”并不是普通意义上的“有数据可读”，
     * 而是表示：
     *   有新的客户端连接到来了，可以调用 accept() 了。
     *
     * 所以这里的语义就是：
     *   一旦 listenfd 变得可读，就执行 handleRead()。
     *
     * 这里用 lambda 捕获 this，
     * 等价于旧写法：
     *   std::bind(&Acceptor::handleRead, this)
     */
    acceptChannel_.setReadCallback([this]{handleRead();});
}
Acceptor::~Acceptor(){
     /**
     * 先让 Channel 停止关注任何事件。
     *
     * 这一步的意思不是“关闭 socket”，
     * 而是先从事件关注层面告诉 EventLoop/Poller：
     *   我这个 Channel 不再关心读/写/错误等任何事件了。
     *
     * 相当于先把“订阅”取消。
     */
    acceptChannel_.disableAll();

     /**
     * 再把这个 Channel 从 EventLoop / Poller 中移除。
     *
     * 为什么要 remove？
     * 因为 Poller 内部通常维护着：
     *   fd -> Channel
     * 的映射关系，
     * 如果对象析构前不 remove，
     * Poller 里可能还残留这个 Channel 的记录，
     * 后续就可能出现悬空引用。
     *
     * 所以顺序一般是：
     * 1. disableAll() 取消事件关注
     * 2. remove()     从 Reactor 体系中摘掉
     */
    acceptChannel_.remove();
    /**
     * 最后关闭 idleFd_。
     *
     * 这是构造函数里打开的“预留 fd”，
     * 析构时当然要回收。
     */
    if(idleFd_ >=0 ){
        ::close(idleFd_);
        idleFd_ = -1;
    }
}

void Acceptor::listen(){
    loop_->assertInLoopThread();
}