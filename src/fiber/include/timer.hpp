#ifndef __MONSOON_TIMER_H__
#define __MONSSON_TIMER_H__

#include <memory>
#include <functional>
#include <set>
#include <vector>
#include "mutex.hpp"
namespace monsoon {
class TimerManager;
class Timer : public std::enable_shared_from_this<Timer> {
    friend class TimerManager;
public:
    typedef std::shared_ptr<Timer> ptr;

    bool cancel();
    bool refresh();
    bool reset(uint64_t ms, bool from_now);

private:
    Timer(uint64_t ms, std::function<void()> cb, bool recuring, TimerManager *manager);
    ~Timer(uint64_t next);

    // 是否是循环定时器
    bool recurring_ = false;
    // 执行周期
    uint64_t ms_ = 0;
    // 精确的执行时间
    uint64_t next_ = 0;
    // 回调函数
    std::function<void()> cb_;
    // 管理器
    TimerManager *manager_ = nullptr;
private:
    struct Comparator {
        bool operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const;
    }
};

/**
 * 定时器添加：通过addTimer或addConditionTimer方法向TimerManager添加定时任务，这些任务会被组织成一个有序集合，按照触发时间排序。
定时器执行：TimerManager提供机制来检查哪些定时器已经到期，并通过listExpiredCb收集这些定时器的回调函数，然后执行。
循环与重复：Timer类支持重复定时器，通过refresh方法更新下次执行时间，确保定时器按周期重复触发。
*/
class TimerManager {
    friend class Timer;

public:
    TimerManager();
    virtual ~TimerManager();
    Timer::ptr addTimer(uint64_t ms, std::function<void()> cb, bool recuring = false);
    Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond,
                                 bool recuring = false);
    // 到最近一个定时器的时间间隔（ms）
    uint64_t getNextTimer();
    // 获取需要执行的定时器的回调函数列表
    void listExpiredCb(std::vector<std::function<void()>> &cbs);
    // 是否有定时器
    bool hasTimer();
protected:
     // 当有新的定时器插入到定时器首部，执行该函数
    virtual void OnTimerInsertedAtFront() = 0;
    // 将定时器添加到管理器
    void addTimer(Timer::ptr val, RWMutex::WriteLock &lock);
private:
    // 检测服务器时间是否被调后了,用于处理系统时间调整导致的潜在问题。
    bool detectClockRolllover(uint64_t now_ms);
    //线程安全地访问timers_。
    RWMutex mutex_;
    // 定时器集合
    std::set<Timer::ptr, Timer::Comparator> timers_;
    // 是否触发OnTimerInsertedAtFront,标记是否因为新定时器插入到队列前端而需要触发回调。
    bool tickled_ = false;
    // 上次执行时间
    uint64_t previouseTime_ = 0;
};
}

#endif