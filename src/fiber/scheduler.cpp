#include "scheduler.hpp"
#include "fiber.hpp"
namespace monsoon {
    // 当前线程的调度器，同一调度器下的所有线程共享同一调度器实例 （线程级调度器）
    static thread_local Scheduler *cur_scheduler = nullptr;
    // 当前线程的调度协程，每个线程一个 (协程级调度器)
    static thread_local Fiber *cur_scheduler_fiber = nullptr;
    const std::string Log_Head = "[scheduler]";

    Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name)
    {
        CondPanic(threads > 0, "threads <= 0");
        isUseCaller_ = use_caller;
        name_ = name;

        // use_caller:是否将当前线程也作为被调度线程,主线程参与
        if (use_caller) {
            std::cout << LOG_HEAD << "current thread as called thread" << std::endl;
            // 总线程数减1
            --threads;
            // 初始化caller线程的主协程
            Fiber::GetThis();
            std::cout << LOG_HEAD << "init caller thread's main fiber success" << std::endl;
            CondPanic(GetThis() == nullptr, "GetThis err:cur scheduler is not nullptr");
            // 设置当前线程为调度器线程（caller thread）
            cur_scheduler = this;
            //主线程参与调度
            // 初始化当前线程的调度协程 （该线程不会被调度器带哦都），调度结束后，返回主协程
            rootFiber_.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));
            std::cout << LOG_HEAD << "init caller thread's caller fiber success" << std::endl;

            Thread::SetName(name_);
            cur_scheduler_fiber = rootFiber_.get();
            //调度器协程所在线程的id
            rootThread_ = GetThreadId();
            threadIds_.push_back(rootThread_);
        } else {
            rootThread_ = -1;
        }
        threadCnt_ = threads;
        std::cout << "-------scheduler init success-------" << std::endl;
    }

    Scheduler* Scheduler::GetThis() {
        return cur_scheduler; //线程调度器只有一个
    }

    Fiber* Scheduler::GetMainFiber() {
        return cur_scheduler_fiber;
    }
    void Scheduler::setThis() { cur_scheduler = this; }

    Scheduler::~Scheduler() {
        CondPanic(isStopped_, "isstopped is false");
        if (GetThis() == this) {
            cur_scheduler = nullptr;
        }
    }
    // 调度器启动
    // 初始化调度线程池
    void Scheduler::start() {
        std::cout << LOG_HEAD << "scheduler start" << std::endl;
        Mutex::Lock lock(mutex_);
        if (isStopped_) {
            std::cout << "scheduler has stopped" << std::endl;
            return;
        }
        CondPanic(threadPool_.empty(), "thread pool is not empty");
        threadPool_.resize(threadCnt_);
        for (int i = 0; i < threadCnt_; ++i) {
            //每个线程都需要一个入口函数，这是线程启动后首先执行的函数。
            //通过绑定这个函数，每个线程都会执行调度器的逻辑，从而实现协程的并发执行。
            threadPool_[i].reset(new Thread(std::bind(&Scheduler::run, this), name_ + "_" + std::to_string(i)));
            threadIds_.push_back(threadPool_[i]->getId());
        }
    }
    // 调度协程
    void Scheduler::run() {
        std::cout << LOG_HEAD << "begin run" << std::endl;
        set_hook_enable(true);
        setThis();

        if (GetThreadId() != rootThread_) {
            // 如果当前线程不是caller线程，则初始化该线程的调度协程
            cur_scheduler_fiber = Fiber::GetThis().get();
        }

        Fiber::ptr idleFiber(new Fiber(std::bind(&Scheduler::idle, this)));
        Fiber::ptr cbFiber;

        SchedulerTask task;
        while (true) {
            
        }
    }
    void Scheduler::stop() {

    }
}