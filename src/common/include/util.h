#ifndef UTIL_H
#define UTIL_H

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/access.hpp>
#include <condition_variable>  // pthread_condition_t
#include <functional>
#include <iostream>
#include <mutex>  // pthread_mutex_t
#include <queue>
#include <random>
#include <sstream>
#include <thread>

#include "config.h"

template<class F>
class DeferClass {
public:
    DeferClass(F&& f) : m_func(std::forward<F>(f)) {}
    DeferClass(F& f) : m_func(f) {}
    ~DeferClass() { m_func(); }

    DeferClass(const DeferClass& e) = delete;
    DeferClass& operator=(const DeferClass& e) = delete;
private:
    F m_func;
};

#define _CONCAT(a, b) a##b
#define _MAKE_DEFER_(line) DeferClass _CONCAT(defer_placeholder, line) = [&]()

#undef DEFER
#define DEFER _MAKE_DEFER_(__LINE__)

void DPrintf(const char* format, ...);

template <typename... Args>
std::string format(const char* format_str, Args... args) {
  //stringstream进行字符串的拼接和格式化操作。
  std::stringstream ss;
  //C++17 引入的折叠表达式
  //每个参数 args，执行 (ss << args) 操作，并将结果赋值给一个临时数组 _[] 的元素。
  int _[] = {((ss << args), 0)...};
  // 不是类型转换，而是将 _ 的值“转换”为 void 类型，以避免编译器的未使用变量警告。
  (void)_;
  return ss.str();
}

void myAssert(bool condition, std::string message = "Assertion failed!");

//包装now
std::chrono::_V2::system_clock::time_point now();

//包装random
std::chrono::milliseconds getRandomizedElectionTimeout();
void sleepNMilliseconds(int N);

// ////////////////////////异步写日志的日志队列
// read is blocking!!! LIKE  go chan
template<typename T>
class LockQueue {
public:
    //多个worker线程会写日志
    void Push(const T& data) {
        std::lock_guard<tsd::mutex> lg(m_mtx);
        m_queue.push(data);
        m_condvariable.notify_one();
    }

    //一个线程读日志
    T Pop() {
        std::unique_lock<tsd::mutex> lg(m_mtx);
        while (m_queue.empty()) { //日志为空等待状态，等待push的notify
            m_condvariable.wait(lg);
        }
        T data = m_queue.front();
        m_queue.pop();
        return data;
    }

    bool timeOutPop(int timeout, T* ResData) {
        std::unique_lock<tsd::mutex> lg(m_mtx);

        auto now = std::chrono::system_clock::now();
        auto timeout_time = now + std::chrono::milliseconds(timeout);
        while (m_queue.empty()) {
            if (m_condvariable.wait_until(lg, timeout_time) == std::cv_status::timeout) {
                return false;
            } else {
                continue; //没有超时就直到queue非空
            }
        }

        T data = m_queue.front;
        m_queue.pop();
        *ResData = data;
        return true;
    }

private:
    std::mutex m_mtx;
    std::queue<T> m_queue;
    std::condition_variable m_condvariable;
};
// 两个对锁的管理用到了RAII的思想，防止中途出现问题而导致资源无法释放的问题！！！
// std::lock_guard 和 std::unique_lock 都是 C++11 中用来管理互斥锁的工具类，它们都封装了 RAII（Resource Acquisition Is
// Initialization）技术，使得互斥锁在需要时自动加锁，在不需要时自动解锁，从而避免了很多手动加锁和解锁的繁琐操作。
// std::lock_guard 是一个模板类，它的模板参数是一个互斥量类型。当创建一个 std::lock_guard
// 对象时，它会自动地对传入的互斥量进行加锁操作，并在该对象被销毁时对互斥量进行自动解锁操作。std::lock_guard
// 不能手动释放锁，因为其所提供的锁的生命周期与其绑定对象的生命周期一致。 std::unique_lock
// 也是一个模板类，同样的，其模板参数也是互斥量类型。不同的是，std::unique_lock 提供了更灵活的锁管理功能。可以通过
// lock()、unlock()、try_lock() 等方法手动控制锁的状态。当然，std::unique_lock 也支持 RAII
// 技术，即在对象被销毁时会自动解锁。另外， std::unique_lock 还支持超时等待和可中断等待的操作。

// 这个Op是kv传递给raft的command
class Op {
public:
    std::string Operation;
    std::string Key;
    std::string Value;
    std::string ClientId; //客户端号码
    int RequestId;      //客户端号码请求的Request的序列号，为了保证线性一致性
                         // IfDuplicate bool // Duplicate command can't be applied twice , but only for PUT and APPEND

    std::string asString() const {
        std::stringstream ss;
        boost::archive::text_oarchive oa(ss);
        //把类成员写入
        oa << *this;

        return ss.str();
    }

    bool parseFromString(std::string str) {
        std::stringstream iss(str);
        boost::archive::text_iarchive ia(iss);
        // read class state from archive
        ia >> *this;
        return true;  // todo : 解析失敗如何處理，要看一下boost庫了
    }

    //两个参数的得友元
    friend std::ostream& operator<<(std::ostream& os, const Op& obj) {
        os << "[MyClass:Operation{" + obj.Operation + "},Key{" + obj.Key + "},Value{" + obj.Value + "},ClientId{" +
              obj.ClientId + "},RequestId{" + std::to_string(obj.RequestId) + "}";  // 在这里实现自定义的输出格式
        return os;
    }
private:
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive &ar, const unsigned int version) {
        ar& Operation;
        ar& Key;
        ar& Value;
        ar& ClientId;
        ar& RequestId;
    }
};

///////////////////////////////////////////////kvserver reply err to clerk

const std::string OK = "OK";
const std::string ErrNoKey = "ErrNoKey";
const std::string ErrWrongLeader = "ErrWrongLeader";

////////////////////////////////////获取可用端口
bool isReleasePort(unsigned short usPort);

bool getReleasePort(short& port);

#endif // UTIL_H