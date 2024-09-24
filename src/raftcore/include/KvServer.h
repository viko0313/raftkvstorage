#ifndef SKIP_LIST_ON_RAFT_KVSERVER_H
#define SKIP_LIST_ON_RAFT_KVSERVER_H

#include <boost/any.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/foreach.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include "kvServerRPC.pb.h"
#include "raft.h"
#include "skipList.h"

class KvServer : raftKVRpcProctoc::kvServerRpc {
private:
    std::mutex m_mtx;  // 用于同步访问 KvServer 实例的互斥锁
    int m_me;  // 当前服务器的唯一标识符
    std::shared_ptr<Raft> m_raftNode;  // 指向当前服务器的 Raft 实例的智能指针
    std::shared_ptr<LockQueue<ApplyMsg>> applyChan;  // 用于 KvServer 和 Raft 节点之间通信的队列
    int m_maxRaftState;  // 当 Raft 日志大小超过此值时，会触发快照

    // 序列化后的键值对数据，用于快照
    std::string m_serializedKVData;
    // 跳表，用于存储键值对数据
    SkipList<std::string, std::string> m_skipList;
    // 哈希表，用于存储键值对数据，提供快速访问
    std::unordered_map<std::string, std::string> m_kvDB;
    // index(raft) -> chan  //？？？字段含义   waitApplyCh是一个map，键是int，值是Op类型的管道
    std::unordered_map<int, LockQueue<Op> *> waitApplyCh;
    
// clientid -> requestID  //一个kV服务器可能连接多个client记录每个客户端最后请求的ID，用于幂等性检查
    std::unordered_map<std::string, int> m_lastRequestId;  
    // last SnapShot point , raftIndex
    int m_lastSnapShotRaftLogIndex;
public:
    // 删除默认构造函数，确保必须提供必要的参数来构造 KvServer 实例
    KvServer() = delete;

    // 构造函数，初始化 KvServer 实例
    KvServer(int me, int maxraftstate, std::string nodeInforFileName, short port);

    // 启动 KvServer 服务
    void StartKVServer();

    // 打印 KvServer 内部的键值对数据库
    void DprintfKVDB();

    // 在 KvServer 的键值对数据库上执行 Append 操作
    void ExecuteAppendOpOnKVDB(Op op);

    // 在 KvServer 的键值对数据库上执行 Get 操作
    void ExecuteGetOpOnKVDB(Op op, std::string *value, bool *exist);

    // 在 KvServer 的键值对数据库上执行 Put 操作
    void ExecutePutOpOnKVDB(Op op);

    //将 GetArgs 改为rpc调用的，因为是远程客户端，即服务器宕机对客户端来说是无感的,这个在raftKVRpcProctoc
    void Get(const raftKVRpcProctoc::GetArgs *args,
            raftKVRpcProctoc::GetReply
                *reply);  
    /**
     * 從raft節點中獲取消息  （不要誤以爲是執行【GET】命令）
     * @param message
     */
    void GetCommandFromRaft(ApplyMsg message);
    // 检查请求是否重复
    bool ifRequestDuplicate(std::string ClientId, int RequestId);

    // clerk 使用RPC远程调用, 处理 Put 或 Append 请求，参数为请求和响应对象
    void PutAppend(const raftKVRpcProctoc::PutAppendArgs *args, raftKVRpcProctoc::PutAppendReply *reply);

    //一直等待raft传来的applyCh ,循环读取
    void ReadRaftApplyCommandLoop();
    // 读取并安装快照
    void ReadSnapShotToInstall(std::string snapshot);
    // 将操作发送到等待队列
    bool SendMessageToWaitChan(const Op &op, int raftIndex);

    // 检查是否需要制作快照，需要的话就向raft之下制作快照
    void IfNeedToSendSnapShotCommand(int raftIndex, int proportion);

    // Handler the SnapShot from kv.rf.applyCh
    // 处理从 Raft 节点接收到的快照
    void GetSnapShotFromRaft(ApplyMsg message);

    std::string MakeSnapShot();
public:
    void PutAppend(google::protobuf::RpcController *controller, const ::raftKVRpcProctoc::PutAppendArgs *request,
                   ::raftKVRpcProctoc::PutAppendReply *response, ::google::protobuf::Closure *done) override;

    void Get(google::protobuf::RpcController *controller, const ::raftKVRpcProctoc::GetArgs *request,
            ::raftKVRpcProctoc::GetReply *response, ::google::protobuf::Closure *done) override;
private:
    friend class boost::serialization::access;

    // When the class Archive corresponds to an output archive, the
    // & operator is defined similar to <<.  Likewise, when the class Archive
    // is a type of input archive the & operator is defined similar to >>.
    // 序列化和反序列化函数，用于将 KvServer 的状态保存到文件或从文件中恢复
    template <class Archive>
    void serialize(Archive &ar, const unsigned int version)  //这里面写需要序列话和反序列化的字段
    {
        ar &m_serializedKVData;

        // ar & m_kvDB;
        ar &m_lastRequestId;
    }

    // 从跳表中获取快照数据
    std::string getSnapshotData() {
        m_serializedKVData = m_skipList.dump_file();
        std::stringstream ss;
        boost::archive::text_oarchive oa(ss);
        oa << *this;
        m_serializedKVData.clear();
        return ss.str();
    }
    
    // 从字符串中解析 KvServer 的状态
    void parseFromString(const std::string &str) {
        std::stringstream ss(str);
        boost::archive::text_iarchive ia(ss);
        //使用输入运算符 >> 将档案 ia 中的数据反序列化到当前的 KvServer 实例（*this 表示）。
        //这个过程会调用 KvServer 类中定义的 serialize 方法，将序列化的数据成员恢复到 KvServer 实例中。
        ia >> *this; 
        m_skipList.load_file(m_serializedKVData);
        m_serializedKVData.clear();
    }

  /////////////////serialiazation end ///////////////////////////////
};

#endif  // SKIP_LIST_ON_RAFT_KVSERVER_H
