#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include "common/types.h"
#include "common/sync.h"
#include "process/pcb.h"

// 前向声明
class MemoryManager;

/// 进程管理器 — 管理所有进程的生命周期
class ProcessManager {
public:
    /// 构造函数，传入内存管理器引用
    explicit ProcessManager(MemoryManager& mem_mgr);
    ~ProcessManager() = default;

    // 禁止拷贝
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;

    // ========== 10 个进程管理命令 ==========

    /// 创建新进程，返回 PID；失败返回 -1
    /// @param name     进程名
    /// @param priority 优先级 (0-15)
    /// @param mem_size 内存大小(KB)，默认64KB
    /// @param ppid     父进程PID，默认1（init的子进程）
    int create_pcb(const std::string& name, int priority,
                   int mem_size = DEFAULT_PROC_MEM, int ppid = 1);

    /// 终止进程（递归杀死所有子进程并回收内存）
    bool kill_pcb(int pid);

    /// 阻塞进程
    bool block_pcb(int pid);

    /// 唤醒进程
    bool wakeup_pcb(int pid);

    /// 查看进程详细信息
    std::string show_pcb(int pid) const;

    /// 列出所有活跃进程
    std::string list_pcb() const;

    /// 树形结构展示进程父子关系
    std::string ptree() const;

    /// 挂起进程（从调度队列移除）
    bool suspend(int pid);

    /// 恢复挂起进程
    bool resume(int pid);

    /// 修改进程优先级
    bool renice(int pid, int new_priority);

    // ========== 辅助接口 ==========

    /// 获取 PCB 引用（只读）
    const PCB* get_pcb(int pid) const;

    /// 获取 PCB 引用（可修改，供调度器使用）
    PCB* get_pcb_mutable(int pid);

    /// 获取所有 PCB 的 PID 列表
    std::vector<int> get_all_pids() const;

    /// 获取子进程 PID 列表
    std::vector<int> get_children(int pid) const;

    /// 初始化 init 进程
    int init();

    /// 获取互斥锁
    Mutex& mutex() { return mutex_; }

    // ========== 持久化接口 ==========
    int get_next_pid() const { return next_pid_; }
    void set_next_pid(int pid) { next_pid_ = pid; }
    const std::map<int, PCB>& get_pcb_table() const { return pcb_table_; }
    /// 直接设置 PCB 表（load 恢复用）
    void set_pcb_table(const std::map<int, PCB>& table) { pcb_table_ = table; }

private:
    MemoryManager& mem_mgr_;           // 内存管理器引用
    std::map<int, PCB> pcb_table_;    // PID -> PCB
    int next_pid_;                    // 下一个可用PID
    mutable Mutex mutex_;             // 线程安全锁

    /// 递归获取进程树字符串
    void ptree_recursive(int pid, const std::string& prefix, bool is_last,
                         std::vector<int>& visited, std::string& output) const;

    /// 递归杀死子进程
    void kill_children(int pid);
};

#endif // PROCESS_MANAGER_H
