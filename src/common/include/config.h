#ifndef CONFIG_H
#define CONFIG_H

const bool Debug = true;
// 时间单位：time.Millisecond，不同网络环境rpc速度不同，因此需要乘以一个系数
const int debugMul = 1;

const int HeartBeatTimeout = 25 * debugMul;
const int ApplyInterval = 10 * debugMul;

const int minRandomizedElectionTime = 300 * debugMul;
const int maxRandomizedElectionTime = 500 * debugMul;

//协程设置

const int FIBER_THREAD_NUM = 1;            // 协程库中线程池大小
const bool FIBER_USE_CALLER_THREAD = false;// 是否使用caller_thread执行调度任务
#endif