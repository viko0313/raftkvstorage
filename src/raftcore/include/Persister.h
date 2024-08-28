#ifndef SKIP_LIST_ON_RAFT_PERSISTER_H
#define SKIP_LIST_ON_RAFT_PERSISTER_H

#include <fstream>
#include <mutex>

class Persister {
private:
    std::mutex m_mtx;
    //存储 Raft 状态的字符串变量
    std::string m_raftState
    std::string m_snapshot;
    //文件名
    const std::string m_raftStateFilleName;
    const std::string m_snapshotFileName;

    std::ostream m_raftStateOutStream;
    std::ostream m_snapshotOutStream;

     /**
     * 保存raftStateSize的大小
     * 避免每次都读取文件来获取具体的大小
     */
    long long m_raftStateSize;
public:
    void Save(std::string raftstate, std::string snapshot);
    void SaveRaftState(const std::string &data);
    std::string ReadRaftState();
    long long RaftStateSize();
    std::string ReadSnapshot();

    explicit Persister();
    ~Persister();

private:
    void ClearRaftState();
    void ClearSnapshot();
    void ClearAllRaftStateAndSnapshot();
}

#endif