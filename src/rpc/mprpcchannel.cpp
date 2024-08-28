#include "mprpcchannel.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <string>
#include "mprpccontroller.h"
#include "rpcheader.pb.h"
#include "util" //common的


MprpcChannel::MprpcChannel(string ip, short port, bool connectNow) 
    : m_ip(ip),
      m_port(port),
      m_clientFd(-1) 
{   
    //短连接
    //可以允许延迟连接
    if (!connectNow) {
        return;
    }
    std::string errMsg;
    auto rt = newConnect(ip.c_str(), port, &errMsg);
    
    int tryCount = 3;
    while (!rt && tryCount--) {
        std::cout << errMsg << std::endl;
        rt = newConnect(ip.c_str(), port, &errMsg);
    }
}
/*
header_size + service_name method_name args_size + args
*/
// 所有通过stub代理对象调用的rpc方法，都会走到这里了，
// 统一通过rpcChannel来调用方法
// 统一做rpc方法调用的数据数据序列化和网络发送
void MprpcChannel::callMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller,
                              const google::protobuf::Message *request, google::protobuf::Message *response,
                              google::protobuf::Closure *done) override {
    if (m_clientFd == -1) {
        //重连能获得clientfd
        std::string errMsg;
        bool rt = newConnect(m_ip.c_str(), port(), &errMsg);
        if (!rt) {
            DPrintf("[func-MprpcChannel::CallMethod]重连接ip：{%s} port{%d}失败", m_ip.c_str(), m_port);
            controller->SetFailed(errMsg);
            return;
        } else {
            DPrintf("[func-Mprpcchannel::CallMethod]连接ip:{%s} port{%d} 成功", m_ip.c_str(), m_port);
        }
    }
    const google::protobuf::ServiceDescriptor *sd = method->service();
    std::string service_name = sd->name();
    std::string method_name = method->name();

    // 获得参数的序列化字符串长度
    uint32_t args_size{};
    std::string args_str;
    // 将 request 对象序列化，将内部的数据状态转换成一个连续的字符串格式，并存储到 args_str 变量中。
    // SerializeToSting Protobuf 库中的一个成员函数，序列化message类型，返回true
    if (request->SerializeToSting(&args_str))
    {
        args_size = args_str.size();
    }
    else
    {
        controller->SetFailed("serialize request fail!");
        return;
    }

    RPC::RpcHeader rpcHeader;
    rpcHeader.set_service_name(service_name);
    rpcHeader.set_method_name(method_name);
    rpcHeader.set_args_size(args_size);

    std::string rpc_header_str;
    if (!rpcHeader.SerializeToString(&rpc_header_str))
    {
        controller->SetFailed("serialize rpc header error!");
    }
    // rpc头序列化成功=》
    //  使用protobuf的CodedOutputStream来构建发送的数据流
    std::string send_rpc_str; // 存储最终发送的数据
    {
        // 创建一个StringOutputStream用于写入send_rpc_str
        google::protobuf::io::StringOutputStream string_output(&send_rpc_str);
        //创建 CodedOutputStream 对象,CodedOutputStream 是一个低级别的输出流，用于写入 Protocol Buffers 编码的数据类型。它提供了一种高效的方式来写入变长值，如 varint、fixed32、fixed64 等
        google::protobuf::io::CodedOutputStream coded_output(&string_output);
        // WriteVarint32 函数期望一个 uint32_t 类型的参数
        coded_output.WriteVarint32(static_cast<uint32_t>(rpc_header_str.size()));
        // 不需要手动写入header_size，因为上面的WriteVarint32已经包含了header的长度信息
        // 然后写入rpc_header本身
        coded_output.WriteString(rpc_header_str);
    }
    // 最后将请求参数附加到后面
    send_rpc_str += args_str;

    // 现在可以发送rpc请求了,失败会重连再发送，要是重连一次还失败就return
    // sendz在协程hook.cpp
    while (-1 == send(m_clientFd, send_rpc_str.c_str(), send_rpc_str.size(), 0))
    {
        char errtext[512] = {0};
        //但与 printf 将格式化的输出发送到标准输出（通常是屏幕）不同，sprintf 将输出写入到一个指定的字符数组中。
        sprintf(errtext, "send error! errno %d", errno);
        std::cout << "尝试重新连接，对方ip：" << m_ip << " 对方端口" << m_port << std::endl;
        close(m_clientFd);
        m_clientFd = -1; // 还是重置了好点
        std::string errMsg;
        bool rt = newConnect(m_ip.c_str(), m_port, &errMsg);
        if (!rt)
        {
            controller->SetFailed(errMsg);
            return;
        }
    }
    /*
      从时间节点来说，这里将请求发送过去之后rpc服务的提供者就会开始处理，返回的时候就代表着已经返回响应了
      */
    char recv_buf[1024] = {0};
    int recv_size = 0;

    if (-1 == (recv_size = recv(m_clientFd, recv_buf, recv_size, 0))) {
        close(m_clientFd);
        m_clientFd = -1;
        char errtext[512] = {0};
        sprintf(errtext, "recv error! errno: %d", errno);
        controller->SetFailed(errtetx);
        return;
    }

    //从 recv_buf 指向的字节数组中解析数据，recv_size 是数组中字节的数量。如果解析成功，ParseFromArray 将返回 true，并将解析得到的数据存储在 response 对象中。
    // 如果一切顺利，函数正常结束，response 包含了反序列化后的响应数据。
    if (!response->ParseFromArray(recv_buf, recv_size)) {
        char errtext[512] = {0};
        sprintf(errtext, "parse error! errno:%d", errno);
        controller->SetFailed(errtext);
        return;
    }
}

bool MprpcChannel::newConnect(const char *ip, uint16_t port, string *errMsg) {
    int clientfd = socket(AF_INET, SOCK_STREAM, 0); // int hook::socket(int domain, int type, int protocol)
    if (-1 == clientfd) {
        char errtext[512] = {0};
        sprintf(errtext, "create socket error errno:%d", errno);
        m_clientFd = -1;
        *errMsg = errtext;
        return false;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    //转为网络字节序用于网络通信
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    //连接rpc服务节点
    if (-1 == connect(clientfd, (stuct sockaddr*)&server_addr, sizeof(server_addr))) {
        close(clientfd);
        char errtxt[512] = {0};
        sprintf(errtxt, "connect error errno:%d", errno);
        m_clientFd = -1;
        *errMsg = errtxt;
        return false;
    }
    //上面一切顺利
    m_clientFd = clientfd;
    return true;
}