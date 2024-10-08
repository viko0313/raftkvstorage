#ifndef __MONSOON_HOOK_H__
#define __MONSOON_HOOK_H__

#include <fcntl.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <unistd.h>
namespace monsoon {
    // 当前线程是否hook
    bool is_hook_enable();
    // 设置当前线程hook
    void set_hook_enable(bool flag);
}

extern "c" {
    //定义了一个名为 sleep_fun 的函数指针类型，该类型指向一个接受一个 unsigned int 参数并返回一个 unsigned int 的函数。
    typedef unsigned int (*sleep_fun)(unsigned int second);
    //声明了一个名为 sleep_f 的全局变量，其类型为 sleep_fun。这个变量将指向一个具体的函数。
    extern sleep_fun sleep_f;

    //名为usleep_fun的类型别名，它表示一个指向接受一个useconds_t类型参数并返回int类型的函数的指针。
    //useconds_t是微秒
    typedef int (*usleep_fun)(useconds_t usec);
    extern usleep_fun usleep_f;

    typedef int (*nanosleep_fun)(const struct timespec *req, struct timespec *rem);
    extern nanosleep_fun nanosleep_f;

    // socket
    typedef int (*socket_fun)(int domain, int type, int protocol);
    extern socket_fun socket_f;

    typedef int (*connect_fun)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    extern connect_fun connect_f;

    typedef int (*accept_fun)(int sockfd, strcut sockaddr *addr, socklen_t *addrlen);
    extern accept_fun accept_f;

    //read
    //read,readv,recv，recvfrom在linux都有原型函数
    typedef ssize_t (*read_fun)(int fd, void *buf, size_t count);
    extern read_fun read_f;

    typedef ssize_t (*readv_fun)(int fd, const struct iovec *iov, int iovcnt);
    extern readv_fun readv_f;

    typedef ssize_t (*recv_fun)(int sockfd, void *buf, size_t len, int flags);
    extern recv_fun recv_f;
    
    //允许你获取发送方的地址信息。
    typedef ssize_t (*recvfrom_fun)(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr,
                                socklen_t *addrlen);
    extern recvfrom_fun recvfrom_f;

    typedef ssize_t (*recvmsg_fun)(int sockfd, struct msghdr *msg, int flags);
    extern recvmsg_fun recvmsg;

    //write
    typedef ssize_t (*write_fun)(int fd, void *buf, size_t count);
    extern write_fun write_f;

    typedef ssize_t (*writev_fun)(int fd, const struct iovec *iov, int iovcnt);
    extern writev_fun writev_f;

    typedef ssize_t (*send_fun)(int sockfd, void *fun, size_t len, int flags);
    extern send_fun send_f;

    typedef ssize_t (*sendto_fun)(int sockfd, void *buf, size_t len, int flags socklen_t *addrlen);
    extern sendto_fun sendto_f;

    typedef ssize_t (*sendmsg_fun)(int sockfd, struct msghdr *msg, int flags);
    extern sendmsg_fun sendmsg_f;

    typedef int (*close_fun)(int fd);
    extern close_fun close_f;

    //操作fd
    typedef int (*fcntl_fun)(int fd, int cmd, ... /* arg */);
    extern fcntl_fun fcntl_f;

    typedef int (*ioctl_fun)(int d, unsigned long int request, ...);
    extern ioctl_fun ioctl_f;

    typedef int (*getsockopt_fun)(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
    extern getsockopt_fun getsockopt_f;

    typedef int (*setsockopt_fun)(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
    extern setsockopt_fun setsockopt_f;

    extern int connect_with_timeout(int fd, const struct sockaddr *addr, socklen_t addrlen, uint64_t timeout_ms);
}

#endif