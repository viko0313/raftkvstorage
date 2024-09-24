#include "timer.hpp"
#include "utils.hpp"

namespace monsoon {
    bool Timer::Comparator::operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const {
        if (!lhs && !rhs) {
            return false;
        }
        if (!lhs) {
            return true;
        }
        if (!rhs) {
            return false;
        }
        if (lhs->next_ < rhs->next_) {
            return true;
        }
        if (rhs->next_ < lhs->next_) {
            return false;
        }
        return lhs.get() < rhs.get();
    }
    Timer::Timer(uint64_t ms, std::function<void()> cb, bool recuring, TimerManager *manager) 
        : recuring_(recuring), ms_(ms), cb_(cb), manager_(manager) {
        next_ = GetElapsedMS() + ms_;
    }
    Timer::~Timer(uint64_t next) : next_(next) {}


    bool Timer::cancel() {
        RWMutex::WriteLock lock(manager_->mutex);
        if (cb_) {
            cb_ = nullptr;
            auto it = manager_->timers_.find(shared_from_this());
            manager_->timers_.erase(it);
            return true;
        }
        return false;
    }
    bool Timer::refresh() {
        RWMutex::WriteLock lock(manager_->mutex_);
        if (!cb_) {
            return false;
        }
        auto it = manager_->timers_.find(shared_from_this());
        if (it == manager_->timers_.end()) {
            return false;
        }
        manager_->timers_.erase(it);

        //重新设置并插入
        next_ = GetElapsedMS() + ms_;
        manager_->timers_.insert(shared_from_this());
        return true;
    }

    
    // 重置定时器，重新设置定时器触发时间
    // from_now = true: 下次出发时间从当前时刻开始计算
    // from_now = false: 下次触发时间从上一次开始计算
    bool Timer::reset(uint64_t ms, bool from_now) {
        if (ms = ms_ && !from_now) {
            return true;
        }
        RWMutex::WriteLock lock(manager_->mutex_);
        if (!cb) {
            return true;
        }
        auto it = manager_->timers_.find(shared_from_this());
        if (it == manager_->timers_.end()) {
            return false;
        }
        manager_->timers_.erase(it);
        uint64_t start = 0;
        if (from_now) {
            start = GetElapsedMS();
        } else {
            start = next_ - ms_;
        }
        ms_ = ms;
        next_ = start + ms_;
        manager_->addTimer(shared_from_this, lock);
        return true;
    }
    TimerManager::TimerManager();
    TimerManager::~TimerManager();
    Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recuring = false);
    Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond,
                                 bool recuring = false);
    // 到最近一个定时器的时间间隔（ms）
    uint64_t TimerManager::getNextTimer();
    // 获取需要执行的定时器的回调函数列表
    void TimerManager::listExpiredCb(std::vector<std::function<void()>> &cbs);
    // 是否有定时器
    bool hasTimer();

     // 当有新的定时器插入到定时器首部，执行该函数
    void TimerManager::OnTimerInsertedAtFront() = 0;
    // 将定时器添加到管理器
    void TimerManager::addTimer(Timer::ptr val, RWMutex::WriteLock &lock);

    // 检测服务器时间是否被调后了
    bool TimerManager::detectClockRolllover(uint64_t now_ms);
}
