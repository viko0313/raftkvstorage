#include "clerk.h"

#include "raftServerRpcUtil.h"

#include "../common/include/util.h"

#include <string>
#include <vector>

// Clerk类的Get方法，用于从服务器获取键值
std::string Clerk::Get(std::string key) {
  // 增加请求ID，用于标识请求
  m_requestId++;
  auto requestId = m_requestId;
  // 选择最近一次成功交互的服务器作为请求的服务器
  int server = m_recentLeaderId;
  // 构建获取请求的参数
  raftKVRpcProctoc::GetArgs args;
  args.set_key(key);  // 设置要获取的键
  args.set_clientid(m_clientId);  // 设置客户端ID
  args.set_requestid(requestId);  // 设置请求ID

  // 无限循环，直到成功获取值或确定键不存在
  while (true) {
    // 构建获取请求的回复对象
    raftKVRpcProctoc::GetReply reply;
    // 向选定的服务器发送Get请求
    bool ok = m_servers[server]->Get(&args, &reply);
    // 如果请求失败，或者回复错误码为ErrWrongLeader（即当前服务器不是领导者）
    if (!ok || reply.err() == ErrWrongLeader) {
      // 更新服务器为下一个（循环选择），并继续循环
      server = (server + 1) % m_servers.size();
      continue;
    }
    // 如果回复错误码为ErrNoKey，表示键不存在
    if (reply.err() == ErrNoKey) {
      return "";  // 返回空字符串表示键不存在
    }
    // 如果回复错误码为OK，表示获取成功
    if (reply.err() == OK) {
      // 更新最近一次成功交互的服务器ID（因为代
      m_recentLeaderId = server;
      // 返回获取到的值
      return reply.value();
    }
  }
  // 如果因为某些未知原因退出循环，返回空字符串
  return "";
}

void Clerk::PutAppend(std::string key, std::string value, std::string op) {
    m_requestId++;
    auto requestId = m_requestId;
    auto server = m_recentLeaderId;
    raftKVRpcProctoc::PutAppendArgs args;
    args.set_key(key);
    args.set_value(value);
    args.set_op(op);
    args.set_clientid(m_clientId);
    args.set_requestid(requestId);
    while (true) {
        // raftKVRpcProctoc::PutAppendArgs args;
        // args.set_key(key);
        // args.set_value(value);
        // args.set_op(op);
        // args.set_clientid(m_clientId);
        // args.set_requestid(requestId);
        raftKVRpcProctoc::PutAppendReply reply;
        bool ok = m_server[server]->PutAppend(&args, &reply);

        if (!ok || reply.err() == ErrWrongLeader) {
            DPrintf("【Clerk::PutAppend】原以为的leader：{%d}请求失败，向新leader{%d}重试  ，操作：{%s}", server, server + 1,
                    op.c_str());
            if (!ok) {
                DPrintf("重试原因 ，rpc失敗 ，");
            }
            if (reply.err() == ErrWrongLeader) {
                DPrintf("重試原因：非leader");
            }
            server = (server + 1) % m_servers.size();  // try the next server
            continue;
        }
        if (reply.err() == ok) { //什么时候reply errno为ok呢？？？
            m_recentLeaderId = server;
            return;
        }
    }
}

void Clerk::Put(std::string key, std::string value) { PutAppend(key, value, "Put"); }

void Clerk::Append(std::string key, std::string value) { PutAppend(key, value, "Append"); }

void Clerk::Init(std::string configFileName) {
  //获取所有raft节点ip、port ，并进行连接
  MprpcConfig config;
  //todo: MprpcConfig有点手生
  config.LoadConfigFile(configFileName.c_str());
  std::vector<std::pair<std::string, short>> ipPortVt;
  for (int i = 0; i < INT_MAX - 1; ++i) {
    std::string node = "node" + std::to_string(i);

    std::string nodeIp = config.Load(node + "ip");
    std::string nodePortStr = config.Load(node + "port");
    if (nodeIp.empty()) {
      break;
    }
    ipPortVt.emplace_back(nodeIp, atoi(nodePortStr.c_str()));  //沒有atos方法，可以考慮自己实现
  }
  //进行连接
  for (const auto& item : ipPortVt) {
    std::string ip = item.first;
    short port = item.second;
    // 2024-01-04 todo：bug fix
    auto* rpc = new raftServerRpcUtil(ip, port);
    m_servers.push_back(std::shared_ptr<raftServerRpcUtil>(rpc));
  }
}

Clerk::Clerk() : m_clientId(Uuid()), m_requestId(0), m_recentLeaderId(0) {}