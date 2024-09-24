#include "iomanager.hpp"

namespace monsoon {
    EventContext &FdContext::getEveContext(Event event) {
        switch (event)
        {
        case READ:
            return read;
        case WRITE:
            return write;
        default:
            CondPanic(false, "getContext error: unknown event");
        }
        throw std::invalid_argument("getContext invalid event");
    }

    // 重置事件上下文
    void FdContext::resetEveContext(EventContext &ctx) {
        ctx.cb = nullptr;
        ctx.fiber.reset();
        ctx.scheduler = nullptr;
    }

    // 触发事件（只是将对应的fiber or cb 加入scheduler tasklist）
    void FdContext::triggerEvent(Event event) {
        CondPanic(events & event, "event hasn't been registed");
        //将触发的事件从事件状态中移除
        events = Event(events & ~event) ;

        EventContext &ctx = getEveContext(event);
        if (ctx.cb) {
            ctx.scheduler->scheduler(ctx.cb);
        }
        else {
            ctx.scheduler->scheduler(ctx.fiber);
        }
        //在事件处理完毕后，重置事件上下文,以便下次使用
        resetEveContext(ctx);
        return;
    }

    IOManager::IOManager(size_t threads = 1, bool use_caller = true, const std::string &name = "IOManager") 
        : Scheduler(threads, use_caller, name) {
            epfd_ = epoll_create(5000);
            int ret = pipe(tickleFds_);
            CondPanic(ret == 0, "pipe error");
            // 注册pipe读句柄的可读事件，用于tickle调度协程
            epoll_event event{};
            // 边缘触发
            event.events = EPOLLIN | EPOLLET;
            event.data.fd = tickleFds_[0];
            //，设置非阻塞
            ret = fcntl(tickleFds_[0], F_SETFL, O_NONBLOCK);
            CondPanic(ret == 0, "set fd nonblock error");
            // 注册管道读描述符
            ret = epoll_ctl(epfd_, EPOLL_CTL_ADD, tickleFds_[0], &event);
            CondPanic(ret == 0, "epoll_ctl error");

            contextResize(32); //?
            // 启动scheduler，开始进行协程调度
            start();
    }

    IOManager::~IOManager() {
        //先停止，然后删除fd资源和上下文
        stop();
        close(epfd_);
        close(tickleFds_[0]);
        close(tickleFds_[1]);

        for (size_t i = 0; i < fdContexts_.size(); i++) {
            if (fdContexts_[i]) {
                delete fdContexts_[i];
            }
        }
    }

    // 添加事件
int IOManager::addEvent(int fd, Event event, std::function<void()> cb) {
    // 初始化一个指向FdContext的指针
    FdContext *fd_ctx = nullptr;

    // 使用读锁锁定互斥体，以安全地访问fdContexts_容器
    RWMutex::ReadLock lock(mutex_);

    // 检查容器是否包含给定的文件描述符
    // TODO：可以使用map代替
    // 找到fd对应的fdCOntext,没有则创建
    if ((int)fdContexts_.size() > fd) {
        // 如果容器已经有这个fd的记录，则直接获取
        fd_ctx = fdContexts_[fd];
        // 释放读锁
        lock.unlock();
    } else {
        // 如果容器没有这个fd的记录，则释放读锁并获取写锁
        lock.unlock();
        RWMutex::WriteLock lock2(mutex_);
        // 调整容器大小，确保有足够的空间存储新的FdContext
        contextResize(fd * 1.5);
        // 获取调整大小后的FdContext
        fd_ctx = fdContexts_[fd];
    }

    // 锁定FdContext的互斥体，确保线程安全
    Mutex::Lock ctxLock(fd_ctx->mutex);
    // 确保同一个fd不允许注册重复事件,相同的话真取反就是假
    CondPanic(!(fd_ctx->events & event), "addevent error, fd = " + fd);

    // 根据是否已经注册过事件，确定是修改还是添加事件
    //如果还是初始NONE的话，enum的值就是0x0就是0；
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    // 创建epoll_event结构体，用于epoll_ctl调用
    epoll_event epevent;
    epevent.events = EPOLLET | fd_ctx->events | event;  // 设置事件类型为边缘触发，并添加新事件
    epevent.data.ptr = fd_ctx;  // 将FdContext指针作为用户数据传递

    // 使用epoll_ctl注册或修改事件,ctl返回-1表示失败
    int ret = epoll_ctl(epfd_, op, fd, &epevent);
    if (ret) {
        // 如果epoll_ctl调用失败，打印错误信息并返回-1
        std::cout << "addevent: epoll ctl error" << std::endl;
        return -1;
    }
    // 增加待执行IO事件的数量
    ++pendingEventCnt_;

    // 更新FdContext中的事件状态，添加新事件， 或运算就是添加呀
    fd_ctx->events = (Event)(fd_ctx->events | event);

    // 设置事件上下文：获取与新事件关联的EventContext
    EventContext &event_ctx = fd_ctx->getEveContext(event);
    // 确保EventContext不是空指针
    CondPanic(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb, "event_ctx is nullptr");

    // 设置调度器
    event_ctx.scheduler = Scheduler::GetThis();
    if (cb) {
        // 如果提供了回调函数，则设置EventContext的回调
        event_ctx.cb.swap(cb);
    } else {
        // 如果没有提供回调函数，则将当前协程设置为回调任务
        event_ctx.fiber = Fiber::GetThis();
        // 确保协程不是在运行状态，因为不应该在运行时注册事件
        CondPanic(event_ctx.fiber->getState() == Fiber::RUNNING, "state=" + event_ctx.fiber->getState());
    }
    // 打印成功信息
    std::cout << "add event success,fd = " << fd << std::endl;
    return 0;  // 返回0表示成功
}

bool IOManager::delEvent(int fd, Event event) {
    RWMutex::ReadLock lock(mutex_);
    if ((int)fdContext_.size() <= fd) {
        return false;
    }
    FdContext *fd_ctx = fdContext_[fd];
    lock.unlock();

    Mutex::lock ctxlock(fd_ctx->mutex);
    //检查 fd_ctx 是否已经注册了要删除的事件。如果未注册，直接返回 false。
    if (!(fd_ctx->events & event)) {
        return false;
    }
    //// 清除指定的事件，表示不关心这个事件了，如果清除之后结果为0，则从epoll_wait中删除该文件描述符
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int ret = epoll_ctl(epfd_, op, fd, &epevent);
    if (ret) {
        std::cout << "delevent: epoll_ctl error" << std::endl;
        return false;
    }

    --pendingEventCnt_;
    fd_ctx->events = new_events;
    EventContext &event_ctx = fd_ctx->getEveContext(event);
    fd_ctx->resetEveContext(event_ctx);
    return true;
}

// 取消fd的某个事件
bool IOManager::cancelEvent(int fd, Event event) {
    RWMutex::ReadLock lock(mutex_);
    if ((int)fdContext_.size <= fd) {
        return false;
    }
    FdContext *fd_ctx = fdContext_[fd];
    lock.unlock();

    Mutex::lock ctxlock(fd_ctx->mutex);

    if (!(fd_ctx->events & event)) {
        return false;
    }

    //清理就完了
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = new_events | EPOLLET;
    epevent.data.ptr = fd_ctx;

    int ret = epoll_ctl(epfd_, op, fd, &epevent);
    if (ret) {
        std::cout << "delevent: epoll_ctl error" << std::endl;
        return false;
    }
    //删除之前，触发此事件
    //取消前完整执行一次，看trigger的代码可知，能让资源得到释放和重置
    fd_ctx->triggerEvent(event);
    --pendingEventCnt_;
    return true;
}
// 取消监听fd的全部事件
bool IOManager::cancelall(int fd) {
    RWMutex::ReadLock lock(mutex_);
    if ((int)fdContext_.size() <= fd) {
        return false;
    }
    FdContext *fd_ctx = fdContext_[fd];
    lock.unlock();

    Mutex::lock ctxlock(fd_ctx->mutex);
    if (!fd_ctx->events) {
        return false;
    }
    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = 0;
    epevent.data.ptr = fd_ctx;

    int ret = epoll_ctl(epfd_, op, fd, &epevent);
    if (ret) {
        std::cout << "delevent: epoll_ctl error" << std::endl;
        return false;
    }
    //还是那句话删除前触发全部事件
    if (fd_ctx->events & READ) {
        fd_ctx->triggerEvent(READ);
        --pendingEventCnt_;
    }
    if (fd_ctx->events & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --pendingEventCnt_;
    }
    CondPanic(fd_ctx->events == 0, "fd not totally clear");
    return true;
}

IOManager &IOManager::GetThis() {
    return dynamic_cast<IOManager *>(Scheduler::GetThis());
}

// 通知调度器有任务要调度
void IOManager::tickle() {
    if (!isHasIdleThreads()) {
    // 此时没有空闲的调度线程
        return;
    }
    // 写pipe管道，使得idle协程凑够epoll_wait退出，开始调度任务
    int rt = write(tickleFds_[1], "T", 1);
    CondPanic(rt == 1, "write pipe error");
}
// 判断是否可以停止
bool IOManager::stopping() {
    uint64_t timeout = 0;
    return stopping(timeout);
}

// 调度器无任务则阻塞在idle线程上
// 当有新事件触发，则退出idle状态，则执行回调函数
// 当有新的调度任务，则退出idle状态，并执行对应任务
void IOManager::idle() {
    const uint64_t MAX_EVENTS = 256;
    epoll_event *events = new epoll_event[MAX_EVENTS]();
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event *ptr)
                                               { delete[] ptr; });
    while (true) {
        // std::cout << "[IOManager] idle begin..." << std::endl;
        //  获取下一个定时器超时时间，同时判断调度器是否已经stop
        uint64_t next_timeout = 0;
        if (stopping(next_timeout)) {
            std::cout << "name=" << getName() << "idle stopping exit";
            break;
        }
        // 阻塞等待，等待事件发生 或者 定时器超时
        int ret = 0;
        do {
            static const int MAX_TIMEOUT = 5000;
            if (next_timeout != ~0ull) {
                next_timeout = std::min((int)next_timeout, MAX_TIMEOUT);
            }
            else {
                next_timeout = MAX_TIMEOUT;
            }
            ret = epoll_wait(epfd_, events, MAX_EVENTS, (int)next_timeout);
            if (ret < 0) {
                if (errno == EINTR) {
                    //系统调用被信号中断
                    continue;
                }
                std::cout << "epoll_wait [" << epfd_ << "] errno,err: " << errno << std::endl;
                break;
            }
            else {
                break;
            }
        } while (true);

        // 收集所有超时定时器，执行回调函数
        std::vector<std::function<void()>> cbs;
        listExpiredCb(cbs);
        if (!cbs.empty()) {
            for (const auto &cb : cbs) {
                scheduler(cb);
            }
            cbs.clear();
        }
        for (int i = 0; i < ret; i++) {
            epoll_event &event = events[i];
            if (event.data.fd == tickleFds_[0]) {
                // pipe管道内数据无意义，只是tickle意义,读完即可
                uint8_t dummy[256];
                // TODO：ET下阻塞读取可能有问题
                while (read(tickleFds_[0], dummy, sizeof(dummy)) > 0)
                ;
                continue;
            }
            FdContext *fd_ctx = (FdContext *)event.data.ptr;
            Mutex::lock ctxlock(fd_ctx->mutex);
            // 错误事件 or 挂起事件(对端关闭)
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                std::cout << "error events" << std::endl;
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }
            int real_events = NONE;
            if (event.events & EPOLLIN) {
                real_events |= READ;
            }
            if (event.events & EPOLLOUT) {
                real_events |= WRITE;
            }
            if ((fd_ctx->events & real_events) == NONE) {
                // 触发的事件类型与注册的事件类型无交集
                continue;
            }
            // 剔除已经发生的事件，将剩余的事件重新加入epoll_wait
            // issue: 在处理 EPOLLERR 或 EPOLLHUP 事件时，可能需要重新注
            // 册 EPOLLIN 或 EPOLLOUT 事件，以确保后续的 IO 可以正常进行
            int left_events = (fd_ctx->events & ~real_events);
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | left_events;

            int ret2 = epoll_ctl(epfd_, op, fd_ctx->fd, &event);
            if (ret2) {
                std::cout << "epoll_wait [" << epfd_ << "] errno,err: " << errno << std::endl;
                continue;
            }
            // 处理已就绪事件 （加入scheduler tasklist,未调度执行）
            if (real_events & READ) {
                fd_ctx->triggerEvent(READ);
                --pendingEventCnt_;
            }
            if (real_events & WRITE) {
                fd_ctx->triggerEvent(WRITE);
                --pendingEventCnt_;
            }
        }  
        // 处理结束，idle协程yield,此时调度协程可以执行run去tasklist中
        // 检测，拿取新任务去调度
        Fiber::ptr cur = Fiber::GetThis();
        auto raw_ptr = cur.get();
        cur.reset();
        // std::cout << "[IOManager] idle yield..." << std::endl;
        raw_ptr->yield();
    }
}

// 判断是否可以停止，同时获取最近一个定时超时时间
bool IOManager::stopping(uint64_t &timeout) {
    timeout = getNextTimer(); //超时器函数
    return timeout == ~0ull && pendingEventCnt_ == 0 && Scheduler::stopping();
}

void IOManager::OnTimerInsertedAtFront() {
    tickle();
}
void IOManager::contextResize(size_t size) {
    fdContext_.resize(size);
    for (size_t i = 0; i < fdContext_.size(); ++i) {
        if (!fdContext_[i]) {
            fdContexts_[i] = new FdContext;
            fdContexts_[i]->fd = i;
        }
    }
}
}