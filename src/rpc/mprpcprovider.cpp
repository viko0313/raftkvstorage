#include "include/rpcprovider.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <string>
#include "rpcheader.pb.h"
#include "util.h"

/*
service=> service服务desc(getDes) =》 name  method_count

再根据method_count=》方法描述 =》 方法名字
方法名字和描述 insert=> methodMap  还有隶属于哪一个service  组成service_info
service_name和 info insert serviceMap
*/
// 这里是框架提供给外部使用的，可以发布rpc方法的函数接口
// 只是简单的把服务描述符和方法描述符全部保存在本地而已
// todo 待修改 要把本机开启的ip和端口写在文件里面
void RpcProvider::NotifyService(google::protobuf::Service *service){
    ServiceInfo service_info;

    //获取服务对象的描述信息
    const google::protobuf::ServiceDescriptor *pserviceDesc = service->GetDescriptor();
    //获取服务名字
    std::string service_name = pserviceDesc->name();
    //获取服务对象的 方法数量
    int methodCnt = pserviceDesc->method_count();

     std::cout << "service_name:" << service_name << std::endl;

     for (int i = 0; i < methodCnt; ++i) {
        //获取服务对象指定下标的方法的描述（抽象描述）
        const google::protobuf::MethodDescriptor *pmethodDesc = pserviceDesc->method(i);
        std::string method_name = pmethodDesc->name();
        service_info.m_methodMap.insert({method_name, pmethodDesc});
     }
     //service_info=> 两个成员m_methodMap和m_service
     service_info.m_service = service;
     m_serviceMap.insert({service_name, service_info});
}

  // 启动rpc服务节点，开始提供rpc远程网络调用服务
void RpcProvider::Run(int nodeIndex, short port) {
    char *ipC;
    char hname[128];
    struct 
}
// 新的socket连接回调
void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &);

// 已建立连接用户的读写事件回调
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &, muduo::net::Buffer *, muduo::Timestamp);
// Closure的回调操作，用于序列化rpc的响应和网络发送
void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &, google::protobuf::Message *);

RpcProvider~RpcProvider() {

}