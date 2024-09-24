#include "fd_manager.hpp"
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "hook.hpp"

//FdManager 类维护了一个文件描述符上下文的集合（std::vector<FdCtx::ptr>）
//通过单例模式实现的FdManager实例，确保整个应用程序中只有一个FdManager实例存在
namespace monsoon {

    /**
     * Socket 标识: 标记该文件描述符是否关联于一个套接字，这对于区分文件和网络连接很重要。
    阻塞控制: 支持用户自定义和系统级的非阻塞标志，允许细粒度控制 I/O 操作的行为。
    超时设置: 提供方法设置读写操作的超时时间，有助于防止操作无限期阻塞。
    */
    FdCtx::FdCtx(int fd)   
        : m_isInit(false), 
          m_isSocket(false), 
          m_sysNonblock(false), 
          m_userNonblock(false), 
          m_isClosed(false),
          m_fd(fd),
          m_recvTimeout(-1),
          m_sendTimeout(-1) {
        init(); //判断文件描述符是否完成了必要的初始化。
    }
    FdCtx::~FdCtx() {}

    FdCtx::init() {
        if (m_isInit()) {
            return true;
        }
        m_recvTimeout = -1;
        m_sendTimeout = -1;

        // 获取文件状态信息
        struct stat fd_stat ;
        if (-1 == fstat(m_fd, &fd_stat)) {
            m_isInit = false;
            m_isSocket = false;
        } else {
            m_isInit = true;
            // 判断是否是socket
            m_isSocket = S_ISSOCK(fd_stat.st_mode);
        }
        if (m_isSocket) {
            int flags = fcntl_f(m_fd, FEGTFL, 0);
            if (!(flags & O_NONBLOCK)) {
                fcntl_f(m_fd, F_GETFL, flags | O_NONBLOCK);
            }
            m_sysNonblock = true;
        } else {
            m_sysNonblock = false;
        }
        m_userNonblock = false;
        m_isClosed = false;
        return m_isInit;
    }

    void FdCtx::setTimeout(int type, uint64_t v) {
        if (type == SO_RCVTIMEO) {
            m_recvTimeout = v;
        } else {
            m_sendTimeout = v;
            //SO_SNDTIMEO
        }
    }
    uint64_t FdCtx::getTimeout(int type) {
        if (type == SO_RCVTIMEO) {
            return m_recvTimeout;
        } else {
            return m_sendTimeout;
        }
    }

    FdManager::FdManager() {
        m_datas.resize(64);
    }

    // 获取/创建文件句柄类
    // auto_create 是否自动创建
    FdCtx::ptr FdManager::get(int fd, bool auto_create = false) {
        if (fd == -1) {
            return nullptr;
        }

        RWMutex::ReadLock lock(m_mutex);
        if ((int)m_datas.size() <= fd) {
            if (auto_create == false) { //true的话会创建空ctx参数为fd
                return nullptr;
            } else {
                if (m_datas[fd] || !auto_create) {
                    return m_datas[fd];
                }
            }
        }
        lock.unlock();

        RWMutex::WriteLock lock2(m_mutex);
        FdCtx::ptr ctx(new FdCtx(fd));
        if  ((int)m_datas.size <= fd) {
            m_datas.resize(fd * 1.5);
        }
        m_datas[fd] = ctx;
        return ctx;
    }
    // 删除文件句柄
    void FdManager::del(int fd) {
        //得获取写锁
        RWMutexType::WriteLock lock(m_mutex);
        if ((int)m_datas.size() <= fd) {
            return;
        }
        m_datas[fd].reset();
    }
}                                                                