#ifndef MPRPCCHANNEL_H
#define MPRPCCHANNEL_H

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
using namespace std;

//RaftRpcUtil 类的成员函数（如 AppendEntries）使用 Stub 对象的对应方法发起 RPC 调用。
//这些调用会通过 MprpcChannel 进行网络传输。
class MprpcChannel : public google::protobuf::RpcChannel
{
private:
    int m_clientFd;
    //const std::string m_ip; 有了using
    const string m_ip;
    const uint16_t m_port;
    /// @brief 连接ip和端口,并设置m_clientFd
    /// @param ip ip地址，本机字节序
    /// @param port 端口，本机字节序
    /// @return 成功返回空字符串，否则返回失败信息
    bool newConnect(const char *ip, uint16_t port, string *errMsg);

public:
    MprpcChannel(string ip, short port, bool connectNow);
    // 所有通过stub代理对象调用的rpc方法，都走到这里了，统一做rpc方法调用的数据数据序列化和网络发送 那一步
    void callMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller,
                    const google::protobuf::Message *request, google::protobuf::Message *response,
                    google::protobuf::Closure *done) override;
    //序列化和发送请求：当 Stub 对象发起 RPC 调用时，MprpcChannel 的 CallMethod 函数会被调用。它负责序列化请求参数，并构造一个包含 RPC 头和参数的完整请求消息，然后通过网络发送。
    //接收和反序列化响应：MprpcChannel 还负责接收服务器的响应，反序列化响应数据，并将其传递回发起调用的客户端。
};

#endif