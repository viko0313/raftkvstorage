#include "kvServer.h"

#include <rpcprovider.h>

#include "mprpcconfig.h"

void kvServer::StartKVServer() {}

// 打印 KvServer 内部的键值对数据库
void kvServer::DprintfKVDB() {
    if (!Debug) {
        return;
    }
    std::lock_guard<std::mutex> lg(m_mtx);
    DEFER {
        m_skipList.display_list();
    };
    // DEFER 宏可以明确地指出哪些操作是函数退出时必须执行的
}

// 在 KvServer 的键值对数据库上执行 Append 操作
void KvServer::ExecuteAppendOpOnKVDB(Op op) {
    // if op.IfDuplicate {   //get请求是可重复执行的，因此可以不用判复
    //	return
    // }
    m_mtx.lock();
     //skiplist的优化
    m_skipList.insert_set_element(op.Key, op.Value);
    //不再使用m_KvDB了

    // if (m_kvDB.find(op.Key) != m_kvDB.end()) {
    //     m_kvDB[op.Key] = m_kvDB[op.Key] + op.Value;
    // } else {
    //     m_kvDB.insert(std::make_pair(op.Key, op.Value));
    // }
    m_lastRequestId[op.ClientId] = op.RequestId;
    m_mtx.unlock();

    //    DPrintf("[KVServerExeAPPEND-----]ClientId :%d ,RequestID :%d ,Key : %v, value : %v", op.ClientId, op.RequestId,
    //    op.Key, op.Value)
    DprintfKVDB();
}

// 在 KvServer 的键值对数据库上执行 Get 操作
void kvServer::ExecuteGetOpOnKVDB(Op op, std::string *value, bool *exist) {
    m_mtx.lock();
    *value = "";
    *exist = false;
    if (m_skipList.search_element(op.Key, *value)) {
        *exist = true;
        //op.value = value;
        //search_element 函数会将对应的值赋给 *value 并返回 true。
    }

    m_lastRequestId[op.ClientId] = op.RequestId; //跟踪最后一个请求
    m_mtx.unlock();
    if( *exist) {

    }else {

    }
    DprintfKVDB();
}

// 在 KvServer 的键值对数据库上执行 Put 操作, 和append几乎一样
void kvServer::ExecutePutOpOnKVDB(Op op) {
    m_mtx.lock();
    m_skipList.insert_set_element(op.Key, op.Value);
    m_lastRequestId[op.ClientId] = op.RequestId;
    m_mtx.unlock();
    DprintfKVDB();
}

// 将 GetArgs 改为rpc调用的，因为是远程客户端，即服务器宕机对客户端来说是无感的,这个在raftKVRpcProctoc
void kvServer::Get(const raftKVRpcProctoc::GetArgs *args,
                   raftKVRpcProctoc::GetReply
                   *reply) {
    Op op;
    op.Operation = "Get";
    op.Key = args->key();
    op.Value = "";
    op.ClientId = args->clientid();
    op.RequestId = args->requestid();

    int raftIndex = -1;
    int _ = -1;
    bool isLeader = false;
    m_raftNode->Start(op, &raftIndex, &_,
                        &isLeader);  // raftIndex：raft预计的logIndex
                                    // ，虽然是预计，但是正确情况下是准确的，op的具体内容对raft来说 是隔离的

    if (!isLeader) {
        reply->set_err(ErrWrongLeader);
        return;
    }
    m_mtx.lock();

    if (waitApplyCh.find(raftIndex) == waitApplyCh.end()) {
        waitApplyCh.insert(std::make_pair(raftIndex, new LockQueue<Op>()));
    }
    auto chForRaftIndex = waitApplyCh[raftIndex];
    m_mtx.unlock(); //直接解锁，等待任务执行完成，不能一直拿锁等待
    Op raftCommitOp;
    if (!chForRaftIndex->timeOutPop(CONSENSUS_TIMEOUT, &raftCommitOp)) {
    //        DPrintf("[GET TIMEOUT!!!]From Client %d (Request %d) To Server %d, key %v, raftIndex %d", args.ClientId,
    //        args.RequestId, kv.me, op.Key, raftIndex)
    // todo 2023年06月01日
    int _ = -1;
    bool isLeader = false;
    m_raftNode->GetState(&_, &isLeader);

    if (ifRequestDuplicate(op.ClientId, op.RequestId) && isLeader) {
        //如果超时，代表raft集群不保证已经commitIndex该日志，但是如果是已经提交过的get请求，是可以再执行的。
        // 不会违反线性一致性
        std::string value;
        bool exist = false;
        ExecuteGetOpOnKVDB(op, &value, &exist);
        if (exist) {
            reply->set_err(OK);
            reply->set_value(value);
        } else {
            reply->set_err(ErrNoKey);
            reply->set_value("");
        }
        } else {
        reply->set_err(ErrWrongLeader);  //返回这个，其实就是让clerk换一个节点重试
        }
        /**
         * 获取当前节点状态：调用 m_raftNode->GetState(&_, &isLeader) 来检查当前节点是否是领导者。
            处理重复请求：如果当前节点是领导者，并且之前已经处理过相同的请求（通过 ifRequestDuplicate(op.ClientId, op.RequestId) 检查），则可以直接重新执行该请求。
            执行 Get 操作：通过 ExecuteGetOpOnKVDB(op, &value, &exist) 在键值数据库上执行 Get 操作，检查键是否存在，并获取其值。
            设置响应：根据键是否存在，设置响应的错误码和值。
        */
    } else {
        // raft已经提交了该command（op），可以正式开始执行了 ,没超时说明整个集群同步了
        //         DPrintf("[WaitChanGetRaftApplyMessage<--]Server %d , get Command <-- Index:%d , ClientId %d, RequestId
        //         %d, Opreation %v, Key :%v, Value :%v", kv.me, raftIndex, op.ClientId, op.RequestId, op.Operation, op.Key,
        //         op.Value)
        //如果 raftCommitOp 成功从通道中获取（即没有超时），则检查这个操作是否与当前请求匹配（通过客户端 ID 和请求 ID）。
        //如果匹配，执行 Get 操作并设置响应。
        // todo 这里还要再次检验的原因：感觉不用检验，因为leader只要正确的提交了，那么这些肯定是符合的
        if (raftCommitOp.ClientId == op.ClientId && raftCommitOp.RequestId == op.RequestId) {
        std::string value;
        bool exist = false;
        ExecuteGetOpOnKVDB(op, &value, &exist);
        if (exist) {
            reply->set_err(OK);
            reply->set_value(value);
        } else {
            reply->set_err(ErrNoKey);
            reply->set_value("");
        }
        } else {
        reply->set_err(ErrWrongLeader);
        //            DPrintf("[GET ] 不满足：raftCommitOp.ClientId{%v} == op.ClientId{%v} && raftCommitOp.RequestId{%v}
        //            == op.RequestId{%v}", raftCommitOp.ClientId, op.ClientId, raftCommitOp.RequestId, op.RequestId)
        }
    }
    m_mtx.lock();  // todo 這個可以先弄一個defer，因爲刪除優先級並不高，先把rpc發回去更加重要
    auto tmp = waitApplyCh[raftIndex];
    waitApplyCh.erase(raftIndex);
    delete tmp;
    m_mtx.unlock();
} 
/**
 * 從raft節點中獲取消息  （不要誤以爲是執行【GET】命令）
 * @param message
 */
// 定义一个函数，该函数接收从Raft协议的ApplyMsg消息，并处理其中的命令
void KvServer::GetCommandFromRaft(ApplyMsg message) {
  // 声明一个Op类型的变量，用于解析和存储收到的命令
  Op op;
  // 从message.Command字符串中解析出Op对象
  op.parseFromString(message.Command);

  // 使用DPrintf打印调试信息，显示收到的命令的详细信息
  DPrintf(
      "[KvServer::GetCommandFromRaft-kvserver{%d}] , Got Command --> Index:{%d} , ClientId {%s}, RequestId {%d}, "
      "Operation {%s}, Key :{%s}, Value :{%s}",
      // 打印服务器自己的ID
      m_me, 
      // 打印命令在Raft日志中的索引
      message.CommandIndex, 
      // 打印命令的客户端ID
      &op.ClientId, 
      // 打印命令的请求ID
      op.RequestId, 
      // 打印操作类型（Put或Append）
      &op.Operation, 
      // 打印操作的键
      &op.Key, 
      // 打印操作的值
      &op.Value);

  // 如果收到的命令索引小于或等于最后一个快照的Raft日志索引，则忽略此命令
  if (message.CommandIndex <= m_lastSnapShotRaftLogIndex) {
    return;
  }

  // 在状态机中处理命令，解决重复命令的问题
  // 如果命令不是重复的，则执行命令
  if (!ifRequestDuplicate(op.ClientId, op.RequestId)) {
    // 根据操作类型执行相应的操作
    if (op.Operation == "Put") {
      // 执行Put操作
      ExecutePutOpOnKVDB(op);
    }
    if (op.Operation == "Append") {
      // 执行Append操作
      ExecuteAppendOpOnKVDB(op);
    }
    // 这里的注释提到，在Executexxx函数里面更新kv.lastRequestId[op.ClientId] = op.RequestId
  }

  // 如果设置了最大Raft状态并且当前状态达到了这个阈值
  if (m_maxRaftState != -1) {
    // 检查是否需要发送快照命令
    IfNeedToSendSnapShotCommand(message.CommandIndex, 9);
    // 这里的9可能是一个特定的阈值，用于决定何时触发快照
  }
  // 将消息发送到客户端的等待通道
  SendMessageToWaitChan(op, message.CommandIndex);
}

// 检查请求是否重复
bool kvServer::ifRequestDuplicate(std::string ClientId, int RequestId) {
    std::lock_guard<std::mutex> lg(m_mtx);
    if (m_lastRequestId.find(ClientId) == m_lastRequestId.end()) {
        return false;
        // todo :不存在这个client就创建
    }
    return RequestId <= m_lastRequestId[ClientId];
}

// clerk 使用RPC远程调用, 处理 Put 或 Append 请求，参数为请求和响应对象
// get和put//append執行的具體細節是不一樣的
// PutAppend在收到raft消息之後執行，具體函數裏面只判斷冪等性（是否重複）
// get函數收到raft消息之後在，因爲get無論是否重複都可以再執行
void kvServer::PutAppend(const raftKVRpcProctoc::PutAppendArgs *args, raftKVRpcProctoc::PutAppendReply *reply) {
    Op op;
    op.Key = args->key();
    op.Value = args->value();
    op.Operation = args->op();
    op.ClientId = args->clientid();
    op.RequestId = args->requestid();
    int raftIndex = -1;
    int _ = -1;
    bool isleader = false;
    //开始一个 Raft 日志条目
    m_raftNode->Start(op, &raftIndex, &_, &isleader);
    if (!isleader) {
        DPrintf(
            "[func -KvServer::PutAppend -kvserver{%d}]From Client %s (Request %d) To Server %d, key %s, raftIndex %d , but "
            "not leader",
            m_me, &args->clientid(), args->requestid(), m_me, &op.Key, raftIndex);

        reply->set_err(ErrWrongLeader);
        return;
    }
    DPrintf(
        "[func -KvServer::PutAppend -kvserver{%d}]From Client %s (Request %d) To Server %d, key %s, raftIndex %d , is "
        "leader ",
        m_me, &args->clientid(), args->requestid(), m_me, &op.Key, raftIndex);
    m_mtx.lock();
    //waitApplyCh（等待应用通道）通常用于管理那些已经由领导者复制到跟随者但还未应用到状态机（例如，键值存储）的日志条目。
    if (waitApplyCh.find(raftIndex) == waitApplyCh.end()) {
        waitApplyCh.insert(std::make_pair(raftIndex, new LockQueue<Op>()));
    }
    auto chForRaftIndex = waitApplyCh[raftIndex];

    m_mtx.unlock();  //直接解锁，等待任务执行完成，不能一直拿锁等待

    // timeout
    Op raftCommitOp;
    if (!chForRaftIndex->timeOutPop(CONSENSUS_TIMEOUT, &raftCommitOp)) {
        DPrintf(
            "[func -KvServer::PutAppend -kvserver{%d}]TIMEOUT PUTAPPEND !!!! Server %d , get Command <-- Index:%d , "
            "ClientId %s, RequestId %s, Opreation %s Key :%s, Value :%s",
            m_me, m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);

        if (ifRequestDuplicate(op.ClientId, op.RequestId)) {
        reply->set_err(OK);  // 超时了,但因为是重复的请求，返回ok，实际上就算没有超时，在真正执行的时候也要判断是否重复
        } else {
        reply->set_err(ErrWrongLeader);  ///这里返回这个的目的让clerk重新尝试
        }
    } else {
        DPrintf(
            "[func -KvServer::PutAppend -kvserver{%d}]WaitChanGetRaftApplyMessage<--Server %d , get Command <-- Index:%d , "
            "ClientId %s, RequestId %d, Opreation %s, Key :%s, Value :%s",
            m_me, m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);
        if (raftCommitOp.ClientId == op.ClientId && raftCommitOp.RequestId == op.RequestId) {
        //可能发生leader的变更导致日志被覆盖，因此必须检查
        reply->set_err(OK);
        } else {
        reply->set_err(ErrWrongLeader);
        }
    }
    m_mtx.lock();
    auto tmp = waitApplyCh[raftIndex];
    waitApplyCh.erase(raftIndex);
    delete tmp;
    m_mtx.unlock();
}

// 一直等待raft传来的applyCh ,循环读取
void kvServer::ReadRaftApplyCommandLoop() {
    while (true) {
        //如果只操作applyChan不用拿锁，因为applyChan自己带锁
        auto message = applyChan->Pop();  //阻塞弹出
        DPrintf(
            "---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{%d}] 收到了下raft的消息",
            m_me);
        // listen to every command applied by its raft ,delivery to relative RPC Handler

        if (message.CommandValid) {
            GetCommandFromRaft(message);
        }
        if (message.SnapshotValid) {
            GetSnapShotFromRaft(message);
        }
    }
}
// 读取并安装快照
// raft会与persist层交互，kvserver层也会，因为kvserver层开始的时候需要恢复kvdb的状态
//  关于快照raft层与persist的交互：保存kvserver传来的snapshot；生成leaderInstallSnapshot RPC的时候也需要读取snapshot；
//  因此snapshot的具体格式是由kvserver层来定的，raft只负责传递这个东西
//  snapShot里面包含kvserver需要维护的persist_lastRequestId 以及kvDB真正保存的数据persist_kvdb
void KvServer::ReadSnapShotToInstall(std::string snapshot) {
  if (snapshot.empty()) {
    // bootstrap without any state?
    return;
  }
  parseFromString(snapshot);

  //    r := bytes.NewBuffer(snapshot)
  //    d := labgob.NewDecoder(r)
  //
  //    var persist_kvdb map[string]string  //理应快照
  //    var persist_lastRequestId map[int64]int //快照这个为了维护线性一致性
  //
  //    if d.Decode(&persist_kvdb) != nil || d.Decode(&persist_lastRequestId) != nil {
  //                DPrintf("KVSERVER %d read persister got a problem!!!!!!!!!!",kv.me)
  //        } else {
  //        kv.kvDB = persist_kvdb
  //        kv.lastRequestId = persist_lastRequestId
  //    }
}

// 将操作发送到等待队列
bool kvServer::SendMessageToWaitChan(const Op &op, int raftIndex) {
    std::lock_guard<std::mutex> lg(m_mtx);
    DPrintf(
        "[RaftApplyMessageSendToWaitChan--> raftserver{%d}] , Send Command --> Index:{%d} , ClientId {%d}, RequestId "
        "{%d}, Opreation {%v}, Key :{%v}, Value :{%v}",
        m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);

    if (waitApplyCh.find(raftIndex) == waitApplyCh.end()) {
        return false;
    }
    waitApplyCh[raftIndex]->Push(op); // 就是这个
    DPrintf(
        "[RaftApplyMessageSendToWaitChan--> raftserver{%d}] , Send Command --> Index:{%d} , ClientId {%d}, RequestId "
        "{%d}, Opreation {%v}, Key :{%v}, Value :{%v}",
        m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);
    return true;
}

// 检查是否需要制作快照，需要的话就向raft之下制作快照
/**
 * 在 kvServer 层：
IfNeedToSendSnapShotCommand 被调用，以决定是否需要创建快照。
如果需要，MakeSnapShot 被调用来创建快照数据。
创建的快照数据通过 m_raftNode->Snapshot 发送到 Raft 层。
在 Raft 层：
Snapshot 方法接收从 kvServer 层发送的快照数据。
它处理这个快照，更新 Raft 层的状态，并持久化这些更改。
快照的接收和应用：
当 Raft 层处理完快照后，可能需要将快照数据传递回 kvServer 层，这时 GetSnapShotFromRaft 会被调用来处理这些数据。
*/
void kvServer::IfNeedToSendSnapShotCommand(int raftIndex, int proportion) {
    if (m_raftNode->GetRaftStateSize() > m_maxRaftState / 10.0) {
        auto snapshot = MakeSnapshot();
        m_raftNode->Snapshot(raftIndex, snapshot);
    }
}

// Handler the SnapShot from kv.rf.applyCh
// 处理从 Raft 节点接收到的快照
void kvServer::GetSnapShotFromRaft(ApplyMsg message) {
    std::lock_guard<std::mutex> lg(m_mtx);
    if (m_raftNode->CondInstallSnapshot(message.SnapshotTerm, message.SnapshotIndex, message.Snapshot)) {
        ReadSnapshotToInstall(message.Snapshot);
        m_lastSnapshotRaftLogIndex = message.SnapshotIndex;
    }
}

std::string kvServer::MakeSnapShot() {
    std::lock_guard<std::mutex> lg(m_mtx);
    std::string snapshotdata = getSnapshotData();
    return snapshotdata;
}

void KvServer::PutAppend(google::protobuf::RpcController *controller, const ::raftKVRpcProctoc::PutAppendArgs *request,
                         ::raftKVRpcProctoc::PutAppendReply *response, ::google::protobuf::Closure *done) {
    KvServer::PutAppend(request, response);
    done->Run();
}

void KvServer::Get(google::protobuf::RpcController *controller, const ::raftKVRpcProctoc::GetArgs *request,
                   ::raftKVRpcProctoc::GetReply *response, ::google::protobuf::Closure *done) {
    KvServer::Get(request, response);
    done->Run();
}

KvServer::KvServer(int me, int maxraftstate, std::string nodeInforFileName, short port) : m_skipList(6) {
    std::shared_ptr<Persister> persister = std::make_shared<Persister>(me);

    m_me = me;
    m_maxRaftState = maxraftstate;
    applyChan = std::make_shared<LockQueue<ApplyMsg> >();

    m_raftNode = std::make_shared<Raft>();

    std::thread t([this, port]() -> void {
        // provider是一个rpc网络服务对象。把UserService对象发布到rpc节点上
        RpcProvider provider;
        provider.NotifyService(this);
        provider.NotifyService(
            this->m_raftNode.get());  // todo：这里获取了原始指针，后面检查一下有没有泄露的问题 或者 shareptr释放的问题
        // 启动一个rpc服务发布节点   Run以后，进程进入阻塞状态，等待远程的rpc调用请求
        provider.Run(m_me, port);
        });
    t.detach();

    ////开启rpc远程调用能力，需要注意必须要保证所有节点都开启rpc接受功能之后才能开启rpc远程调用能力
    ////这里使用睡眠来保证
    std::cout << "raftServer node:" << m_me << " start to sleep to wait all ohter raftnode start!!!!" << std::endl;
    sleep(6);
    std::cout << "raftServer node:" << m_me << " wake up!!!! start to connect other raftnode" << std::endl;
    //获取所有raft节点ip、port ，并进行连接  ,要排除自己
    MprpcConfig config;
    config.LoadConfigFile(nodeInforFileName.c_str());
    std::vector<std::pair<std::string, short> > ipPortVt;
    for (int i = 0; i < INT_MAX - 1; ++i) {
        std::string node = "node" + std::to_string(i);

        std::string nodeIp = config.Load(node + "ip");
        std::string nodePortStr = config.Load(node + "port");
        if (nodeIp.empty()) {
        break;
        }
        ipPortVt.emplace_back(nodeIp, atoi(nodePortStr.c_str()));  //沒有atos方法，可以考慮自己实现
    }
    std::vector<std::shared_ptr<RaftRpcUtil> > servers;
    //进行连接
    for (int i = 0; i < ipPortVt.size(); ++i) {
        if (i == m_me) {
        servers.push_back(nullptr);
        continue;
        }
        std::string otherNodeIp = ipPortVt[i].first;
        short otherNodePort = ipPortVt[i].second;
        auto *rpc = new RaftRpcUtil(otherNodeIp, otherNodePort);
        servers.push_back(std::shared_ptr<RaftRpcUtil>(rpc));

        std::cout << "node" << m_me << " 连接node" << i << "success!" << std::endl;
    }
    sleep(ipPortVt.size() - me);  //等待所有节点相互连接成功，再启动raft
    m_raftNode->init(servers, m_me, persister, applyChan);
    // kv的server直接与raft通信，但kv不直接与raft通信，所以需要把ApplyMsg的chan传递下去用于通信，两者的persist也是共用的

    //////////////////////////////////

    // You may need initialization code here.
    // m_kvDB; //kvdb初始化
    m_skipList;
    waitApplyCh;
    m_lastRequestId;
    m_lastSnapShotRaftLogIndex = 0;  // todo:感覺這個函數沒什麼用，不如直接調用raft節點中的snapshot值？？？
    auto snapshot = persister->ReadSnapshot();
    if (!snapshot.empty()) {
        ReadSnapShotToInstall(snapshot); //将快照数据读取并应用到KvServer
    }
    std::thread t2(&KvServer::ReadRaftApplyCommandLoop, this);  //马上向其他节点宣告自己就是leader
    t2.join();  //由於ReadRaftApplyCommandLoop一直不會結束，达到一直卡在这的目的
}