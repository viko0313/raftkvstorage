#ifndef __MONSOON_FIBER_H__
#define __MONSOON_FIBER_H__

#include <ucontext.h>
#include <mutex.h>
#include <functional>
namespace monsoon {
    class Fiber : public std::enable_shared_from_this<Fiber> {
    public:
        typedef std::shared_ptr<Fiber> ptr;
        enum State
        {
            READY,
            RUNNING,
            TERM,
        };
    private:
        Fiber();
    public:
        //协程就是接受函数和栈空间大小，
        Fiber(std::function<void()> cb, size_t stackSz = 0, bool run_in_scheduler = true);
        ~Fiber();
        void resume();
        void yield();
        // 重置协程状态，复用栈空间
        void reset(std::function<void()> cb);

        uint64_t getId() const { return id_; }
        State getState() const { return State_; }
        // 设置当前正在运行的协程
        static void SetThis(Fiber *f);
        // 获取当前线程中的执行线程
        // 如果当前线程没有创建协程，则创建第一个协程，且该协程为当前线程的
        // 主协程，其他协程通过该协程来调度
        static Fiber::ptr GegThis();
        //没有实现
        static uint64_t TotalFiberNum();
        // 协程回调函数
        static void MainFunc();
        //获取当前协程id，没有实现
        static uint64_t GetCurFiberId();
    private:
        uint64_t id_ = 0;
        // 协程栈大小
        uint32_t stackSize_ = 0;
        // 协程状态
        State state_ = READY;
        // 协程上下文
        ucontext_t ctx_;
        // 协程栈地址
        void *stack_ptr = nullptr;
        // 协程回调函数
        std::function<void()> cb_;
        // 本协程是否参与调度器调度
        bool isRunInScheduler_;
    };
}
#endif
