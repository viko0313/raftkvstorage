#ifndef APPLYMSG_H
#define APPLYMSG_H

//Command和CommandIndex主要用于指示需要应用到状态机的具体命令及其在日志中的位置，
//而SnapshotTerm、SnapshotIndex及SnapshotValid则是为了描述一个快照的状态，包括它属于哪个任期、快照覆盖到的日志位置等，用于状态机快速恢复到某一特定状态。
class ApplyMsg {
private:
    //命令应用和快照恢复
    bool CommandValid;
    bool SnapshotValid;
    std::string Command;
    int CommandIndex;
    int SnapshotTerm;
    int SnapshotIndex;
public:
    ApplyMsg()
      : CommandValid(false),
        Command(),
        CommandIndex(-1),
        SnapshotValid(false),
        SnapshotTerm(-1),
        SnapshotIndex(-1){

        };
}
//std::shared_ptr<LockQueue<ApplyMsg> > applyChan; kvServer和raft节点都有，之间的通信管道
#endif