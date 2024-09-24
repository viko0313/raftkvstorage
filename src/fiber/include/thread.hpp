#ifndef __SYLAR_THREAD_H_
#define __SYLAR_THREAD_H_

#include <memory>
#include <functional>
#include <string>
#include <pthread.h>
namespace monsoon {
class Thread {
public:
    typedef std::shared_ptr<Thread> ptr;
    Thread(std::function<void()> cb, const std::string &name);
    ~Thread();

    pid_t getId() {return id_;}
    const std::string &getName() { return name_; }
    void join();

    static Thread *GetThis(); //单例
    static const std::string &GetName();
    static void SetName(const std::string &name);
private:
    //删掉拷贝构造和赋值,应该可以直接继承noncopyable吧
    Thread(const Thread &) = delete;
    Thread(const Thread &&) = delete;
    Thread operator=(const Thread &) = delete;
    static void *run(void *args);

private:
    pid_t id_;
    pthread_t thread_;
    std::function<void()> cb_;
    std::string name_;
};
} // namespace

#endif