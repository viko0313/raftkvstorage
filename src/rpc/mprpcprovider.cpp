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
    char hname[128]
        // struct hostent {
        // char    *h_name;        /* 官方主机名 */
        // char    **h_aliases;    /* 别名列表 */
        // int     h_addrtype;     /* 主机地址类型 */
        // int     h_length;       /* 主机地址长度 */
        // char    **h_addr_list;  /* 地址列表 *//
    gethostname(hname, sizeof(hname));
    struct hostent *hent;
    hent = gethostbyname(hname);
    for (int i = 0; hent->h_addr_list[i]; i++) {
        //inet_ntoa函数将IP地址的二进制形式转换为可读的点分十进制形式。
        ipC = inet_ntoa(*(struct in_addr *)(hent->h_addr_list[i]));  // IP地址
    } 
    std::string ip = std::string(ipC);
    std::string node = "node" + std::to_string(nodeIndex);
    //ostrem是写入·
    std::ofstream outfile;
    //std::ios::app 是一个标志，指示文件应该以追加模式打开，这意味着写入操作会在文件的末尾添加内容，而不是覆盖现有内容
    outfile.open("test.conf", std::ios::app);  //打开文件并追加写入
    if (!outfile.is_open()) {
        std::cout << "打开文件失败！" << std::endl;
        exit(EXIT_FAILURE);
    }
    outfile << node + "ip=" + ip << std::endl;
    outfile << node + "port=" + std::to_string(port) << std::endl;
    outfile.close();

    //创建muduoserver
    muduo::net::InetAddress address(ip, port);
    m_muduo_Server = std::make_shared<muduo::net::TcpServer>(&m_eventLoop, address, "RpcProvider");
    
    // 绑定连接回调和消息读写回调方法  分离了网络代码和业务代码
    /*
    bind的作用：
    如果不使用std::bind将回调函数和TcpConnection对象绑定起来，那么在回调函数中就无法直接访问和修改TcpConnection对象的状态。因为回调函数是作为一个独立的函数被调用的，它没有当前对象的上下文信息（即this指针），也就无法直接访问当前对象的状态。
    如果要在回调函数中访问和修改TcpConnection对象的状态，需要通过参数的形式将当前对象的指针传递进去，并且保证回调函数在当前对象的上下文环境中被调用。这种方式比较复杂，容易出错，也不便于代码的编写和维护。因此，使用std::bind将回调函数和TcpConnection对象绑定起来，可以更加方便、直观地访问和修改对象的状态，同时也可以避免一些常见的错误。
    */
    m_muduo_server->setConnectionCallback(std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
    m_muduo_server->setMessageCallback(
        std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    // 设置muduo库的线程数量
    m_muduo_server->setThreadNum(4);

    // rpc服务端准备启动，打印信息
    std::cout << "RpcProvider start service at ip:" << ip << " port:" << port << std::endl;

    // 启动网络服务
    m_muduo_server->start();
    m_eventLoop.loop();
    /*
    这段代码是在启动网络服务和事件循环，其中server是一个TcpServer对象，m_eventLoop是一个EventLoop对象。

    首先调用server.start()函数启动网络服务。在Muduo库中，TcpServer类封装了底层网络操作，包括TCP连接的建立和关闭、接收客户端数据、发送数据给客户端等等。通过调用TcpServer对象的start函数，可以启动底层网络服务并监听客户端连接的到来。

    接下来调用m_eventLoop.loop()函数启动事件循环。在Muduo库中，EventLoop类封装了事件循环的核心逻辑，包括定时器、IO事件、信号等等。通过调用EventLoop对象的loop函数，可以启动事件循环，等待事件的到来并处理事件。

    在这段代码中，首先启动网络服务，然后进入事件循环阶段，等待并处理各种事件。网络服务和事件循环是两个相对独立的模块，它们的启动顺序和调用方式都是确定的。启动网络服务通常是在事件循环之前，因为网络服务是事件循环的基础。启动事件循环则是整个应用程序的核心，所有的事件都在事件循环中被处理。
    */
}
// 新的socket连接回调
void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &conn) {
    // 如果是新连接就什么都不干，即正常的接收连接即可
    if (!conn->connected()) {
        conn->shutdown();
    }
}

// 已建立连接用户的读写事件回调
// 已建立连接用户的读写事件回调 如果远程有一个rpc服务的调用请求，那么OnMessage方法就会响应
// 这里来的肯定是一个远程调用请求
// 因此本函数需要：解析请求，根据服务名，方法名，参数，来调用service的来callmethod来调用本地的业务
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buffer, muduo::Timestamp) {
    std::string recv_buf = buffer->retrieveAllAsString();

    google::protobuf::io::ArrayInputStream array_input(recv_buf.data(), recv_buf.size());
    google::protobuf::io::CodedInputStream coded_input(&array_input);
    uint32_t header_size{};

    coded_input.ReadVarint32(&header_size);

    std::string rpc_header_str;
    RPC::RpcHeader rpcheader;
    std::string service_name;
    std::string method_name;

    google::protobuf::io::CodedInputStream::Limit msg_limit = coded_input.PushLimit(header_size);
    coded_input.ReadString(&rpc_header_str, header_size); //第一参数为位置，第二是长度
    //解除限制，继续读取其他设置
    coded_input.PopLimit(msg_limit);
    uint32_t args_size{};

    if (rpcheader.ParseFromString(rpc_header_str)) {
        //对数据头反序列化，保存入RPC::RpcHeader rpcheader;
        service_name = rpcheader.service_name();
        method_name = rpcheader.method_name();
        args_size = rpcheader.args_size();
    }
    else {
        // 数据头反序列化失败
        std::cout << "rpc_header_str:" << rpc_header_str << " parse error!" << std::endl;
        return;
    }

    //获取rpc方法 参数的字符流数据
    std::string args_str;
    bool read_args_success = coded_input.ReadString(&args_str, &args_size);

    if (!read_args_success) {
        //
        std::cout<< "参数读取失败" << std::endl;
    }

    auto it = m_serviceMap.find(service_name);
    if (it == m_serviceMap) {
        std::cout << "服务：" << service_name << " 不存在的哇" << std::endl;
        std::cout << "当前服务有："<< endl;
        for (auto it : m_serviceMap) {
            std::cout << it.first << " ";
        }
        std::cout << std::endl;
        return;
    }
    //再找一下方法名
    auto mit = it->second.m_methodMap.find(method_name);
    if (mit == it->second.m_methodMap.end()) {
        std::cout << service_name << ":" << method_name << " is not exist!" << std::endl;
        return;
    }

    google::protobuf::Service *service = it->second.m_service;
    const google::protobuf::MethodDescriptor *method = mit->second;

    google::protobuf::Message *request = service->GetRequestPrototype(method).New();
    if (!request->ParseFromString(args_str)) {
        std::cout << "request parse error, content:" << args_str << std::endl;
        return;
    }

    google::protobuf::Message *response = service->GetResponsePrototype(method).New();
    // 给下面的method方法的调用，绑定一个Closure的回调函数
    // closure是执行完本地方法之后会发生的回调，因此需要完成序列化和反向发送请求的操作
    google::protobuf::Closure *done =
        google::protobuf::NewCallback<RpcProvider, const muduo::net::TcpConnectionPtr &, google::protobuf::Message *>(
            this, &RpcProvider::SendRpcResponse, conn, response);

    // 在框架上根据远端rpc请求，调用当前rpc节点上发布的方法
    // new UserService().Login(controller, request, response, done)

    /*
    为什么下面这个service->CallMethod 要这么写？或者说为什么这么写就可以直接调用远程业务方法了
    这个service在运行的时候会是注册的service
    // 用户注册的service类 继承 .protoc生成的serviceRpc类 继承 google::protobuf::Service
    // 用户注册的service类里面没有重写CallMethod方法，是 .protoc生成的serviceRpc类 里面重写了google::protobuf::Service中
    的纯虚函数CallMethod，而 .protoc生成的serviceRpc类 会根据传入参数自动调取 生成的xx方法（如Login方法），
    由于xx方法被 用户注册的service类 重写了，因此这个方法运行的时候会调用 用户注册的service类 的xx方法
    真的是妙呀
    */
    //真正调用方法
    service->CallMethod(method, nullptr, request, response, done);

}
// Closure的回调操作，用于序列化rpc的响应和网络发送
void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message *response) {
    std::string response_str;
    if (response->SerializeToString(response_str)) {
        //response序列化就发送
        conn->send();
    } else {
        std::cout << "SerializeToString response_str error! " <<endl;

    }
    //    conn->shutdown(); // 模拟http的短链接服务，由rpcprovider主动断开连接  //改为长连接，不主动断开
}

RpcProvider~RpcProvider() {
    std::cout << "[func - RpcProvider::~RpcProvider()]: ip和port信息：" << m_muduo_server->ipPort() << std::endl;
    m_eventLoop.quit();
    //    m_muduo_server.   怎么没有stop函数，奇奇怪怪，看csdn上面的教程也没有要停止，甚至上面那个都没有
}
}