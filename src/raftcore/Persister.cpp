#include "/include/Persister.h"
#include "util.h"
#include <iostream>

// todo:会涉及反复打开文件的操作，没有考虑如果文件出现问题会怎么办？？
void Persister::Save(std::string raftstate, std::string snapshot) {
    std::lock_guard<std::mutex> lg(m_mtx);
    ClearAllRaftStateAndSnapshot();
    m_raftStateOutStream << raftstate;
    m_snapshotOutStream << snapshot;

}
void Persister::SaveRaftState(const std::string &data) {
    std::lock_guard<std::mutex> lg(m_mtx);
    ClearRaftState();
    m_raftStateOutStream << data;
    m_raftStateSize += data.size();
}

std::string Persister::ReadRaftState() {
    std::lock_guard<std::mutex> lg(m_mtx);
    std::fstream ifs(m_snapshotFileName, std::ios_base::in);
    if (!ifs.good()) {
        return "";
    }
    std::string snapshot;
    ifs >> snapshot;
    ifs.close();
    return snapshot;
}

long long Persister::RaftStateSize() {
    std::lock_guard<std::mutex> lg(m_mtx);
    return m_raftStateSize;
}

std::string Persister::ReadSnapshot() {
    // 使用std::lock_guard自动锁定互斥量，保证线程安全
    std::lock_guard<std::mutex> lg(m_mtx);

    // 检查m_snapshotOutStream是否已经打开，如果是，则关闭它
    if (m_snapshotOutStream.is_open()) {
        m_snapshotOutStream.close();
    }

    // 使用宏DEFER来确保在函数返回前执行某段代码
    // 这里用于重新打开快照输出流，无论函数正常返回还是异常退出
    DEFER {
        m_snapshotOutStream.open(m_snapshotFileName);
    };

    // 创建一个输入文件流ifs，用于读取快照文件
    std::fstream ifs(m_snapshotFileName, std::ios_base::in);

    // 检查ifs是否成功打开并且处于良好状态
    if (!ifs.good()) {
        // 如果ifs不处于良好状态，返回空字符串
        return "";
    }

    // 读取快照数据到字符串snapshot中
    // 这里假设快照文件只包含单行文本
    std::string snapshot;
    ifs >> snapshot;

    // 关闭输入文件流
    ifs.close();

    // 返回读取到的快照数据
    return snapshot;
}

Persister::Persister(const int me) 
    : m_snapshotFileName("snapshotpersist" + std::to_string(me) + ".txt"),
      m_raftStateFileName("raftstatePersist" + std::to_string(me) + ".txt"),
      m_raftStateSize(0) {
    bool fileOpenFlag = true;
    //创建一个 std::fstream 对象 file，尝试以输出模式（std::ios::out）
    //和截断模式（std::ios::trunc）打开 m_raftStateFileName 指定的文件。
    std::fstream file(raftStateFilemname, std::ios::out | std::ios::trunc);
    if (file.is_open()) {
        file.close();
    } else {
        fileOpenFlag = false;
    }
    file = std::fstream(std::ios::out | std::std::ios::trunc);
    if (file.is_open()) {
        file.close();
    } else {
        fileOpenFlag = false;
    }
    if (!fileOpenFlag) {
        DPrintf("[func-Persister::Persister] file open error");
    }
    /**
     * 绑定流,绑定文件
     */
    m_raftStateOutStream.open(m_raftStateFileName);
    m_snapshotOutStream.open(m_snapshotFileName);
}
Persister::~Persister() {
    //及时关闭流
    if (m_raftStateOutStream.is_open()) {
        m_raftStateOutStream.close();
    }
    if (m_snapshotOutStream.is_open()) {
        m_snapshotOutStream.close();
    }
}

void Persister::ClearRaftState() {
    //重置size= 0=》关闭流=》重新打开并清空
    m_raftStateSize = 0;
    if (m_raftStateOutStream.is_open()) {
        m_raftStateOutStream.close();
    }
    // 重新打开文件流并清空文件内容
    m_raftStateOutStream.open(m_raftStateFileName, std::ios::out | std::ios::trunc);
    
}
void Persister::ClearSnapshot() {
    if (m_snapshotOutStream.is_open()) {
        m_snapshotOutStream.close();
    }
    m_snapshotOutStream.open(m_snapshotFileName, std::ios::out | std::ios::trunc);
}
void Persister::ClearAllRaftStateAndSnapshot() {
    clearRaftState();
    clearSnapshot();
}