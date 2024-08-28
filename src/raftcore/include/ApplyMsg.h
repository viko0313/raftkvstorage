#ifndef APPLYMSG_H
#define APPLYMSG_H

//这个和快照信息有什么不同呢
class ApplyMsg {
private:
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

#endif