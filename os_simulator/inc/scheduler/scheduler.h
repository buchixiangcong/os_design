#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <string>
#include <atomic>
#include "common/types.h"
#include "common/sync.h"
#include "scheduler/mlfq.h"

// 前向声明
class ProcessManager;
class MemoryManager;

/// 调度器 — MLFQ 多级反馈队列调度
class Scheduler {
public:
    /// 构造函数
    /// @param proc_mgr  进程管理器引用
    /// @param mem_mgr   内存管理器引用
    Scheduler(ProcessManager& proc_mgr, MemoryManager& mem_mgr);
    ~Scheduler();

    // 禁止拷贝
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // ========== 调度命令 ==========

    /// 启动自动调度
    /// @return 是否启动成功
    bool start_sched();

    /// 停止自动调度
    bool stop_sched();

    /// 重启调度（从停止状态恢复）
    bool restart_sched();

    /// 单步执行一次调度
    /// @return 详细的执行过程字符串
    std::string step();

    // ========== 状态查询 ==========

    /// 调度器是否正在运行
    bool is_running() const { return running_; }

    /// 调度器是否已暂停
    bool is_paused() const { return paused_; }

    /// 获取 MLFQ 引用
    MLFQ& mlfq() { return mlfq_; }
    const MLFQ& mlfq() const { return mlfq_; }

    /// 获取互斥锁
    Mutex& mutex() { return mutex_; }

    /// 设置时间片速度倍率（验收时可调快）
    void set_speed_multiplier(double mult) { speed_mult_ = mult; }

private:
    ProcessManager& proc_mgr_;
    MemoryManager& mem_mgr_;
    MLFQ mlfq_;

    std::atomic<bool> running_;
    std::atomic<bool> paused_;
    mutable Mutex mutex_;

    double speed_mult_;  // 时间片速度倍率（默认1.0，验收时可调为0.1或更小）

    // Windows 线程句柄
    #ifdef _WIN32
    void* sched_thread_;
    static unsigned __stdcall sched_thread_func(void* arg);
    #endif

    /// 执行一次自动调度（一行摘要，不刷屏）
    std::string tick();

    /// 执行一次调度决策（step 用完整诊断输出）
    std::string execute_one_tick();

    /// 将进程加入 MLFQ（基于其优先级）
    void add_to_mlfq(int pid);

    /// 处理进程时间片耗尽后的降级
    void handle_time_slice_exhausted(int pid);
};

#endif // SCHEDULER_H
