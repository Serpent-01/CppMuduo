#ifndef MINI_REACTOR_CURRENT_THREAD_H
#define MINI_REACTOR_CURRENT_THREAD_H

#include <thread>

// 对应书里的 CurrentThread::tid()。
// 书里通常会缓存 Linux 线程 tid，这里为了现代 C++ 和可读性，
// 直接使用 std::this_thread::get_id()。
namespace CurrentThread {

inline std::thread::id tid() noexcept {
    return std::this_thread::get_id();
}

}  // namespace CurrentThread

#endif  // MINI_REACTOR_CURRENT_THREAD_H