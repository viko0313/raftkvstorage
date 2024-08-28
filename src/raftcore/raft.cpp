#include "./include/raft.h"
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <memory> //.h好像有了
#include "config.h"
#include "util.h"

//监控选举超时，三个定时器都挺类似的
void Raft::electionTimeOutTicker() {
    while (true) {
        while (m_status == leader) {
            //HeartBeatTimeout => config.h
            usleep(HeartBeatTimeout);
        }
        //定义持续时间，纳秒级别， 有撒子用？？
        //第一个模板参数指定存储时间值的数据类型，第二个模板参数 std::ratio 用于指定时间单位的比例。例如，std::ratio<1, 1000000000> 表示 1 纳秒。
        std::chrono::duration<signed long int, std::ratio<1, 1000000000>> suitableSleepTime{};
        std::chrono::system_clock::time_point wakeTime{};
        {   
            //得到合适睡眠时间
            std::lock_guard<std::mutex> lg(m_mtx);
            wakeTime = now();
            //getrandom=> util.cpp
            suitableSleepTime = getRandomizeElectionTimeout() + m_lastResetElectionTime - wakeTime;
        }

        //只关注睡眠时间大于1毫秒，睡眠时间太短可能对系统性能或其他逻辑没有实际意义
        //表示一个以毫秒为单位、存储值为 double 类型的时间间隔
        if (std::chrono::duration<double, std::milli>(suitableSleepTime).count() > 1) {
            auto start = now();
            usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count);
            auto end = now();
            //计算duration，计算实际睡眠时间
            std::chrono::duration<double, std::milli> duration = end - start;
            // 使用ANSI控制序列将输出颜色修改为紫色
            // \033[1;35m 设置文本颜色为紫色，\033[0m 用于重置文本颜色
            std::cout << "\033[1;35m electionTimeOutTicker();函数设置睡眠时间为: "
                        << std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count() << " 毫秒\033[0m"
                        << std::endl;
            std::cout << "\033[1;35m electionTimeOutTicker();函数实际睡眠时间为: " << duration.count() << " 毫秒\033[0m"
                        << std::endl;
        }
        if (std::chrono::duration<double, std::milli>(m_lastResetElecitonTime - wakeTime).count >0) {
            //说明睡眠的这段时间有领导者重置定时器，那么就没有超时，再次睡眠
            continue;
        }
        doEleciton();
    }
    
}
void Raft::doEleciton() {
    std::lock_guard<std::mutex> g(m_mtx);

    if(m_status == Leader) {
        //fmt.Printf("[   ticker-func-rf(%v)  ] is a leader, wait the lock\n", rf.me);
    }

    if (m_status != Leader) {
        DPrintf("[  ticker-func-rf(%d)  ] 选举定时器到期且不是Leader，开始选举\n", m_me);
        m_status = Candidate;
        m_currentTerm += 1;
        m_votedFor = m_me;
        persist();
        std::shared_ptr<int> votedNum = std::make_shared<int>(1);
        m_lastResetElectionTime = now();
        for (int i = 0; i < m_peers.size(); i++) {
            if (i == m_me) {
                continue;
            }
            int lastLogIndex = -1, lastLogTerm = -1;
            getLastLogIndexAndTerm(&lastLogIndex, &lastLogTerm);
            //这里也用auto也生代码
            std::shared_ptr<raftRpcProtoc::RequestVoteArgs> requestVoteArgs = std::make_shared<raftRpcProtoc::RequestVoteArgs>();
            requestVoteArgs->set_term(m_currentTerm);
            requestVoteArgs->set_candidateid(m_me);
            requestVoteArgs->set_lastlogindex(lastLogIndex);
            requestVoteArgs->set_lastlogterm(lastLogTerm);
            auto requestVoteReply = std::make_share<raftRpcProtoc::RequestVoteReply>();

            //开辟新线程执行send request，避免拿到锁？
            std::thread t(&sendRequestVote, this, requestVoteArgs, requestVoteReply, votedNum);
            t.detach();
        }
    }
}
    //这两个函数的关系？, 这个是对端节点！！填充回复结构信息的
void Raft::RequestVote(const raftRpcProtoc::RequestVoteArgs *args,
                     raftRpcProtoc::RequestVoteReply *reply) {
    std::lock_guard<std::mutex> lg(m_mtx);

    DEFER {
        ////应该先持久化，再撤销lock ,啥意思
        persist();
    };
    if (args->term() < m_currentTerm) {
        reply->set_term(m_currentTerm);
        reply->set_votegranted(false);
        reply->set_votestate(Expire);
        return;
    } else if (arg->term() > m_currentTerm) {
        DPrintf("[	    func-RequestVote-rf(%v)		] : 变成follower且更新term
        因为candidate{%v}的term{%v}> rf{%v}.term{%v}\n ", rf.me, args.CandidateId, args.Term, rf.me,
        rf.currentTerm)
        m_currentTerm = args->term();
        m_status = Follower;
        m_voted = -1;
    }

    //不等的话就输出信息
    myAssert(args->term() == m_currentTerm, format("[func--rf{%d}] 前面校验过args.Term==rf.currentTerm，这里却不等", m_me));
    //比完term比日志
    int lastLogTerm = getLastLogTerm();
    //uptodate这个函数就是把发送者的两个index传入比较，如果发送者日志新，返回true
    //这里的意思就是发现发送者日志竟然旧！ 当然不可能给他投票
    if (!UpToDate(args->lastlogterm(), args->lastlogindex)) {
        if (args->lastlogterm() < lastLogTerm) {
            //调试信息，term都比我小，拿什么和我比
        } else {
            //调试信息，term虽然比我大，但是index又比我小
        }
        //老样子，reply三部
        reply->set_term(m_currentTerm);
        reply->set_votestate(voted); // 不明白为啥状态是这个？
        //首先不可能设为expire因为上面比较的term是相等的能会导致候选者错误地认为需要重新发起投票请求。
        reply->set_votegranted(false);
        return;
    }
    //发送者日志新，再看接收者情况
    //接收者投过票，并且投的不是这次发送者，丸辣
    if(m_voteFor != -1 && m_votedFor != candidateid()) {
        reply->set_term(m_currentTerm);
        reply->set_votestate(voted);
        reply->set_votegranted(false);
        return;
    } else { //否则的话就投啦！
        m_voteFor = args->candidate();
        m_lastResetElectionTime = now();
        //减少请求数，否则万一发送者还没当选，我这里超时发起选举了多不好。
        reply->set_term(m_currentTerm);
        reply->set_votestate(Normal); //normal的意思是啥
        reply->set_votegranted(true);
        return;
    }
}

//这个函数是follower节点收到sendvoterequest用来比较日志谁新的
bool Raft::UpToDate(int index, int term) {
    int lastIndex = -1, lastTerm = -1;
    getLastLogIndexAndTerm(&lastIndex, &lastTerm);
    return term > lastTerm || (term == lastTerm && index >= lastIndex);
}

int Raft::getLastLogTerm() {
    int term = -1, _ = -1;
    getLastLogIndexAndTerm(&_, &term);
    return term;
}

int Raft::getLastLogIndex() {
    int index = -1, _ = -1;
    getLastLogIndexAndTerm(&index, &_);
    return index;
}

void Raft::getLastLogIndexAndTerm(int *index, int *term) {
    if (m_logs.emtpy()) {
        //说明没有日志只有snapshot，那就用lastsnapshot
        index = m_lastSnapshotIncludeIndex;
        term = m_lastSnapshotIncludeTerm;
    }
    index = m_logs[m_logs.size() - 1].logindex();
    term = m_logs[m_logs.size() - 1].logterm();
}


bool Raft::sendRequestVote(int server, std::shared_ptr<raftRpcProtoc::RequestVoteArgs> arg,
                         std::shared_ptr<raftRpcProtoc::RequestVoteReply> reply, std::shared_ptr<int> votednum) {
    auto start = now();
    DPrintf("[func-sendRequestVote rf{%d}] 向server{%d} 发送 RequestVote 开始", m_me, m_currentTerm, getLastLogIndex());
    //ok 变量的值反映了 RequestVote RPC 调用是否成功。如果 ok 为 true，表示投票请求成功发送并得到了响应
    //调用的是rpcutil的RequestVote
    bool ok = m_peers[server]->RequestVote(arg.get(), reply.get());
    DPrintf("[func-sendRequestVote rf{%d}] 向server{%d} 发送 RequestVote 完毕，耗时：{%d}ms", m_me, m_currentTerm, getLastLogIndex(), now() - start);
    if (!ok) {
        return ok; //不加这个服务器会出问题？
    }

    std::lock_guard<std::mutex> lg(m_mtx);
    //send发出去是能收到回复的参数有reply
    if (reply->term() > m_currentTerm) { //获取protoc变量就变量名加括号
        m_status = Follower;
        m_currentTerm = reply->term();
        m_votedFor = -1; //新人没投票
        persist();
        return true;
    } else if (reply->term() < m_currentTerm){
        return true; //回复的term不应该小
    }
    //格式化输出？
    myAssert(reply->term() == m_currentTerm, format("assert {reply.Term==rf.currentTerm} fail")); 

    if (!reply->votegranted()) { //投票没有被接受？
        return true;
    }   //在proto有记录，reply有三个元素，term，granted和votestate

    //接受了
    *votednum += 1; //加票要检查是否符合leader  //单数加一？
    if (*votednum > m_peers.size / 2 + 1) {
        *votednum = 0;
        if (m_status == Leader) {
            myAssert(false,
               format("[func-sendRequestVote-rf{%d}]  term:{%d} 同一个term当两次领导，error", m_me, m_currentTerm));
        }
        m_status = Leader;
        DPrintf("[func-sendRequestVote rf{%d}] elect success, current term:{%d}, lastLogIndex:{%d}\n", m_me, m_currentTerm, 
                 getlastLogIndex());
        int lastlogindex = getLastLogIndex();
        /**
         * 心跳机制的一个关键作用是同步领导者的日志到跟随者。初始化 m_nextIndex 和 m_matchIndex 后，
         * 领导者知道从哪里开始同步日志，这是发送心跳前的必要步骤。
        */
        for (int i = 0; i < m_nextIndex.size(); i++) {
            m_nextIndex[i] = lastlogindex + 1;
            m_matchIndex[i] = 0;
        }
        //心跳，操作分离（异步），防止延迟影响响应
        std::thread t(raft::doHeartBeat, this);
        t.detach();
        //leader初始化完了状态，peisist一下
        persist();
    }
}

//日志同步/心跳过程 installsnapshot有调用
void Raft::pushMsgToKvServer(ApplyMsg msg) { applyChan->Push(msg); }

void Raft::leaderHearBeatTicker() {
  while (true) {
    //不是leader的话就没有必要进行后续操作，况且还要拿锁，很影响性能，目前是睡眠，后面再优化优化
    while (m_status != Leader) {
      usleep(1000 * HeartBeatTimeout);
      // std::this_thread::sleep_for(std::chrono::milliseconds(HeartBeatTimeout));
    }
    static std::atomic<int32_t> atomicCount = 0;

    std::chrono::duration<signed long int, std::ratio<1, 1000000000>> suitableSleepTime{};
    std::chrono::system_clock::time_point wakeTime{};
    {
      std::lock_guard<std::mutex> lock(m_mtx);
      wakeTime = now();
      suitableSleepTime = std::chrono::milliseconds(HeartBeatTimeout) + m_lastResetHearBeatTime - wakeTime;
    }

    if (std::chrono::duration<double, std::milli>(suitableSleepTime).count() > 1) {
      std::cout << atomicCount << "\033[1;35m leaderHearBeatTicker();函数设置睡眠时间为: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count() << " 毫秒\033[0m"
                << std::endl;
      // 获取当前时间点
      auto start = std::chrono::steady_clock::now();

      usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count());
      // std::this_thread::sleep_for(suitableSleepTime);

      // 获取函数运行结束后的时间点
      auto end = std::chrono::steady_clock::now();

      // 计算时间差并输出结果（单位为毫秒）
      std::chrono::duration<double, std::milli> duration = end - start;

      // 使用ANSI控制序列将输出颜色修改为紫色
      std::cout << atomicCount << "\033[1;35m leaderHearBeatTicker();函数实际睡眠时间为: " << duration.count()
                << " 毫秒\033[0m" << std::endl;
      ++atomicCount;
    }

    if (std::chrono::duration<double, std::milli>(m_lastResetHearBeatTime - wakeTime).count() > 0) {
      //睡眠的这段时间有重置定时器，没有超时，再次睡眠
      continue;
    }
    // DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了\n", m_me);
    doHeartBeat();
  }
}

void Raft::doHeartBeat() {
    std::lock_guard<std::mutex> lg(m_mtx);
    if (m_status == leader) {
        DPrintf("[func-Raft::doHeartBeat()-leader: {%d}]Leader的心跳定时器触发了且拿到mutex，开始发送AE\n", m_me);
        auto appendNums = std::make_shared<int>(1); // 正确返回的节点的数量

        for (int i = 0;i < m_peers.size(); i++) {
            if (i == m_me) {
                continue;
            }
            DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了 index:{%d}\n", m_me, i);
            //这个变量保存了领导者认为跟随者需要的下一条日志条目的索引。在 Raft 中，领导者使用这个索引来决定发送哪些日志条目给跟随者。
            myAssert(m_nextIndex[i] >= 1, format("rf.nextIndex[%d] = {%d}", i, m_nextIndex[i]));
            //发送心跳需要带什么东西呢
            if (m_nextIndex[i] <= m_lastSnapshotIncludeIndex) {
                //领导者将不会发送单独的日志条目，而是选择发送整个快照给跟随者。这是因为快照包含了所有必要的日志信息
                std::thread t(&Raft::leaderSendSnapshot, this, i);
                //快照是 Raft 中用来减少日志大小和加速跟随者状态同步的一种机制。
                t.detach();
                continue;
            }

            int preLogIndex = -1, preLogTerm = -1;
            getPreLogInfo(i, &preLogIndex, &preLogTerm);
            appendEntriesArgs->set_term(m_currentTerm);
            appendEntriesArgs->set_leaderid(m_me);
            appendEntriesArgs->set_prelogindex(preLogIndex);
            appendEntriesArgs->set_logterm(preLogterm);
            //告诉其他节点我同步到哪了，你也同步一下
            appendEntriesArgs->set_leadercommit(m_commitIndex);
            //比较prelogindex和lastsnapshotindex然后决定发送整个日志还是某个下标之后
            //有一种直接发送快照的吧？这个函数能调用sendlog或者sendsnapshot

            ////这意味着跟随者已经包含了从快照恢复的日志，领导者只需要发送从 preLogIndex 的下一条开始的日志。
            if (preLogIndex != m_lastSnapshotIncludeIndex) {
                for (int i = getSlicesIndexFromLogIndex(preLogIndex) + 1; j < m_logs.size(); ++i) {
                    //AppendEntries RPC请求中添加一个新的日志条目，并获取这个新日志条目的指针，以便可以对其进行进一步的操作
                    rafrRpcProtoc::LogEntry *sendEntryPtr = appendEntriesArgs->add_entries();
                    // 这样的赋值操作实际上是在利用 Protocol Buffers（protobuf）库来序列化和反序列化数据
                    *sendEntryPtr = m_logs[i];
                }
            } else {
                for (const & item : m_logs) {
                    rafrRpcProtoc::LogEntry *sendEntryPtr = appendEntriesArgs->add_entries();
                    *sendEntryPtr = item;
                }
            }
                        
            //更新完log后，获取index
            int lastlogindex = getLastLogIndex();
            myAssert(appendEntriesArgs->prevlogindex() + appendEntriesArgs->entries_size() == lastLogIndex,
               format("appendEntriesArgs.PrevLogIndex{%d}+len(appendEntriesArgs.Entries){%d} != lastLogIndex{%d}",
                      appendEntriesArgs->prevlogindex(), appendEntriesArgs->entries_size(), lastLogIndex));

            const std::shared_ptr<raftRpcProtoc::AppendEntriesReply> appendEntriesReply =
                std::make_shared<raftRpcProtoc::AppendEntriesReply>();
            //AppendEntriesReply对象的字段可能在RPC调用的不同阶段被设置,事实上再appendrntries1() (接收节点调用设置了
            // 例如，Success字段可能在RPC成功或失败后被设置，而UpdateNextIndex可能在跟随者确认日志条目后被更新。
            appendEntriesReply->set_appstate(Disconnected);
            std::thread t(&raft::sendAppendEntries, this, appendEntriesArgs, appendEntriesReply, appendNums);
            //啥时候调用安装快照呢？？
            t.detach();
        }
        m_lastRestHeartBeatTime = now();
    }
}

bool Raft::sendAppendEntries(int server, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args,
                         std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply, std::shared_ptr<int> appendNums) {
    //处理一下网络rpc异常情况然后再加锁处理
    //这个ok是网络是否正常通信的ok，而不是requestVote rpc是否投票的rpc
    // 如果网络不通的话肯定是没有返回的，不用一直重试
    // todo： paper中5.3节第一段末尾提到，如果append失败应该不断的retries ,直到这个log成功的被store
    DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc開始 ， args->entries_size():{%d}", m_me,
          server, args->entries_size());
    bool ok = m_peers[server]->AppendEntries(args.get(), reply.get());
    if (!ok) {
        Printf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc失敗", m_me, server);
        return ok;
    }
    Printf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc成功", m_me, server);
    if (reply->appstate() == Disconnected) {
        return;
    }

    std::lock_guard<std::mutex> lg(m_mtx);
    //先检查term
    if (reply->term() > m_currentTerm) {
        m_status = Follower;
        m_currentTerm = reply->term();
        m_votedFor = -1;
        //persist(); 为啥这里没有持久化呢
    } else if (reply->term() < m_currentTerm) {
        DPrintf("[func -sendAppendEntries  rf{%d}]  节点：{%d}的term{%d}<rf{%d}的term{%d}\n", m_me, server, reply->term(),
                 m_me, m_currentTerm);
        return ok;
    }
    myAssert(reply->term() == m_currentTerm, format("reply.Term{%d} != rf.currentTerm{%d}   ", reply->term(), m_currentTerm));

    if (!reply->success()) {
        //-100可能是一个特殊值，用来表示没有有效的nextIndex更新建议。
        if (reply->updatenextindex != -100) {
            DPrintf("[func -sendAppendEntries  rf{%d}]  返回的日志term相等，但是不匹配，回缩nextIndex[%d]：{%d}\n", m_me,
                     server, reply->updatenextindex());
            //AppendEntries请求失败，Leader节点不会更新matchIndex，因为Follower节点没有成功复制日志。
            //matchindex数组记录了Follower节点已经成功复制的日志条目的最大索引
            m_nextIndex[server] = reply->updatenextindex();
        }
    } else {
        *appendNums += 1; //意味着日志匹配？
        DPrintf("---------------------------tmp------------------------- 節點{%d}返回true,當前*appendNums{%d}", server,
                *appendNums);
        //因为leader已经把从preindex开始的日志发送entries_size大小了,确保leader知道follower节点复制到哪里了
        //得到的是当前请求中最后一个日志条目的索引加一
        m_matchIndex[server] = std::max(m_matchIndex[server], args->prevlogindex() + args->entries_size());
        m_nextIndex[server] = m_matchIndex[server] + 1;
        int lastlogindex = getLastLogIndex();

        //nextindex超出last +1肯定不对
        myAssert(m_nextIndex[server] <= lastLogIndex + 1,
             format("error msg:rf.nextIndex[%d] > lastLogIndex+1, len(rf.logs) = %d   lastLogIndex{%d} = %d", server,
                    m_logs.size(), server, lastLogIndex));
        
        //，同步完之后先修改match和next然后开始可以commit了，检查commit条件
        if (*appendNums >= m_peers.size() /2 + 1) {
            *appendNums = 0;
            //m_commitIndex =  达咩直接更新commit，只有满足leader当前提交日志中，term为当前（最新才更新commitindex
            if (args->logentries_size() > 0) {
                DPrintf("args->entries(args->entries_size()-1).logterm(){%d}   m_currentTerm{%d}",
                        args->entries(args->entries_size() - 1).logterm(), m_currentTerm);
            }
            if (args->logentries_size > 0 && args->entries(args->entries_size() - 1).logterm() == m_currentTerm) {
                m_commitIndex = std::max(m_commitIndex, args->prevlogindex() + args->entries_size());
                DPrintf(
                "---------------------------tmp------------------------- 當前term有log成功提交，更新leader的m_commitIndex "
                "from{%d} to{%d}",
                m_commitIndex, args->prevlogindex() + args->entries_size());
            }
            myAssert(m_commitIndex <= lastLogIndex,
                     format("[func-sendAppendEntries,rf{%d}] lastLogIndex:%d  rf.commitIndex:%d\n", m_me, lastLogIndex,
                      m_commitIndex));
        }
        return ok;
    }
}

//啥时候发snapshot
void Raft::leaderSendSnapshot(int server) {
    m_mtx.lock();
    raftRpcProto::InstallSnapshotRequest args;
    // 五个参数LeaderId，term,lastsnapshotIncludeindex,term,data;
    args->set_leaderid(m_me);
    args->set_term(m_currentTerm);
    args->set_lastsnapshotIncludeindex(m_lastSnapshotIncludeIndex);
    args->set_lastsnapshotincludeterm(m_lastSnapshotIncludeTerm);
    args->set_data(m_persister->readSnapshot()); //读取snapshot是这样的
    raftRpcProto::InstallSnapshotReply reply;
    m_mtx.unlock();
    bool ok = m_peers[server]->InstallSnapshot(&args, &reply); //所以是不是在这里调用得到reply
    m_mtx.lock();
    DEFER { m_mtx.unlock(); };
    //下面检查状态还是加锁，确保不被修改
    if (!ok) {
        return;
    }
    //中间释放过锁m_term可能会变，释放前其实是相等的
    if (m_status != Leader || m_currentTerm != args.term()) {
        return;
    }
    
    //涉及到和follower通信的都要检查term
    if (reply.term() > m_currentTerm) {
        m_status = Follower;
        m_currentTerm = reply.term();
        m_votedFor = -1;
        persist();
        m_lastRestElectionTime = now(); //每次变为follower，要给他设定超时时间对吧，选举时间是ramdom + m_last - now()
        return;
    }
    //leader更新一下match和next  这两个是维护其他节点的 ,自己的叫做commitIndex
    m_matchIndex[server] = args->lastsnapshotincludeindex(); // 因为rpc会延迟，不可以m_lastSnapShotIncludeIndex()，
    m_nextIndex[server] = m_matchIndex[server] + 1; //nextindex的作用要好好理解，在appendentries有很多if
    return;
}

//没有谁调用啊，因为在sendappend那里直接优化了，对比arg->entries(entries.size()- 1) 和currentterm对比了
void Raft::leaderUpdateCommitIndex() {
  m_commitIndex = m_lastSnapshotIncludeIndex;
  // for index := rf.commitIndex+1;index < len(rf.log);index++ {
  // for index := rf.getLastIndex();index>=rf.commitIndex+1;index--{
  for (int index = getLastLogIndex(); index >= m_lastSnapshotIncludeIndex + 1; index--) {
    int sum = 0;
    for (int i = 0; i < m_peers.size(); i++) {
      if (i == m_me) {
        sum += 1;
        continue;
      }
      if (m_matchIndex[i] >= index) {
        sum += 1;
      }
    }

    //        !!!只有当前term有新提交的，才会更新commitIndex！！！！
    // log.Printf("lastSSP:%d, index: %d, commitIndex: %d, lastIndex: %d",rf.lastSSPointIndex, index, rf.commitIndex,
    // rf.getLastIndex())
    if (sum >= m_peers.size() / 2 + 1 && getLogTermFromLogIndex(index) == m_currentTerm) {
      m_commitIndex = index;
      break;
    }
  }
  //    DPrintf("[func-leaderUpdateCommitIndex()-rf{%v}] Leader %d(term%d) commitIndex
  //    %d",rf.me,rf.me,rf.currentTerm,rf.commitIndex)
}

void Raft::AppendEntries1(const mprrpc::AppendEntriesArgs *args,
                       mprrpc::AppendEntriesReply *reply) {
    std::lock_guard<std::mutex> lg(m_mtx);
    reply->set_appstate(AppNormal);

    if (args->term() < m_currentTerm) {
        reply->set_term(m_currentTerm);
        reply->set_success(false);
        reply->set_nextindex(-100);
        return; //从过期leader收到消息不要reset定时器
    }
    //Defer ec1([this]() -> void { this->persist(); }); 解耦了this？
    DEFER { persist(); };
    if (args->term() > m-currentTerm) {
        m_currentTerm = args->term();
        m_status = Follower;
        m_votedFor = -1;
        // 这里设置成-1有意义，如果突然宕机然后上线理论上是可以投票的
        // 这里可不返回，应该改成让改节点尝试接收日志,始终要进行同步操作！！
        // 如果是领导人和candidate突然转到Follower好像也不用其他操作
        // 如果本来就是Follower，那么其term变化，相当于“不言自明”的换了追随的对象，因为原来的leader的term更小，是不会再接收其消息了
    }
    myAssert(args->term() == m_currentTerm, format("assert {args->term() == m_currentTerm} fail"));
    m_status = Follower; 
    //和上面看似代码重复，实则不然，在网络分区的情况下可能会存在follow=》candidate，term+1之后会和leader相同
    //当收到leader的AE，就要变为follow
    //还是每次变为follow都要设置超时
    m_lastResetElectionTime = now();

    //args->prevlogindex()领导者希望追随者从其日志中的哪个索引位置开始复制新条目。这通常是追随者日志中最后一个条目的索引。
    if (args->prevlogindex() > getLastLogIndex()) {
        reply->set_term(m_currentTerm);
        reply->set_success(false);
        //告诉领导者追随者的日志是完整的，直到 getLastLogIndex()
        reply->set_updatenextindex(getLastLogIndex() + 1); //为啥呢
        DPrintf("[func-AppendEntries-rf{%v}] 拒绝了节点{%v}，因为日志太新,args.PrevLogIndex{%v} >
        lastLogIndex{%v}，返回值：{%v}\n", rf.me, args.LeaderId, args.PrevLogIndex, rf.getLastLogIndex(), reply)
        return;
    } else if (args->prevlogindex() < m_lastSnapShotIncludeIndex) {
        reply->set_term(m_currentTerm);
        reply->set_success(false);
        reply->set_updatenextindex(m_lastSnapShotIncludeIndex + 1); //因为是从后慢慢往前匹配的，这里不匹配说明后面的都不匹配，不能和上面getLastLogIndex() + 1
        DPrintf("[func-AppendEntries-rf{%v}] 拒绝了节点{%v}，因为log太老，返回值：{%v}\n", rf.me, args.LeaderId, reply)
        return; //源代码注释掉return了
    }
    //通过arg的index来得到follower节点的term与arg的term相比，true返回匹配，更新entries
    //注意：这里目前当args.PrevLogIndex == rf.lastSnapshotIncludeIndex与不等的时候要分开考虑，可以看看能不能优化这块
    if(matchLog(args->prevlogindex(), args->prevlogterm())) {
        for (int i = 0; i < args->entires_size(); i++) {
            auto log = args->entries(i);
            if (log.logindex() > getLastLogIndex()) { //这里默认term是一样的吗，可以加个或条件比较term>?
                //超过就直接添加日志
                m_logs.push_back(log);
            } else {
                //没超过就比较是否匹配，不匹配再更新，而不是直接截断
                // todo ： 这里可以改进为比较对应logIndex位置的term是否相等，term相等就代表匹配
                //  todo：这个地方放出来会出问题,按理说index相同，term相同，log也应该相同才对
                // rf.logs[entry.Index-firstIndex].Term ?= entry.Term
                //index不同而已，比较term
                if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm() == log.logterm() && 
                    m_logs[getSlicesIndexFromLogIndex(logindex())].command() != log.command()) {
                    //相同位置的log ，其logTerm相等，但是命令却不相同，不符合raft的前向匹配，异常了！
                    //这种情况不应该发生，所以不应该更新
                    myAssert(false, format("[func-AppendEntries-rf{%d}] 两节点logIndex{%d}和term{%d}相同，但是其command{%d:%d}   "
                            " {%d:%d}却不同！！\n",
                            m_me, log.logindex(), log.logterm(), m_me,
                            m_logs[getSlicesIndexFromLogIndex(log.logindex())].command(), args->leaderid(),
                            log.command()));
                }
                if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm().logterm() != log.logterm()) {
                    m_logs[getSlicesIndexFromLogIndex(log.logindex())] = log; //不匹配就更新
                }
            }
        }
        //更新完后getLastLogIndex()应该是等于的
        myAssert(
            getLastLogIndex() >= args->prevlogindex() + args->entries_size(),
            format("[func-AppendEntries1-rf{%d}]rf.getLastLogIndex(){%d} != args.PrevLogIndex{%d}+len(args.Entries){%d}",
                    m_me, getLastLogIndex(), args->prevlogindex(), args->entries_size()));
        if (args->leadercommit() > m_commitIndex) {
            m_commitIndex = std::min(args->leadercommit(), getLastLogIndex());\
            // 这个地方不能无脑跟上getLastLogIndex()，因为可能存在args->leadercommit()落后于 getLastLogIndex()的情况
        }
        // 领导会一次发送完所有的日志
        myAssert(getLastLogIndex() >= m_commitIndex,
                 format("[func-AppendEntries1-rf{%d}]  rf.getLastLogIndex{%d} < rf.commitIndex{%d}", m_me,
                         getLastLogIndex(), m_commitIndex));
        reply->set_success(true);
        reply->set_term(m_currentTerm);
        return;
    } else { //这个else是不match的时候
        //就是index相同，term不同
        // PrevLogIndex 长度合适，但是不匹配，因此往前寻找 矛盾的term的第一个元素
        // 为什么该term的日志都是矛盾的呢？也不一定都是矛盾的，只是这么优化减少rpc而已
        // ？什么时候term会矛盾呢？很多情况，比如leader接收了日志之后马上就崩溃等等
        reply->set_updatenextindex(args->prevlogindex());
        for (int index = args->prevlogindex(); index >= m_lastSnapShotIncludeIndex; --index) {
            if (getLogTermFromLogIndex(index) != getLogTermFromLogIndex(args->prevlogindex())) {
                reply->set_updatenextindex(index + 1);
                break;
            }
        }
        reply->set_success(false);
        reply->set_term(m_currentTerm);
        return;
    }
}

int Raft::getLogTermFromLogIndex(int logIndex) {
    //还是先判定Logindex的合法性
    DPrintf(logIndex >= m_lastSnapshotIncludeIndex,
            format("[func-getLogTermFromLogIndex-rf{%d}]  index{%d} < rf.lastSnapshotIncludeIndex{%d}", m_me,
                   logIndex, m_lastSnapshotIncludeIndex));
    int lastindex = getLastLogIndex();
    DPrintf(logIndex <= lastindex, format("[func-getLogTermFromLogIndex-rf{%d} logindex{%d} > lastlogindex{%d}]", m_me, logIndex, lastindex));
    //能执行到这说明在m_lastSnapshotIncludeIndex和lastindex两者之间，考虑到等于snapshot是不在日志里的
    if (logIndex == m_lastSnapshotIncludeIndex) {
        return m_lastSnapshotIncludeterm;
    } else {
        return m_logs[getSlicesIndexFromLogIndex(logIndex)].logterm();
    }
}

//获取持久化数据的大小，这是persist的成员变量
int getRaftStateSize(); {
    return m_persister->RaftStateSize(); //点进persister可以看到是shared指针
}

//根据日志的下标（日志包括index和term）找到物理下标，计算法则就是要-snapshot-1，因为从0开始算
int getSlicesIndexFromLogIndex(int logindex) {
    //还是判定合理性，但是不能==m_lastsnapshotindex，等于意味着日志为空
    myAssert(logIndex > m_lastSnapshotIncludeIndex,
           format("[func-getSlicesIndexFromLogIndex-rf{%d}]  index{%d} <= rf.lastSnapshotIncludeIndex{%d}", m_me,
                  logIndex, m_lastSnapshotIncludeIndex));
    int lastLogIndex = getLastLogIndex();
    myAssert(logIndex <= lastLogIndex, format("[func-getSlicesIndexFromLogIndex-rf{%d}]  logIndex{%d} > lastLogIndex{%d}",
                                              m_me, logIndex, lastLogIndex));
    int sliceIndex = logindex - m_lastSnapshotIncludeIndex - 1;
    return sliceIndex;
}

//比较term，command不比较
bool Raft::matchLog(int logindex, int logterm) {
    //传过来的index还是得判断合法性，然后再在根据index得到自己日志的term进行比较
    myAssert(logindex >= m_lastSnapShotIncludeIndex && logindex <= getLastLogIndex(),
             format("不满足：logIndex{%d}>=rf.lastSnapshotIncludeIndex{%d}&&logIndex{%d}<=rf.getLastLogIndex{%d}",
                    logIndex, m_lastSnapshotIncludeIndex, logIndex, getLastLogIndex()));
    return logterm == getLogTermFromLogIndex();
}
//下面是持久化
void Raft::persist() {
    auto data = persisData();
    m_persister->SaveRaftState(data);
}
std::string Raft::persistData() {
    BoostPersistRaftNode boostPersistRaftNode;
    boostPersistRaftNode.m_currentTerm = m_currentTerm;
    boostPersistRaftNode.m_votedFor = m_votedFor;
    boostPersistRaftNode.m_lastSnapshotIncludeIndex = m_lastSnapshotIncludeIndex;
    boostPersistRaftNode.m_lastSnapshotIncludeterm = m_lastSnapshotIncludeterm;
    for (auto & it : m_logs) {
        boostPersistRaftNode.m_log.push_back(it.SerializeAsString());
    }
}

//从持久化恢复raft节点状态，在 Raft 节点初始化时，需要从持久化存储中恢复状态。
void Raft::readPersist(std::string data) {
    if (data.empty()) {
        return;
    }
    std::stringstream iss(data);
    boost::archive::text_iarchive ia(iss); //输入存档，用于从 iss 中读取数据。
    BoostPersistRaftNode boostPersistRaftNode;
    //反序列化的数据读取到一个 BoostPersistRaftNode 对象中
    //它负责从输入流中读取数据。当使用 >> 操作符与输入存档对象一起使用时，它会调用与存档兼容的类型的对象的 serialize 函数。
    ia >> boostPersistRaftNode; //直接读取到对象？？

    //恢复
    m_currentTerm = boostPersistRaftNode.m_currentTerm;
    m_votedFor = boostPersistRaftNode.m_votedFor;
    m_lastSnapshotIncludeIndex = boostPersistRaftNode.m_lastSnapshotIncludeIndex;
    m_lastSnapshotIncludeterm = boostPersistRaftNode.m_lastSnapshotIncludeterm;
    m_logs.clear();
    for (auto & it : boostPersistRaftNode.m_logs) {
        raftRpcProtoc::LogEntry logentry;
        logentry.ParseFromString(it); //上面持久化的时候对日志进行SerializeAsString，这里恢复再ParseFromString
        m_logs.push_back(logentry);
    }
}

void Raft::applierTicker() {
    while (true) {
        if (m_status == Leader) {
            DPrintf("[Raft::applierTicker() - raft{%d}]  m_lastApplied{%d}   m_commitIndex{%d}", m_me, m_lastApplied,
                    m_commitIndex);
        }
        //std::lock_guard<std::mutex> lg(m_mtx);
        m_mtx.lock();
        auto msg = getApplyLogs();
        m_mtx.unlock();
        if (!msg.empty()) {
            //format也可以吧
            DPrintf("[func- Raft::applierTicker()-raft{%d}] 向kvserver报告的applyMsgs长度爲：{%d}", m_me, applyMsgs.size()); 
        }
        myAssert(!msg.empty(), format("[func- Raft::applierTicker()-raft{%d}] 向kvserver報告的applyMsgs長度爲：{%d}", m_me, applyMsgs.size()));
        for (auto & it : msg) {
            //可以直接pushmsgtokv函数
            applyChan->Push(it); //这个Push函数有点东西=》util.h
        }
        // usleep(1000 * ApplyInterval); 因为usleep是微秒级别
        sleepNMilliseconds(ApplyInterval); //毫秒级别，封装了std::chrono
    }
} 

//获取需要应用到状态机的日志条目
std::vector<ApplyMsg> Raft::getApplyLogs() {
    std::vector<Applymsg> applyMsgs;
    myAssert(m_commitIndex < getLastLogIndex(), format("func-getApplyLogs-rf{%d} m_commitIndex{%d} > getLastLogIndex{%d}", m_me, m_commitIndex, getLastLogIndex()));

    while (m_lastApplied < m_commitIndex) {
        m_lastApplied++;
        myAssert(m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].logindex() == m_lastApplied,
                 format("rf.logs[rf.getSlicesIndexFromLogIndex(rf.lastApplied)].LogIndex{%d} != rf.lastApplied{%d} ",
                 m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].logindex(), m_lastApplied));
        ApplyMsg applyMsg;
        applyMsg.CommandValid = true;
        applyMsg.Command = m_logs[getSlicesIndexFromLogIndex(m_lastApplied)].command();
        applyMsg.CommandIndex = m_lastApplied;
        applyMsgs.SnapshotValid = false; //这个为false就少了三个成员
        applyMsgs.push_back(applyMsg);
    }
    return applyMsgs;
}

// doHeartBeat调用，也是appendentriesarg的参数
// leader调用，传入：服务器index，传出：发送的AE的preLogIndex和PrevLogTerm
void Raft::getPrevLogInfo(int server, int* preIndex, int* preTerm) {
  // logs长度为0返回0,0，不是0就根据nextIndex数组的数值返回
  if (m_nextIndex[server] == m_lastSnapshotIncludeIndex + 1) {
    //要发送的日志是第一个日志，因此直接返回m_lastSnapshotIncludeIndex和m_lastSnapshotIncludeTerm
    *preIndex = m_lastSnapshotIncludeIndex;
    *preTerm = m_lastSnapshotIncludeTerm;
    return;
  }
  auto nextIndex = m_nextIndex[server];
  *preIndex = nextIndex - 1;
  *preTerm = m_logs[getSlicesIndexFromLogIndex(*preIndex)].logterm();
}

//其实这个函数直接用m_status == Leader不是一样的吗，但是要给kvserver调用
void Raft::GetState(int* term, bool* isLeader) {
  m_mtx.lock();
  DEFER {
    // todo 暂时不清楚会不会导致死锁
    m_mtx.unlock();
  };

  // Your code here (2A).
  *term = m_currentTerm;
  *isLeader = (m_status == Leader);
}

//接收者
void Raft::InstallSnapshot(const raftRpcProctoc::InstallSnapshotRequest* args,
                           raftRpcProctoc::InstallSnapshotResponse* reply) {
    //std::lock_guard<std::mutex> lg(m_mtx);
    m_mtx.lock();
    DERFER { m_mtx.unlock(); };
    if (args->term() < m_currentTerm)
    {
        reply->set_term(m_currentTerm);
        return;
    }
    else if(args->term() > m_currentTerm)
    {
        m_currentTerm = args->term();
        m_votedFor = -1;
        m_status = Follower; // 为啥这个不用设置超时时间
        persist();
    }
    //和appendentries一样无论谁收到installsnapshot都要变为follower
    m_status = Follower;
    m_lastResetElectionTime = now();

    if (args->lastsnapshotincludeindex() <= m_lastSnapShotIncludeIndex) {
        return;
    }
    ////截断日志，修改commitIndex和lastApplied
    //截断日志包括：日志长了，截断一部分，日志短了，全部清空，其实两个是一种情况
    //但是由于现在getSlicesIndexFromLogIndex的实现，不能传入不存在logIndex，否则会panic
    auto lastLogIndex = getLastLogIndex();
    if (args->lastsnapshotincludeindex() < lastLogIndex) {
        m_logs.erase(m_logs.begin(), m_logs.begin() + getSlicesIndexFromLogIndex(args->lastsnapshotincludeindex()) + 1);
    } else {
        //snapshot都大于你最新的（最后一个日志）直接全部清空只要snapshot就行
        m_logs.clear();
    }

    m_commitIndex = std::max(m_commitIndex, args->lastsnapshotincludeindex());
    m_lastapplied = std::max(m_lastapplied, args->lastsnapshotincludeindex());

    m_lastSnapShotIncludeIndex = args->lastsnapshotincludeindex();
    m_lastSnapShotIncludeTerm = args->lastsnapshotincludeterm();
    //args还有data字段

    reply->set_term(m_currentTerm);
    ApplyMsg applyMsg;
    applyMsg.SnapshotValid = true;
    applyMsg.SnapshotTerm = args->lastsnapshotincludeterm();
    applyMsg.SnapshotIndex = args->lastsnapshotincludeindex();
    applyMsg.Snapshot = args->data();

    applyChan->Push(applyMsg); //存疑这里重复推送了啊？？
    std::thread t(&pushMsgToKvServer, this, applyMsg);
    t.datach();

    //还有持久化一下刚来的snapshot
    //persister有两个save，第二个是savestate
    m_persister->Save(persistData(), args->data());
    //说白了raftstate就是InstallSnapshotRequest除了data的字段，包括日志，data只是snapshot
    //日志+snapshot = 全部
}

//server调用，但是是否安装快照是要对比很多条件的
bool Raft::CondInstallSnapshot(int lastIncludedTerm, int lastIncludedIndex, std::string snapshot) {
  return true;
  //// Your code here (2D).
  // rf.mu.Lock()
  // defer rf.mu.Unlock()
  // DPrintf("{Node %v} service calls CondInstallSnapshot with lastIncludedTerm %v and lastIncludedIndex {%v} to check
  // whether snapshot is still valid in term %v", rf.me, lastIncludedTerm, lastIncludedIndex, rf.currentTerm)
  //// outdated snapshot
  // if lastIncludedIndex <= rf.commitIndex {
  //	return false
  // }
  //
  // lastLogIndex, _ := rf.getLastLogIndexAndTerm()
  // if lastIncludedIndex > lastLogIndex {
  //	rf.logs = make([]LogEntry, 0)
  // } else {
  //	rf.logs = rf.logs[rf.getSlicesIndexFromLogIndex(lastIncludedIndex)+1:]
  // }
  //// update dummy entry with lastIncludedTerm and lastIncludedIndex
  // rf.lastApplied, rf.commitIndex = lastIncludedIndex, lastIncludedIndex
  //
  // rf.persister.Save(rf.persistData(), snapshot)
  // return true
}

//这些 RPC 函数通常由 Raft 节点的网络层调用，以实现节点之间的通信。例如，当一个节点需要向其他节点发送 AppendEntries 请求时，它会使用这些接口之一来发起 RPC 调用。
//done：一个回调函数，当 RPC 调用完成时会被执行。执行回调函数，这是异步 RPC 调用中常见的模式。当 RPC 调用完成并且响应已经被处理后，done 回调被触发，以通知调用者 RPC 已经完成。
void AppendEntries(google::protobuf::RpcController *controller, const ::raftRpcProctoc::AppendEntriesArgs *request,
                       ::raftRpcProctoc::AppendEntriesReply *response, ::google::protobuf::Closure *done) {
    AppendEntries1(request, response);
    done->Run();
}

void InstallSnapshot(google::protobuf::RpcController *controller,
                     const ::raftRpcProctoc::InstallSnapshotRequest *request,
                     ::raftRpcProctoc::InstallSnapshotResponse *response, ::google::protobuf::Closure *done)
{
    InstallSnapshot(request, response);
    done->Run();
}

void RequestVote(google::protobuf::RpcController *controller, const ::raftRpcProtoc::RequestVoteArgs *arg,
                 ::raftRpcProtoc::RequestVoteReply *response, ::google::protobuf::Closure *done)
{
    RequestVote(request, response);
    done->Run();
}

//用于在领导者节点上启动一个操作（Op）
void Start(Op command, int *newlogindex, int *newlogterm, bool *isleader) {
    //要修改节点成员，加锁
    std::lock_guard<std::mutex> lg(m_mtx);
    if (m_status != Leader) {
        *newlogindex = -1;
        *newlogterm = -1;
        *isLeader = false;
        return;
    }
    raftRpcProtoc::LogEntry newLog; //有新命令就代表是日志
    //asString()是util.h一个函数,序列化吧可能？？
    newLog.set_command(command.asString());
    newLog.set_logterm(m_currentTerm);
    newLog.set_logindex(getNewCommandIndex());
    //这个log会在心跳超时发送，doHeartBeat=> sendAppendrntry()
    m_logs.emplace_back(newLog);

    // leader应该不停的向各个Follower发送AE来维护心跳和保持日志同步，目前的做法是新的命令来了不会直接执行，而是等待leader的心跳触发
    //好像可以设置一个很短的超时时间？
    DPrintf("[func-Start-rf{%d}]  lastLogIndex:%d,command:%s\n", m_me, lastLogIndex, &command);
    persist(); // 日志更新了得持久化吧
    *newlogindex = newlog.logindex();
    *newlogterm = newlog.logterm();
    *isLeader = true;
}

//新日志的index必须last+1
int getNewCommandIndex() {
    auto lastlogindex = getLastLogIndex();
    return lastlogindex + 1;
}

//节点创建快照并持久化
//注意可以不是全部日志都创建，所以会有剩下的日志
void snapshot(int index, std::string snapshot) {
    //得修改last啥的成员，加锁！
    std::lock_guard<std::mutex> lg(m_mtx);
    auto lastlogindex = getLastLogIndex();
    //还是判断index的合理性,我觉得这里还要加上判定lastindex的
    if (index <= m_lastSnapshotIncludeIndex || index > m_commitIndex || index > lastlogindex) {
        DPrint("[func-snapshot-rf{%d}] refuse replacing log with snapshot");
        return;
    }
    int newLastSnapshotIncludeIndex = index;
    int newLastSnapshotIncludeTerm = m_logs[getSlicesIndexFromLogIndex(index)].logterm();
    std::vector<raftRpcProtoc::LogEntry> trunkedLogs;
    for (int i = index + 1; i <= lastlogindex; ++i) {
        trunkedLogs.emplace_back(m_logs[getSlicesIndexFromLogIndex(i)]);
    }
    m_lastSnapshotIncludeIndex = newLastSnapshotIncludeIndex;
    m_lastSnapshotIncludeTerm = newLastSnapshotIncludeTerm;
    m_logs = trunkedLogs; //更新截断后的日志
    //日志=》快照肯定要更新commit和applied的
    m_commitIndex = std::max(m_commitIndex, m_lastSnapshotIncludeIndex);
    m_lastApplied = std::max(m_lastApplied, m_lastSnapshotIncludeIndex);

    //persistdata函数是把BoostPersistRaftNode的成员变量写入字符串流ss, save()函数就是把persistData()的字符串写入snapshot
    m_persister->Save(persistData(), snapshot);
    DPrintf("[SnapShot]Server %d snapshot snapshot index {%d}, term {%d}, loglen {%d}", m_me, index,
            m_lastSnapshotIncludeTerm, m_logs.size());
    myAssert(m_logs.size() + m_lastSnapshotIncludeIndex == lastLogIndex,
             format("len(rf.logs){%d} + rf.lastSnapshotIncludeIndex{%d} != lastLogjInde{%d}", m_logs.size(),
             m_lastSnapshotIncludeIndex, lastLogIndex));

}
void init(std::vector<std::shared_ptr<RaftRpcUtil>> peers, int me, std::shared_ptr<Persister> persister,
          std::shared_ptr<LockQueue<ApplyMsg>> applych)
{
    m_peers = peers;
    m_me = me;
    m_persister = persister; //这三个变量不会被多次访问和修改
    m_mtx.lock();

    this->apllyChan = applych;
    m_status = Follower;
    m_currentTerm = 0;
    m_commitIndex = 0;
    m_lastApplied = 0;
    m_logs.clear();

    for (int i = 0; i < m_peers.size(); ++i) {
        m_matchIndex.push_back(0);
        m_nextIndex.push_back(0);
    }
    m_votedFor = -1;
    m_lastSnapshotIncludeIndex = 0;
    m_lastSnapshotIncludeTerm = 0;
    m_lastResetElectionTime = now();
    m_lastResetHeartBeatTime = now();

    //崩溃还要恢复状态
    readPersist(m_persister->ReadRaftState());
    if (m_lastSnapshotIncludeIndex > 0) {
        m_lastApplied = m_lastSnapshotIncludeIndex;
    }

    DPrintf("[Init&ReInit] Sever %d, term %d, lastSnapshotIncludeIndex {%d} , lastSnapshotIncludeTerm {%d}", m_me,
          m_currentTerm, m_lastSnapshotIncludeIndex, m_lastSnapshotIncludeTerm);
    m_mtx.unlock();
    //调用协程
    m_ioManager = std::make_unique<monsoon::IOManager>(FIBER_THREAD_NUM, FIBER_USE_CALLER_THREAD);

    m_ioManager->schedule([this]() -> void
                          { this->leaderHeartBeatTicker(); });
    m_ioManager->schedule([this]() -> void
                          { this->electionTimeOutTicker(); });
    std::thread t3(&Raft::applierTicker, this);
    t3.detach();
}