#ifndef __MONSOON_SCHEDULER_H__
#define __MONSOON_SCHEDULER_H__

#include <atomic>
#include <boost/type_index.hpp>
#include <functional>
#include <list>
#include <memory>
#include <vector>
#include "fiber.hpp"
#include "thread.hpp"
#include "utils.hpp"

namespace monsoon {

//表示一个任务，可以是一个卸车或者是一个函数
class SchedulerTask {
public:
    friend class Scheduler;
    SchedulerTask() { thread_ = 1; }
    SchedulerTask(Fiber::ptr f, int t) : fiber_(f), thread_(t) {}
    SchedulerTask(Fiber::ptr *f, int t) {
        fiber_.swap(*f); //*f取值
        thread_ = t;
    }
    //很重要这里接受的是函数和线程编号
    SchedulerTask(std::function<void()> f, int t) {
        cb_ = f;
        thread_ = t;
    }
    void reset() {
        cb_ = nullptr;
        thread_ = -1;
        fiber_ = nullptr;
    }
private:
    Fiber::ptr fiber_;
    int thread_;
    std::function<void()> cb_;
};

// N->M协程调度器
class Scheduler {
public:
    typedef std::shared_ptr<Scheduler> ptr;
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string &name = "Scheduler");
    virtual ~Scheduler();
    const std::string &getName() const { return name_; }
    static Scheduler *GetThis();
    static Fiber *GetMainFiber();
    /**
     * \brief 添加调度任务
     * \tparam TaskType 任务类型，可以是协程对象或函数指针
     * \param task 任务
     * \param thread 指定执行函数的线程，-1为不指定
     */
    template<class TaskType>
    void scheduler(TaskType task, int thread = -1) {
        bool isNeedTickle = false;
        {
            Mutex::Lock lock(mutex_);
            isNeedTickle = schedulerNoLock(task, thread);
        }
        if (isNeedTickle) {
            tickle(); // 唤醒idle协程
        }
    }

    void start(); // 启动调动器
    void stop();

protected:
    //通知调度器到达
    virtual void tickle();
    /**
     * \brief  协程调度函数,
     * 默认会启用hook
     */
    void run();
    //无任务时执行idle协程
    //调度器中没有任务执行时执行的线程
    virtual void idle();
    //返回是否可以停止
    virtual bool stopping();
    //设置当前线程的调度器
    void setThis();
    //返回是否有空闲进程
    bool isHasIdleThreads() { return idleThreadCnt_ > 0; }

private:
    // 无锁下，添加调度任务
    // todo 可以加入使用clang的锁检查
    template<class TaskType>
    bool schedulerNoLock(TaskType t, int thread) {
        bool isNeedTickle = tasks_.empty();
        SchedulerTask(t, thread);
        if (task.fiber_ || task.cb_) {
            // std::cout << "有效task" << std::endl;
            tasks_.push_back(task);
        }
        return isNeedTickle;
    }
    // 调度器名称
    std::string name_;
    // 互斥锁
    Mutex mutex_;
    // 线程池
    std::vector<Thread::ptr> threadPool_;
    // 任务队列
    std::list<SchedulerTask> tasks_;
    // 线程池id数组
    std::vector<int> threadIds_;
    //工作线程数量
    size_t threadCnt_ = 0;
    // 活跃线程数
    std::atomic<size_t> activeThreadCnt_ = {0};
    // IDLE线程数
    std::atomic<size_t> idleThreadCnt_ = {0};
    // 是否是use caller
    bool isUseCaller_;
    // use caller= true,调度器所在线程的调度协程
    Fiber::ptr rootFiber_;
    // use caller = true,调度器协程所在线程的id
    int rootThread_ = 0;
    bool isStopped_ = false;
};
}
#endif