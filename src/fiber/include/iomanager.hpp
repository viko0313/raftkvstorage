#ifndef __SYLAR_IOMANAGER_H__
#define __SYLAR_IOMANAGER_H__
#include "utils.hpp"
#include "schedule.hpp"
#include "fiber.hpp"
#include "mutex.hpp"
#include "fcntl.h"
#include <sys/epoll.h>
#include "timer.hpp"
#include <string>

//当有新的任务需要被调度时，IOManager 可以通过向管道写入数据来通知调度器。
//确保调度器能够及时响应新任务，而不需要依赖 I/O 事件的发生。
namespace monsoon {
    //这些枚举值的设计允许使用位运算符（如按位与 &）来检查一个事件是否包含特定的类型。
    //可以使用 event & READ != 0 来检查是否发生了读事件
    enum Event
    {
        NONE = 0x0,
        READ = 0x1,
        WRITE = 0x4,
    };
    struct EventContext {
        Scheduler *scheduler = nullptr;
        std::function<void()> cb;
        Fiber::ptr fiber;
    };

    class FdContext {
        friend class IOManager;
    public:
        // 获取事件上下文
        EventContext &getEveContext(Event event);
        // 重置事件上下文
        void resetEveContext(EventContext &ctx);
        // 触发事件
        void triggerEvent(Event event);
    private:
        int fd = 0;
        Event events = NONE;
        EventContext read;
        EventContext write;
        Mutex mutex;
    };
    class IOManager : public Scheduler, public TimerManager {
    public:
        typedef std::shared_ptr<C> ptr;

        IOManager(size_t threads = 1, bool use_caller = true, const std::string &name = "IOManager");
        ~IOManager();

        int addEvent(int fd, Event event, std::function<void()> cb = nullptr);

        bool delEvent(int fd, Event event);

        //取消fd的某个事件
        bool cancelEvent(int fd, Event event);
        //取消监听fd的全部事件
        bool cancelall(int fd);

        static IOManager &GetThis();
    protected:
        // 通知调度器有任务要调度
        void tickle() override;
        // 判断是否可以停止
        bool stopping() override;
        // idle协程
        void idle() override;

        // 判断是否可以停止，同时获取最近一个定时超时时间
        bool stopping(uint64_t &timeout);

        void OnTimerInsertedAtFront() override;  //继承timer
        void contextResize(size_t size);
    private:
        int epfd_ = 0;
        int tickleFds_[2];

        std::atomic<size_t> pendingEventCnt_ = {0};
        RWMutex mutex_;
        std::vector<FdContext *> fdContext_;
    };
}

#endif