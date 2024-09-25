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
    //删除之后设置ms_和next_,然后新的addTimer
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
        //from_now为true，则新的起始时间start为当前已流逝的时间（通过GetElapsedMS()获取）。
        if (from_now) {
            start = GetElapsedMS();
        } else {
            //上一次计算：原定触发时间减去旧的超时时间
            start = next_ - ms_;
        }
        //新的超时时间
        ms_ = ms;
        //执行（触发）时间
        next_ = start + ms_;
        manager_->addTimer(shared_from_this, lock);
        return true;
    }
    TimerManager::TimerManager() {
        previouseTime_ = GetElapsedMS(); //上次执行时间，基准时间
    }
    TimerManager::~TimerManager() {}

    Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recuring) {
        Timer::ptr timer(new Timer(ms, cb, recuring, this));
        RWMutex::WriteLock lock(mutex_);
        addTimer(timer, lock);
        return timer;
    }
    static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
        std::shared_ptr<void> tmp = weak_cond.lock();
        if (tmp) {
            cb();
        }
    }
    Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond,
                                 bool recuring) {
        return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recuring);
    }
    // 到最近一个定时器的时间间隔（ms）
    uint64_t TimerManager::getNextTimer() {
        RWMutex::ReadLock lock(mutex_);
        tickled_ = false;
        if (timers_.empty()) {
            return ~0ull;
        }
        const Timer::ptr &next = *timers_.begin();
        uint64_t now_ms = GetElapsedMS();
        if (now_ms >= next->next_) {
            return 0;
        }else {
            return next->next_ - now_ms;
        }
    }
    // 获取需要执行的定时器的回调函数列表
    void TimerManager::listExpiredCb(std::vector<std::function<void()>> &cbs) {
        uint64_t now_ms = GetElapsedMS();
        std::vector<Timer::ptr> expried;
        {
            RWMutex::ReadLock lock(mutex_);
            if (timers_.empty()) {
                return;
            } //这部分逻辑不是hasTimer()的吗，完全没有用到
        }
        RWMutex::WriteLock lock(mutex_);
        if (timers_.empty()) {
            return;
        }
        bool rollover = false;
        if (detectClockRollover(now_ms)) {
            rollover = true;
        }
        //next_ > now_ms就是没有需要执行的呀
        if (!rollover && ((*timers.begin())->next_ > now_ms)) {
            return;
        }
        Timer::ptr now_timer(new Timer(now_ms));
        
        //集合末尾开始找
        //使用lower_bound(now_timer)找到第一个next_值大于等于now_ms的定时器
        auto it = rollover ? timers_.end() : timers_.lower_bound(now_timer);

        //跳过所有触发时间为now_ms的定时器。
        while (it != timers_.end() && (*it)->next_ == now_ms) { 
            ++it;
        }
        //收集从集合开头到找到的位置的所有定时器(timers_.begin(), it)插到expired开始
        expired.insert(expired.begin(), timers_.begin(), it);
        //并从原集合中删除这些过期定时器。(该执行了)
        timers_.erase(timers_.begin(), it);

        cbs.reserve(expired.size());
        for (auto &timer : expired) {
            cbs.push_back(timer->cb_);
            if (timer->recurring_) {
                timer->next_ = now_ms + timer->ms_;
                timers_.insert(timer);
            } else {
                timer->cb_ = nullptr;
            }
        }
    }

    // 是否有定时器
    bool TimerManager::hasTimer() {
        RWMutex::ReadLock lock(mutex_);
        return !timers_.empty();
    }

     // 当有新的定时器插入到定时器首部，执行该函数
    //void TimerManager::OnTimerInsertedAtFront() = 0;
    // 将定时器添加到管理器
    void TimerManager::addTimer(Timer::ptr val, RWMutex::WriteLock &lock) {
        //lock传过来处于锁ing
        auto it = timers_.insert(val).first;
        //检查新插入的定时器是否位于容器的最前面（即是最接近触发的定时器），并且之前没有触发过OnTimerInsertedAtFront。
        bool at_front = (it == timers_.begin()) && !tickled_;
        if (at_front) {
            //用于记录是否有定时器插入到了队列的最前端
            tickled_ = true;
        }
        lock.unlock();
        if (at_front) {
            //通知系统有新的最紧急定时器加入，可能需要立即检查是否需要触发定时器
            OnTimerInsertedAtFront();
        }
    }

    // 检测服务器时间是否被调后了
    bool TimerManager::detectClockRolllover(uint64_t now_ms) {
        bool rollover = false;
        //now怎么可能小于pre呢 第二个条件是检查是否回拨了一个小时
        if (now_ms < previouseTime_ && now_ms < (previouseTime_ - 60 * 60 * 1000)) {
            rollover = true;
        }
        //必须执行
        previouseTime_ = now_ms;
        return rollover;
    }
}   // namespace monsoon
