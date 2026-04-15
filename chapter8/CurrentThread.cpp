#include "CurrentThread.h"
#include <sys/syscall.h>
#include <unistd.h>

namespace CurrentThread{
    //变量的真正定义，初始值为 0
    thread_local pid_t t_cacheTid = 0;

    void cacheTid(){
        if(t_cacheTid == 0){
            t_cacheTid = static_cast<pid_t>(::syscall(SYS_gettid));
        }
    }
}