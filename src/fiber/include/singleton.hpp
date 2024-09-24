#ifndef __MONSOON_SINGLETON_H__
#define __MONSOON_SINGLETON_H__

#include <memory>

namespace monsoon {
namespace {
    template<class T, class X, int N>
    T &GetInstanceX() {
        static T v; //懒汉？
        return v;
    }

    template<class T, class X, int N>
    std::shared_ptr<T> GetInstancePtr() {
        static std::shared_ptr<T> v(new T);
        return v;
    }
} //namespace

/**
 * @brief 单例模式封装类
 * @details T 类型
 *          X 为了创造多个实例对应的Tag
 *          N 同一个Tag创造多个实例索引
 */
template<class T, class X = void, int N = 0>
class Singleton {
public:
    static T *GetInstance() {
        static T v;
        return &v;
        
    }
};
template<class T, class X = void, int N = 0>
class SingletonPtr {
public:
    static std::shared_ptr<T> GetInstance() {
        static std::shared_ptr<T> v(new T);
        return v;
        // return GetInstancePtr<T, X, N>();
    }
};
}// namespace monsoon

#endif