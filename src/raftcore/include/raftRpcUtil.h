#ifndef RAFTRPCUTIL_H
#define RAFTRPCUTIL_H

#include "raftRPc.pb.h"

class RaftRpcUtil {
private:
    raftRpcProtoc::raftRpc_Stub *stub_;
public:
    //主动调用其他节点的三个方法,可以按照mit6824来调用，但是别的节点调用自己的好像就不行了，要继承protoc提供的service类才行
    bool AppendEntries(raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *response);
    bool InstallSnapshot(raftRpcProctoc::InstallSnapshotRequest *args, raftRpcProctoc::InstallSnapshotResponse *response);
    bool RequestVote(raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *response); 
    //获得其他节点响应的方法

    RaftRpcUtil(std::string ip, short port);
    ~RaftRpcUtil();
}

#endif