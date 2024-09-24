#include "include/fiber.hpp"
#include <assert.h>
#include <atomic>
#include "include/schedule.hpp"
#include "include/utils.hpp"

namespace monsoon {
    const bool DEBUG = true;
    // 当前线程正在运行的协程
    static thread_local Fiber *cur_fiber = nullptr;
    // 当前线程的主协程
    static thread_local Fiber::ptr cur_thread_fiber = nullptr;
    // 用于生成协程Id
    static std::atomic<uint64_t> cur_fiber_id{0};
    // 统计当前协程数
    static std::atomic<uint64_t> fiber_count{0};
    // 协议栈默认大小 128k
    static int g_fiber_stack_size = 128 * 1024;
    class StackAllocator {
    public:
        static void *Alloc(size_t size) { return malloc(size); }
        static void Delete(void *vp, size_t size) { return free(vp); }
    };
    /**
    * @brief 构造函数
    * @attention ⽆参构造函数只⽤于创建线程的第⼀个协程，也就是线程主函数对应的协程，
    * 这个协程只能由GetThis()⽅法调⽤，所以定义成私有⽅法
    */
    Fiber::Fiber() {
        SetThis(this);
        state_ = RUNING;
        //是否能获取上下文
        CondPanic(getcontext(&ctx_) == 0, "getcontext error");
        ++fiber_count;
        id_ = cur_fiber_id++;
        std::cout << "[fiber] create fiber , id = " << id_ << std::endl;
    }
    void Fiber::SetThis(Fiber *f) {
        cur_fiber = f;
    }

    // 获取当前正在执行的协程的智能指针。不存在则创建
    //无论是否有，都返回智能指针，确保生命周期
    Fiber::ptr Fiber::GetThis() {
        if (cur_fiber) {
            return cur_fiber->shared_from_this();
        }
        Fiber::ptr main_fiber(new Fiber); //调用默认的构造函数
        CondPanic(cur_fiber == main_fiber.get(), "cur_fiber need to be main_fiber");
        cur_thread_fiber = main_fiber;
        return cur_fiber->shared_from_this();
    }

    Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_isshceduler) 
        : m_id(cur_fiber_id++),
          cb_(cb),
          isRunInScheduler_(run_isshceduler) {
        ++fiber_count;
        stackSize_ = stacksize > 0 ? stacksize : g_fiber_stack_size;
        stack_ptr = StackAllocator::Alloc(stackSize_);
        CondPanic(getcontext(&ctx_) == 0, "getcontext error");
        ctx_.un_link = nullptr;
        ctx_.uc_stack.ss_sp = stack_ptr;
        ctx_.uc_stack.ss_size = stackSize_;
        //makecontext执⾏完后，ctx_就与MainFunc绑定了，调⽤setcontext或swapcontext激活uctx_时，MainFunc就会被运⾏
        makecontext(&ctx_, &Fiber::MainFunc, 0);
    }
    /**
    * @brief 将当前协程切到到执⾏状态
    * @details 当前协程和正在运⾏的协程进⾏交换，前者状态变为RUNNING，后者状态变为READY
    */
    void Fiber::resume() {
        CondPanic(state_ != TERM && state_ != RUNNING, "state error");
        SetThis(this);
        state_ = RUNNING;
        //相比随想录就是多了个调度器，调度器是对称协程吧
        if (isRunInScheduler_) {
            // 当前协程参与调度器调度，则与调度器主协程进行swap
            CondPanic(0 == swapcontext(&(Scheduler::GetMainFiber()->ctx_), &ctx_),
                      "isRunInScheduler_ = true,swapcontext error");
        } else {
            // 切换主协程到当前协程，并保存主协程上下文到子协程ctx_
            CondPanic(0 == swapcontext(&(cur_thread_fiber->ctx_), &ctx_), "isRunInScheduler_ = false,swapcontext error");
        }
    }
    // 当前协程让出执行权
    // 协程执行完成之后胡会自动yield,回到主协程，此时状态为TEAM
    void Fiber::yield() {
        CondPanic(state_ == TERM || state_ == RUNNING, "state error");
        SetThis(cur_thread_fiber.get());

        if (state_ != TERM) {
            state_ = READY;
        }
        //注意一下swapcontext的保存顺序
        if (isRunInScheduler_) {
                CondPanic(0 == swapcontext(&ctx_, &(Scheduler::GetMainFiber()->ctx_)),
              "isRunInScheduler_ = true,swapcontext error");
        } else {
            CondPanic(0 == swapcontext(&ctx_, (&cur_thread_fiber->ctx_)), "isRunInScheduler_ = false,swapcontext error");
        }
    }

    //协程的入口函数
    void Fiber::MainFunc() {
        Fiber::ptr cur = GetThis(); // GetThis()的shared_from_this()⽅法让引⽤计数加1
        CondPanic(cur != nullptr, "cur is nullptr");
        //执行完就置空？
        cur->cb_();
        cur->cb_ = nullptr;
        cur->state_ = TERM;
        // ⼿动让t_fiber的引⽤计数减1
        auto raw_ptr = cur.get();
        //reset()方法会将智能指针的管理对象置为空（默认行为），从而减少它所指向对象的引用计数。
        cur.reset();
        //由于raw_ptr是一个原始指针，它不改变对象的引用计数，而是直接调用了协程实例上的yield方法，用于将控制权交回给调度协程或住协程，实现协程的切换。
        raw_ptr.yield();
    }

    // 协程重置（复用已经结束的协程，复用其栈空间，创建新协程）
    // TODO:暂时不允许Ready状态下的重置,但其实刚创建好但没执⾏过的协程也应该允许重置
    void Fiber::reset(std::function<void()> cb) {
        ConPanic(stack_ptr, "stack_ptr is nullptr");
        ConPanic(state_ == TERM, "state is not TERM");
        cb_ = cb;
        ConPanic(0 == getcontext(&ctx_), "getcontext failed");
        ctx_.uc_link = nullptr;
        ctx_.uc_stack.ss_sp = stack_ptr;
        ctx_.uc_stack.ss_size = stackSize_;
        makecontext(&ctx_, &Fiber::MainFunc, 0);
        state_ = READY;
    }

    Fiber::~Fiber() {
        --fiber_count;
        if (stack_ptr) {
            // 有栈空间，说明是子协程
            CondPanic(state_ == TERM, "fiber state should be term");
            StackAllocator::Delete(stack_ptr, stackSize_);
        } else {
            // 没有栈空间，说明是线程的主协程
            // 主协程的一些检查：
            // 1. 确保主协程没有回调函数（cb_），因为主协程不通过回调函数启动
            CondPanic(!cb_, "main fiber no callback");
            // 2. 确认主协程的状态应该是RUNNING（正在运行），因为主协程不应该在非运行状态下被销毁
            CondPanic(state_ == RUNNING, "main fiber should be running");

            // 获取当前正在执行的协程指针
            Fiber *cur = cur_fiber;

            // 如果当前执行的协程正好是正在被销毁的主协程，则需要将当前协程指针重置为nullptr
            if (cur == this) {
                SetThis(nullptr); // 设置当前协程指针为nullptr，表示没有活跃的协程
            }
        }
    }
}