#include "hook.hpp"

#include <dlfcn.h>
#include <cstdarg>
#include <string>

#include "fd_manager.hpp"
#include "iomanager.hpp"
#include "fiber.hpp"

namespace monsoon {

    static thread_local bool t_hook_enable = false;
    static int g_tcp_connect_timeout = 5000;

//宏列表定义了一系列与网络 IO、文件操作、时间延迟相关的函数，在被调用时，如果启用了钩子功能（由t_hook_enable控制），则会执行自定义的逻辑，而非直接调用原始的系统库函数。
#define HOOK_FUN(XX)   \
    XX(sleep)          \
    XX(usleep)         \
    XX(nanosleep)      \
    XX(socket)         \
    XX(connect)        \
    XX(accept)         \
    XX(read)           \
    XX(readv)          \
    XX(recv)           \
    XX(recvfrom)       \
    XX(recvmsg)        \
    XX(write)          \
    XX(writev)         \
    XX(send)           \
    XX(sendto)         \
    XX(sendmsg)        \
    XX(close)          \
    XX(fcntl)          \
    XX(ioctl)          \
    XX(getsockopt)     \
    XX(setsockopt)

//XX是一个占位符，每次宏调用时，它会被宏参数列表中的实际函数名替换。例如，使用这个宏定义后，
//可能会在后续代码中看到对每个列出函数的预处理展开，生成针对每个函数的钩子实现代码

void hook_init() {
  static bool is_inited = false;
  if (is_inited) {
    return;
  }
  // dlsym:Dynamic LinKinf Library.返回指定符号的地址
#define XX(name) name##_f = (name##_fun)dlsym(RTLD_NEXT, #name);
  HOOK_FUN(XX);
#undef XX
}


// hook_init放在静态对象中，则在main函数执行之前就会获取各个符号地址并
// 保存到全局变量中
static uint64_t s_connect_timeout = -1;
struct _HOOKIniter {
  _HOOKIniter() {
    hook_init();
    s_connect_timeout = g_tcp_connect_timeout;
    // std::cout << "hook init success" << std::endl;
  }
};

static _HOOKIniter s_hook_initer;

bool is_hook_enable() { return t_hook_enable; }

void set_hook_enable(const bool flag) { t_hook_enable = flag; }

struct timer_info {
  int cnacelled = 0;
};

//增强标准 I/O 操作，使其支持超时、异步通知以及与事件循环
template <typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char *hook_fun_name, uint32_t event, int timeout_so, Args &&...args) {
  // 如果全局钩子开关未启用，直接调用原始I/O函数并返回结果
  if (!t_hook_enable) {
    return fun(fd, std::forward<Args>(args)...);
  }

  // 获取当前文件描述符的上下文信息
  FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
  if (!ctx) {
    // 上下文不存在，直接调用原始I/O函数
    return fun(fd, std::forward<Args>(args)...);
  }

  // 检查文件描述符是否已关闭，已关闭则设置errno为EBADF并返回-1
  if (ctx->isClose()) {
    errno = EBADF;
    return -1;
  }

  // 非socket或用户设置了非阻塞标志，直接调用原始I/O函数
  if (!ctx->isSocket() || ctx->getUserNonblock()) {
    return fun(fd, std::forward<Args>(args)...);
  }

  // 获取对应type的fd超时时间
  uint64_t to = ctx->getTimeout(timeout_so);

  // 创建一个计时器信息共享指针
  std::shared_ptr<timer_info> tinfo(new timer_info);

retry:
  // 尝试执行I/O操作，若因信号中断(-1且errno==EINTR)则重试
  ssize_t n = fun(fd, std::forward<Args>(args)...);
  while (n == -1 && errno == EINTR) {
    n = fun(fd, std::forward<Args>(args)...);
  }

  // 若操作未就绪(EAGAIN)，则进行异步处理
  if (n == -1 && errno == EAGAIN) {
    IOManager *iom = IOManager::GetThis(); // 获取当前IO管理器实例

    // 设置超时计时器，若超时则取消对应事件
    Timer::ptr timer;
    if (to != (uint64_t)-1) {
      timer = iom->addConditionTimer(to, [winfo = std::weak_ptr<timer_info>(tinfo), fd, iom, event]() {
        auto t = winfo.lock();
        if (t && !t->cancelled) {
          t->cancelled = ETIMEDOUT;
          iom->cancelEvent(fd, (Event)event);
        }
      }, tinfo);
    }

    // 添加读/写事件到IO管理器
    int rt = iom->addEvent(fd, (Event)event);
    if (rt) {
      // 添加事件失败，取消计时器并返回错误
      std::cout << hook_fun_name << " addEvent(" << fd << ", " << event << ")";
      if (timer) {
        timer->cancel();
      }
      return -1;
    } else {
      // 当前线程（Fiber）让出执行权
      Fiber::GetThis()->yield();

      // 取消计时器（如果存在）
      if (timer) {
        timer->cancel();
      }

      // 检查是否因超时被取消，如果是则设置errno并返回-1
      if (tinfo->cancelled) {
        errno = tinfo->cancelled;
        return -1;
      }

      // 重新尝试I/O操作
      goto retry;
    }
  }

  // 成功或有数据传输，返回实际传输大小
  return n;
}

extern "C" {
#define XX(name) name##_fun name##_f = nullptr;
HOOK_FUN(XX);
#undef XX
//初始化为nullptr稍后可以通过其他方式（如dlsym）来为它们赋予实际的函数地址。
//调用之前定义的HOOK_FUN宏，它会展开为一系列函数指针的声明和初始化，如sleep_fun sleep_f = nullptr;，usleep_fun usleep_f = nullptr;


//你需要提供一个自己的sleep函数定义，这个函数内部会决定
//是否调用原始的sleep函数（现在通过sleep_f指针访问）或者在调用前后添加额外的逻辑。
//可以先判断!t_hook_enabl或者反过来也可以
unsigned int sleep(unsigned int seconds) {
  // std::cout << "HOOK SLEEP" << std::endl;
  if (!t_hook_enable) {
    // 不允许hook,则直接使用系统调用
    return sleep_f(seconds);
  }
  // 允许hook,则直接让当前协程退出，seconds秒后再重启（by定时器）
  Fiber::ptr fiber = Fiber::GetThis();
  IOManager *iom = IOManager::GetThis();
  iom->addTimer(seconds * 1000,
                std::bind((void(Scheduler::*)(Fiber::ptr, int thread)) & IOManager::scheduler, iom, fiber, -1));
  Fiber::GetThis()->yield();
  return 0;
}

int usleep(useconds_t usec) {
    if (!t_hook_enable) {
        // 不允许hook,则直接使用系统调用
        auto ret = usleep_f(seconds);
        return 0;
    }
    // 允许hook,则直接让当前协程退出，seconds秒后再重启（by定时器）
    Fiber::ptr fiber = Fiber::GetThis();
    IOManager *iom = IOManager::GetThis();
    iom->addTimer(usec / 1000,
                    std::bind((void(Scheduler::*)(Fiber::ptr, int thread)) & IOManager::scheduler, iom, fiber, -1));
    Fiber::GetThis()->yield();
    return 0;
}

// nanosleep 在指定的纳秒数内暂停当前线程的执行
int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!t_hook_enable) {
        // 不允许hook,则直接使用系统调用
        return nanosleep_f(req, rem);
    }
    // 允许hook,则直接让当前协程退出，seconds秒后再重启（by定时器）
    Fiber::ptr fiber = Fiber::GetThis();
    IOManager *iom = IOManager::GetThis();
    int timeout_s = req->tv_sec * 1000 + req->tv_nsec / 1000 / 1000;
    //addtimer要的是ms，所以sec*1000, ns除6个0
    iom->addTimer(timeout_s,
                    std::bind((void(Scheduler::*)(Fiber::ptr, int thread)) & IOManager::scheduler, iom, fiber, -1));
    Fiber::GetThis()->yield();
    return 0;
}

int socket(int domain, int type, int protocol) {
    // std::cout << "HOOK SOCKET" << std::endl;
    if (!t_hook_enable) {
        return socket_f(domain, type, protocol);
    }
    int fd = socket_f(domain, type, protocol);
    if (fd == -1) {
        return fd;
    }
    // 将fd加入Fdmanager中
    FdMgr::GetInstance()->get(fd, true);
    return fd;
}

//可以设定最长等待时间，避免了长时间阻塞的问题。同时，它利用了事件驱动模型（通过IOManager和Fiber），提高了异步处理的效率
int connect_with_timeout(int fd, const struct sockaddr *addr, socklen_t addrlen, uint64_t timeout_ms) {
  // 如果全局钩子开关未启用，则直接调用原始connect函数
  if (!t_hook_enable) {
    return connect_f(fd, addr, addrlen);
  }

  // 获取文件描述符上下文
  FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
  if (!ctx || ctx->isClose()) {
    // 上下文不存在或文件描述符已关闭，设置errno为EBADF并返回-1
    errno = EBADF;
    return -1;
  }

  // 非socket或已设置用户非阻塞模式，直接调用原始connect
  if (!ctx->isSocket() || ctx->getUserNonblock()) {
    return connect_f(fd, addr, addrlen);
  }

  // 尝试执行connect调用
  int n = connect_f(fd, addr, addrlen);
  if (n == 0) {
    // 连接成功立即返回
    return 0;
  } else if (n != -1 || errno != EINPROGRESS) {
    // 其他错误直接返回
    return n;
  }

  // 进入到这里说明connect处于非阻塞且未完成状态(EINPROGRESS)
  IOManager *iom = IOManager::GetThis(); // 获取当前IO管理器实例
  Timer::ptr timer;                      // 计时器指针
  std::shared_ptr<timer_info> tinfo(new timer_info); // 计时器信息
  std::weak_ptr<timer_info> winfo(tinfo);           // 弱引用，用于定时器回调

  // 设置超时计时器
  if (timeout_ms != (uint64_t)-1) {
    timer = iom->addConditionTimer(timeout_ms, [winfo, fd, iom]() {
      auto t = winfo.lock();
      if (t && !t->cancelled) {
        t->cancelled = ETIMEDOUT;
        iom->cancelEvent(fd, WRITE); // 超时后取消WRITE事件
      }
    }, tinfo);
  }

  // 注册WRITE事件，准备监听可写事件
  int rt = iom->addEvent(fd, WRITE);
  if (rt == 0) {
    // 让当前Fiber暂停，等待事件触发
    Fiber::GetThis()->yield();
    // 处理超时或事件完成后的逻辑
    if (timer) {
      timer->cancel(); // 取消计时器
    }
    if (tinfo->cancelled) {
      // 超时，设置errno并返回-1
      errno = tinfo->cancelled;
      return -1;
    }
  } else {
    // 添加事件失败
    if (timer) {
      timer->cancel();
    }
    std::cout << "connect addEvent(" << fd << ", WRITE) error" << std::endl;
  }

  // 检查连接状态
  int error = 0;
  socklen_t len = sizeof(int);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1) {
    return -1; // 获取错误状态失败
  }
  if (!error) {
    return 0; // 连接成功
  } else {
    errno = error; // 设置错误码
    return -1;     // 连接失败
  }
}
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  return monsoon::connect_with_timeout(sockfd, addr, addrlen, s_connect_timeout);
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
    int fd = do_io(s, accept_f, "accept", READ, SO_RCVTIMEO, addr, addrlen);
    if (fd >= 0) {
        FdMgr::GetInstance()->get(fd, true);
    }
    return fd;
}

ssize_t read(int fd, void *buf, size_t count) { 
    return do_io(fd, read_f, "read", READ, SO_RCVTIMEO, buf, count); 
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    return do_io(sockfd, recv_f, "recv", READ, SO_RCVTIMEO, buf, len, flags);
}
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
    return do_io(sockfd, recvfrom_f, "recvfrom", READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    return do_io(sockfd, recvmsg_f, "recvmsg", READ, SO_RCVTIMEO, msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return do_io(fd, write_f, "write", WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, writev_f, "writev", WRITE, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int s, const void *msg, size_t len, int flags) {
    return do_io(s, send_f, "send", WRITE, SO_SNDTIMEO, msg, len, flags);
}

ssize_t sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
    return do_io(s, sendto_f, "sendto", WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
}

ssize_t sendmsg(int s, const struct msghdr *msg, int flags) {
     return do_io(s, sendmsg_f, "sendmsg", WRITE, SO_SNDTIMEO, msg, flags);
}

int close(int fd) {
    if (!t_hook_enable) {
        return close_f(fd);
    }

    FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
    if (ctx) {
        auto iom = IOManager::GetThis();
        if (iom) {
        iom->cancelAll(fd);
        }
        FdMgr::GetInstance()->del(fd);
    }
    //先清理资源再调用原生close
    return close_f(fd);
}

//主要处理F_SETFL和F_GETFL命令，用于设置和获取文件描述符的标志（如O_NONBLOCK）
int fcntl(int fd, int cmd, ... /* arg */) {
  va_list va;
  va_start(va, cmd);
  switch (cmd) {
    case F_SETFL: {
      int arg = va_arg(va, int);
      va_end(va);
      FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
      if (!ctx || ctx->isClose() || !ctx->isSocket()) {
        return fcntl_f(fd, cmd, arg);
      }
      ctx->setUserNonblock(arg & O_NONBLOCK);
      if (ctx->getSysNonblock()) {
        arg |= O_NONBLOCK;
      } else {
        arg &= ~O_NONBLOCK;
      }
      return fcntl_f(fd, cmd, arg);
    } break;
    case F_GETFL: {
      va_end(va);
      int arg = fcntl_f(fd, cmd);
      FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
      if (!ctx || ctx->isClose() || !ctx->isSocket()) {
        return arg;
      }
      if (ctx->getUserNonblock()) {
        return arg | O_NONBLOCK;
      } else {
        return arg & ~O_NONBLOCK;
      }
    } break;
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
    case F_SETFD:
    case F_SETOWN:
    case F_SETSIG:
    case F_SETLEASE:
    case F_NOTIFY:
#ifdef F_SETPIPE_SZ
    case F_SETPIPE_SZ:
#endif
    {
      int arg = va_arg(va, int);
      va_end(va);
      return fcntl_f(fd, cmd, arg);
    } break;
    case F_GETFD:
    case F_GETOWN:
    case F_GETSIG:
    case F_GETLEASE:
#ifdef F_GETPIPE_SZ
    case F_GETPIPE_SZ:
#endif
    {
      va_end(va);
      return fcntl_f(fd, cmd);
    } break;
    case F_SETLK:
    case F_SETLKW:
    case F_GETLK: {
      struct flock *arg = va_arg(va, struct flock *);
      va_end(va);
      return fcntl_f(fd, cmd, arg);
    } break;
    case F_GETOWN_EX:
    case F_SETOWN_EX: {
      struct f_owner_exlock *arg = va_arg(va, struct f_owner_exlock *);
      va_end(va);
      return fcntl_f(fd, cmd, arg);
    } break;
    default:
      va_end(va);
      return fcntl_f(fd, cmd);
  }
}

//特别关注于处理FIONBIO请求，即设置或查询文件描述符的非阻塞状态。
int ioctl(int d, unsigned long int request, ...) {
  va_list va;
  va_start(va, request);
  void *arg = va_arg(va, void *);
  va_end(va);

  if (FIONBIO == request) {
    bool user_nonblock = !!*(int *)arg;
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(d);
    if (!ctx || ctx->isClose() || !ctx->isSocket()) {
      return ioctl_f(d, request, arg);
    }
    ctx->setUserNonblock(user_nonblock);
  }
  return ioctl_f(d, request, arg);
}

//直接转发到getsockopt_f
int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
  return getsockopt_f(sockfd, level, optname, optval, optlen);
}
//对于特定的选项（SO_RCVTIMEO和SO_SNDTIMEO，即接收和发送超时），函数会更新FdCtx中的超时值。
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
  if (!t_hook_enable) {
    return setsockopt_f(sockfd, level, optname, optval, optlen);
  }
  if (level == SOL_SOCKET) {
    if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
      FdCtx::ptr ctx = FdMgr::GetInstance()->get(sockfd);
      if (ctx) {
        const timeval *v = (const timeval *)optval;
        ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
      }
    }
  }
  return setsockopt_f(sockfd, level, optname, optval, optlen);
}
}
}