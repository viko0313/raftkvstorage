#ifndef __MONSOON_UTIL_H__
#define __MONSOON_UTIL_H__

#include <string>
#include <vector>

#include <assert.h>
#include <cxxabi.h>
#include <execinfo.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <stdint.h>
#include <unistd.h>

#include <pthread.h>
#include <stdio.h>
#include <iostream>
#include <sstream>
namespace monsoon {
    pid_t GetThreadId();
    u_int32_t GetFiberId();

    // 获取当前启动的毫秒数
    // 系统从启动到当前时刻的毫秒数
    static uint64_t GetElapsedMS() {
        struct timespec ts = {0};
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }

// 将原始函数名解析为可读函数名
static std::string demangle(const char *str) {

}

// 获取当前线程的调用栈信息
static void Backtrace(std::vector<std::string> &bt, int size, int skip) {

}

static std::string BacktraceToString(int size, int skip, const std::string &prefix) {

}

static void CondPanic(bool condition, std::string err) {
    if (!condition) {
        std::cout << "[assert by] (" << __FILE__ << ":" << __LINE__ << "), err: " << std::endl;
        std::cout << "[backtrace]\n"
                  << BacktraceToString(6, 3, "") << std::endl;
        assert(condition);
    }
}
}

#endif