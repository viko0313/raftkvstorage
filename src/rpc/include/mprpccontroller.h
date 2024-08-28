#ifndef MPRPCCONTROLLER_H
#define MPRPCCONTROLLER_H
#include <google/protobuf/service.h>
#include <string>

class MprpcController : public google::protobuf::RpcController {
public:
    MprpcController();
    void Reset();
    bool Failed const(); // const在后面意味着不会修改成员？
    std::string ErrorText() const;
    void SetFailed(const std::string &reason);

    // 目前为实现的功能
    void StartCancel();
    bool IsCanceled() const;
    // 实现这个就是异步了吧
    void NotifyOnCancel(google::protobuf::Closure *callback);

private:
    bool m_failed;         //rpc执行的状态 
    std::string m_errText; // rpc执行的错误信息
};

#endif