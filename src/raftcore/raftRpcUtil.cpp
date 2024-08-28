#include "raftRpcUtil.h"

#include <mprpcchannel.h>
#include <mprpcchannel.cpp>

bool RaftRpcUtil::AppendEntries(raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *response)
{
    MprpcController controller;
    stub_->AppendEntries(&controller, args, response, nullptr);
    return !controller.Failed(); //不失败就是成功
}

bool RaftRpcUtil::InstallSnapshot(raftRpcProctoc::InstallSnapshotRequest *args, raftRpcProctoc::InstallSnapshotResponse *response)
{
    MprpcController controller;
    stub_->InstallSnapshot(&controller, args, response, nullptr);
    return !controller.Failed(); //不失败就是成功
}

bool RaftRpcUtil::RequestVote(raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *response)
{
    MprpcController controller;
    stub_->RequestVote(&controller, args, response, nullptr);
    return !controller.Failed(); //不失败就是成功
}

RaftRpcUtil::RaftRpcUtil(std::string ip, short port)
{   
    //使用提供的 IP 和端口创建一个 MprpcChannel 对象，并用它来创建一个 raftRpc_Stub 对象。这个 Stub 对象是客户端用于发起 RPC 调用的代理。
    stub_->new raftRpcProtoc::raftRpc_Stub(new MprpcChannel(ip, port, true));
}
RaftRpcUtil::~RaftRpcUtil() {
    delete stub_; //必须释放指针的呀
}

/**
 * RaftRpcUtil::AppendEntries, RaftRpcUtil::InstallSnapshot, 和 RaftRpcUtil::RequestVote 这三个函数是客户端调用的接口，用于通过 RPC 向服务器发送请求。
每个函数都创建了一个 MprpcController 对象（这里的 MprpcController 可能是 google::protobuf::RpcController 的一个别名或者封装，用于控制 RPC 调用的状态）。
然后使用 stub_->AppendEntries, stub_->InstallSnapshot, 和 stub_->RequestVote 这三个函数调用服务器上的对应 RPC 方法。这里的 stub_ 是一个存根对象，它代表了服务器端的接口，用于发起 RPC 调用。
每个函数的最后一个参数 nullptr 表示这个 RPC 调用是同步的，不需要通过回调函数（Closure）来处理异步结果。
函数返回 !controller.Failed()，这表示如果 RPC 调用成功（没有失败），则返回 true。
*/

/**
 * 调用流程
客户端创建请求对象，填充数据，然后调用 RaftRpcUtil 类的相应函数。
RaftRpcUtil 类的函数通过 stub_ 发起 RPC 调用，将请求发送到服务器。
服务器端的 gRPC 框架接收到 RPC 请求，并调用 Raft 类的相应函数。
Raft 类的函数执行业务逻辑，填充响应对象，然后通过 done->Run() 完成 RPC 调用。
服务器端的 gRPC 框架将响应发送回客户端。
客户端接收到响应，根据需要处理结果。
*/
