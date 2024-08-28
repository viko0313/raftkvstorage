#ifndef RAFT_H
#define RAFT_H
 
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>

#include "boost/any.hpp" //其实应该放在上面用<>
#include "boost/serialization/serialization.hpp"

//rpc可以删除，实际生产用不到 ，方便网络分区的时候debug
constexpr int Disconnected = 0;
constexpr int AppNormal = 1;

//投票状态
constexpr int Killed = 0;
constexpr int Voted = 1;
constexpr int Expired = 2;
constexpr int Normal = 3;

class Raft : public raftRpcProctoc::raftRpc{
private:
    std::mutex m_mtx;
    std::vector<std::shared_ptr<RaftRpcUtil>> m_peers; //记录其他节点？
    std::shared_ptr<Persister> m_persister;

    int m_me;
    int m_currentTerm;
    int m_VoteFor;

    std::vector<raftRpcProtoc::LogEntry> m_log; //log数组，结构在proto定义了
    int m_commitIndex;
    int m_lastapplied;

    //这个变量保存了领导者认为跟随者需要的下一条日志条目的索引。在 Raft 中，领导者使用这个索引来决定发送哪些日志条目给跟随者。
    std::vector<int> m_nextIndex;
    std::vector<int> m_matchIndex; //两个状态从1开始，因为commitindex 和 lastapplied从0开始

    enum Status
    {
        Follower,
        Candidate,
        Leader
    };
    Status m_status;

    //存储日志的作用还是？
    std::shared_ptr<LockQueue<ApplyMsg>> applyChan; //client从这里取日志，client与raft通信的接口
    // ApplyMsgQueue chan ApplyMsg // raft内部使用的chan，applyChan是用于和服务层交互，最后好像没用上

    //选举超时
    std::chrono::_V2::system_clock::time_point m_lastResetElectionTime;
    //心跳超时，用于leader，规定时间没发心跳？
    std::chrono::_V2::system_clock::time_point m_lastResetHeartBeatTime;

    int m_lastSnapShotIncludeIndex;
    int m_lastSnapShotIncludeTerm;
    //协程
    std::unique_ptr<monsoon::IoManager> m_ioManager = nullptr;
public:
    //选举过程
    void doEleciton(); //重点，发起选举
    //这两个函数的关系？
    void RequestVote(const raftRpcProtoc::RequestVoteArgs *args,
                     raftRpcProtoc::RequestVoteReply *reply);
    bool sendRequestVote(int server, std::shared_ptr<raftRpcProtoc::RequestVoteArgs> arg,
                         std::shared_ptr<raftRpcProtoc::RequestVoteReply> reply, std::shared_ptr<int> votednum);
    
    void electionTimeOutTicker(); //监控选举超时

    //心跳和同步
    void doHeartBeat();
    void leaderHeartBeatTricker(); //leader超时器
    void AppendEntries1(const mprrpc::AppendEntriesArgs *args,
                       mprrpc::AppendEntriesReply *reply); //// 日志同步 + 心跳 rpc ，重点关注
    bool sendAppendEntries(int server, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args,
                         std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply, std::shared_ptr<int> appendNums);
    void leaderSendSnapshot(int server); //啥时候发snapshot
    void applierTicker(); // 定时向状态机写日志
    //下载快照？
    bool CondInstallSnapshot(int lastIncludeTerm, int lastIncludeIndex, std::string snapshot);
    //只有leader才需要发心跳
    std::vector<ApplyMsg> getApplyLogs(); //容器和get？
    //得搞懂ApplyMsg.h 干嘛的？
    void pushMsgToKvServer(ApplyMsg msg);

    int getNewCommandIndex(); //获取新命令索引
    void getPrevLogInfo(int server, int *prevIndex, int *prevTerm);
    void getState(int *term, bool *isLeader); //判断是否leader, 指针传递我猜方便函数修改？
    //调用protoc的service！！
    void InstallSnapshot(const raftRpcProtoc::InstallSnapshotRequest *args,
                         raftRpcProtoc::InstallSnapshotResponse *reply);
    void leaderUpdateCommitIndex(); //啥时候更新
    bool matchLog(int logindex, int logterm);
    bool UptoDate(int index, int term); //判断当前节点是否含有最新日志

    int getLastLogTerm();
    int getLastLogIndex();
    void getLastLogIndexAndTerm(int *index, int *term); // 返回最后日志的index和term

    void getLogTermFromLogindex(int index); //根据index获取term
    int getRaftStateSize(); //获取大小，避免每次读文件获取，不太懂？

    int getSlicesIndexFromLogIndex(int logindex); //逻辑下标转为物理下标，因为物理被snapshot影响

    void persist(); //做持久化操作
    void readPersist(std::string data);//持久化存储中读取节点状态
    std::string persistData();

    void Start(Op command, int *newlogindex, int *newlogterm, bool *isleader);
    void snapshot(int index, std::string snapshot); //是否更新快照？
public:
//给rpc调用的接口，那上面写的 raftRpcProtoc::是干啥的
/**
 * 
 * 每个函数都遵循protobuf RPC接口的约定，接受四个参数：
controller：用于控制RPC调用的生命周期和获取状态信息。
request：指向包含请求数据的protobuf消息的指针。
response：指向用于填充响应数据的protobuf消息的指针。
done：一个Closure对象的指针，用于在操作完成后执行回调。
*/
    void AppendEntries(google::protobuf::RpcController *controller, const ::raftRpcProctoc::AppendEntriesArgs *request,
                       ::raftRpcProctoc::AppendEntriesReply *response, ::google::protobuf::Closure *done) override;
    void InstallSnapshot(google::protobuf::RpcController *controller,
                         const ::raftRpcProctoc::InstallSnapshotRequest *request,
                         ::raftRpcProctoc::InstallSnapshotResponse *response, ::google::protobuf::Closure *done) override;
    void RequestVote(google::protobuf::RpcController *controller, const ::raftRpcProtoc::RequestVoteArgs *arg,
                     ::raftRpcProtoc::RequestVoteReply *response, ::google::protobuf::Closure *done) override;

public:
    void init(std::vector<std::shared_ptr<RaftRpcUtil>> peers, int me, std::shared_ptr<Persister> persister,
              std::shared_ptr<LockQueue<ApplyMsg>> applych);
private:
    class BoostPersistRaftNode {
        //声明原有允许外部boost::serialiazation 访问 这个类的公有私有成员
        friend class boost::serialization::access;
        //serialize 函数是一个模板函数，用于与 Boost 序列化库配合，将对象的状态保存到一个存档（archive）中，或者从存档中恢复对象的状态。这里的 Archive 是一个泛型参数，
        //可以是任何 Boost 序列化库支持的存档类型，比如 boost::archive::text_oarchive（文本输出存档）或 boost::archive::text_iarchive（文本输入存档）。
        //如果 ar 是一个输出存档（text_oarchive），这些操作将成员变量的值写入存档；如果 ar 是一个输入存档（text_iarchive）
        template<class Archive>
        void serialize(Archive &ar, const unsigned int version) {
            ar & m_currentTerm;
            ar & m_votedFor;
            ar & m_lastSnapshotIncludeIndex;
            ar & m_lastSnapshotIncludeTerm;
            ar & m_logs;
        }
        int m_currentTerm;
        int m_voteFor;
        int m_lastSnapshotIncludeIndex;
        int m_lastSnapshotIncludeTerm;
        std::vector<std::string> m_log;
        std::unordered_map<std::string, int> umap;
    };
};

#endif