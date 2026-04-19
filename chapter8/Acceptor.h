#pragma once


#include "Channel.h"
#include <functional>
#include <utility>
#include "Socket.h"
namespace muduo::net {


class EventLoop;
class InetAddress;

/*
    Acceptor
    1、持有监听 socket(listenfd)
    2、用acceptChannel_ 监听 listenfd的可读事件
    3、当一个listenfd可读时，调用accept()接收一个新连接
    4、再把这个新连接通过回调函数交给上层(通常是TcpServer)
    注意 ： 这里监听是交给Poller，Acceptor只负责建立新连接
    监听 socket 的创建、注册、accept 处理，这条接入链路由 `Acceptor` 封装；其中 listenfd 的事件检测由 `Poller` 完成
*/

class Acceptor{
public:
    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress & perrAddress)>;
    
    Acceptor(EventLoop* loop,const InetAddress& listenAddr,bool reuseport);
    ~Acceptor();
    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;
    
    // 按值接收再 move
    void setNewConnectionCallback(NewConnectionCallback cb){
        newConnectionCallback_ = std::move(cb);
    }
    
    // 进入监听状态：
    // 1. 对 listenfd 调用 listen()
    // 2. 让 acceptChannel_ 关注 listenfd 的读事件
    // 3. 通过 EventLoop/Poller 将该读事件注册到 IO 多路复用器
    void listen();

    bool listening() const noexcept{
        return listening_;
    }
private:
    //当 listenfd可读触发
    //这通常意味着有新连接正在等待accept()
    void handleRead();
private:
    EventLoop* loop_;
    Socket acceptSocket_;
    Channel acceptChannel_;
    NewConnectionCallback newConnectionCallback_;
    bool listening_{false};
    int idleFd_{-1};
};

}//namespace muduo::net    
