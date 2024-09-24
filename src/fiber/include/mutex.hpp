#ifndef __MONSOON_MUTEX_H_
#define __MONSOON_MUTEX_H_

#include <mutex>
#include <thread>

#include "noncopyable.hpp"
#include "utils.hpp"
namespace monsoon {
//重新实现信号量，cond？
class Semaphore : Noncopyable {
public:
    Semaphore(uint32_t count = 0);
    ~Semaphore();

    void wait();
    void notify();

private:
    sem_t semaphore_;
};

// 局部锁类模板
template<class T>
struct ScopeLockImpl {
public:
    ScopeLockImpl(T &mutex) : m_(mutex) {
        m_.lock();
        isLocked_ = true;
    }

    void lock() {
        if (!isLocked_) {
            std::cout << "lock" << std::endl;
            m_.lock();
            isLocked = true;
        }
    }

    void unlock() {
        if (isLocked_) {
            std::cout << "unlock" << std::endl;
            m_.unlock();
            isLocked = false;
        }
    }
    ~ScopeLockImpl() {
        unlock();
    }
private:
    T &m_;
    bool isLocked;
};

//局部读锁
template <class T>
struct ReadScopedLockImpl {
public:
    ReadScopedLockImpl(T &mutex) : mutex_(mutex) {
        mutex_.rdlock();
        isLocked_ = true;
    }
    ~ReadScopedLockImpl() { unlock(); }

    void lock() {
        if (!isLocked_) {
        mutex_.rdlock();
        isLocked_ = true;
        }
    }
    void unlock() {
        if (isLocked_) {
        mutex_.unlock();
        isLocked_ = false;
        }
    }

private:
    /// mutex
    T &mutex_;
    /// 是否已上锁
    bool isLocked_;
};

template <class T>
struct WriteScopedLockImpl {
public:
    WriteScopedLockImpl(T &mutex) : mutex_(mutex) {
        mutex_.wrlock();
        isLocked_ = true;
    }

    ~WriteScopedLockImpl() { unlock(); }
    void lock() {
        if (!isLocked_) {
        mutex_.wrlock();
        isLocked_ = true;
        }
    }
    void unlock() {
        if (isLocked_) {
        mutex_.unlock();
        isLocked_ = false;
        }
    }

private:
    //Mutex
    T &mutex_;
    /// 是否已上锁
    bool isLocked_;
};

class Mutex : Noncopyable {
public:
    typedef ScopeLockImpl<Mutex> ptr;

    Mutex() { CondPanic(0 == pthread_mutex_init(&m_, nullptr), "lock init success") };

    void lock() { CondPanic(0 == pthread_mutex_lock(&m_), "Mutex::lock error"); }
    void unlock() { ConPanic(0 == pthread_mutex_unlock(&m_), "Mutex::unlock error") };
    ~Mutex() { CondPanic(0 == pthread_mutex_destroy(&m_), "destory lock error") };

private:
    pthread_mutex_t m_;
};

class RWMutex : Noncopyable {
public:
    typedef ReadScopedLockImpl<RWMutex> ReadLock;
    typedef WriteScopedLockImpl<RWMutex> WriteLock;

    RWMutex() { pthread_rwlock_init(&m_, nullptr) };
    ~RWMutex() { pthread_rwlock_destroy(&m_); }

    void rdlock() { pthread_rwlock_rdlock(&m_); }
    void wrlock() { pthread_rwlock_wrlock(&m_); }
    void unlock() { pthread_rwlock_unlock(&m_); }

private : pthread_rwlock_t m_;
};
}

#endif