#include "process/process_manager.h"
#include "memory/memory_manager.h"
#include <iostream>
#include <sstream>
#include <algorithm>

// ============================================================
// 构造与初始化
// ============================================================

ProcessManager::ProcessManager(MemoryManager& mem_mgr)
    : mem_mgr_(mem_mgr), next_pid_(1)
{
}

int ProcessManager::init() {
    LockGuard lock(mutex_);

    // 创建 init 进程 (PID=1)
    PCB init_pcb(1, 0, "init", 0);
    init_pcb.state = ProcessState::RUNNING;
    // init 进程默认分配 64KB 内存
    int addr = mem_mgr_.alloc(DEFAULT_PROC_MEM, 1);
    init_pcb.mem_addr = addr;
    init_pcb.mem_size = (addr >= 0) ? DEFAULT_PROC_MEM : 0;

    pcb_table_[1] = init_pcb;
    next_pid_ = 2;

    std::cout << "[进程] init 进程创建完成 (PID=1, 内存 "
              << DEFAULT_PROC_MEM << "KB)" << std::endl;
    return 1;
}

// ============================================================
// 进程管理命令
// ============================================================

int ProcessManager::create_pcb(const std::string& name, int priority,
                                 int mem_size, int ppid) {
    LockGuard lock(mutex_);

    // 校验优先级范围
    if (priority < 0 || priority > 15) {
        std::cout << "[错误] 优先级必须在 0-15 之间。" << std::endl;
        return -1;
    }

    // 校验内存大小
    if (mem_size <= 0 || mem_size > TOTAL_MEMORY) {
        std::cout << "[错误] 内存大小必须在 1-" << TOTAL_MEMORY << "KB 之间。" << std::endl;
        return -1;
    }

    // 校验父进程存在
    if (ppid != 0 && pcb_table_.find(ppid) == pcb_table_.end()) {
        std::cout << "[错误] 父进程 PID=" << ppid << " 不存在。" << std::endl;
        return -1;
    }

    // 分配内存（用户指定大小）
    int addr = mem_mgr_.alloc(mem_size, next_pid_);
    if (addr < 0) {
        std::cout << "[错误] 内存不足，无法创建进程（需要 " << mem_size << "KB）。" << std::endl;
        return -1;
    }

    // 创建 PCB
    PCB pcb(next_pid_, ppid, name, priority);
    pcb.state = ProcessState::READY;
    pcb.mem_addr = addr;
    pcb.mem_size = mem_size;

    pcb_table_[next_pid_] = pcb;

    // 更新父进程的子进程列表
    if (ppid != 0 && pcb_table_.find(ppid) != pcb_table_.end()) {
        pcb_table_[ppid].children.push_back(next_pid_);
    }

    int result_pid = next_pid_;
    ++next_pid_;
    return result_pid;
}

bool ProcessManager::kill_pcb(int pid) {
    LockGuard lock(mutex_);

    // 不能杀死 init 进程或不存在的进程
    if (pid == 1) {
        std::cout << "[错误] 不能杀死 init 进程。" << std::endl;
        return false;
    }

    auto it = pcb_table_.find(pid);
    if (it == pcb_table_.end()) {
        std::cout << "[错误] 进程 PID=" << pid << " 不存在。" << std::endl;
        return false;
    }

    // 递归杀死子进程
    kill_children(pid);

    // 释放进程占用的内存
    PCB& pcb = it->second;
    if (pcb.mem_addr >= 0 && pcb.mem_size > 0) {
        mem_mgr_.free_mem(pcb.mem_addr);
    }

    // 从父进程的子进程列表中移除
    if (pcb.ppid != 0) {
        auto parent_it = pcb_table_.find(pcb.ppid);
        if (parent_it != pcb_table_.end()) {
            auto& siblings = parent_it->second.children;
            siblings.erase(
                std::remove(siblings.begin(), siblings.end(), pid),
                siblings.end());
        }
    }

    // 从 PCB 表中删除
    pcb_table_.erase(it);

    std::cout << "[进程] 进程 " << pid << " (" << pcb.name << ") 已终止，内存已回收。" << std::endl;
    return true;
}

bool ProcessManager::block_pcb(int pid) {
    LockGuard lock(mutex_);

    auto it = pcb_table_.find(pid);
    if (it == pcb_table_.end()) return false;

    PCB& pcb = it->second;
    if (pcb.state == ProcessState::READY || pcb.state == ProcessState::RUNNING) {
        pcb.state = ProcessState::BLOCKED;
        return true;
    }
    return false; // 只能在 READY 或 RUNNING 状态下阻塞
}

bool ProcessManager::wakeup_pcb(int pid) {
    LockGuard lock(mutex_);

    auto it = pcb_table_.find(pid);
    if (it == pcb_table_.end()) return false;

    PCB& pcb = it->second;
    if (pcb.state == ProcessState::BLOCKED) {
        pcb.state = ProcessState::READY;
        return true;
    }
    return false; // 只能在 BLOCKED 状态下唤醒
}

std::string ProcessManager::show_pcb(int pid) const {
    LockGuard lock(mutex_);

    auto it = pcb_table_.find(pid);
    if (it == pcb_table_.end()) {
        return "[错误] 进程 PID=" + std::to_string(pid) + " 不存在。";
    }
    return it->second.to_detailed_string();
}

std::string ProcessManager::list_pcb() const {
    LockGuard lock(mutex_);

    std::ostringstream oss;
    oss << "\n========== 进程列表 (" << pcb_table_.size() << " 个进程) ==========\n";
    oss << "PID  状态        优先级  CPU时间  内存     名称\n";
    oss << "------------------------------------------------\n";

    for (const auto& kv : pcb_table_) {
        oss << kv.second.to_short_string() << "\n";
    }
    oss << "==========================================\n";
    return oss.str();
}

std::string ProcessManager::ptree() const {
    LockGuard lock(mutex_);

    std::string output;
    output += "\n========== 进程树 ==========\n";

    // 找到根进程（ppid == 0 的进程，通常是 init）
    for (const auto& kv : pcb_table_) {
        if (kv.second.ppid == 0) {
            std::vector<int> visited;
            ptree_recursive(kv.first, "", true, visited, output);
            break;
        }
    }

    output += "==============================\n";
    return output;
}

bool ProcessManager::suspend(int pid) {
    LockGuard lock(mutex_);

    auto it = pcb_table_.find(pid);
    if (it == pcb_table_.end()) return false;

    PCB& pcb = it->second;
    if (pcb.state == ProcessState::READY || pcb.state == ProcessState::BLOCKED) {
        pcb.state = ProcessState::SUSPENDED;
        return true;
    }
    return false;
}

bool ProcessManager::resume(int pid) {
    LockGuard lock(mutex_);

    auto it = pcb_table_.find(pid);
    if (it == pcb_table_.end()) return false;

    PCB& pcb = it->second;
    if (pcb.state == ProcessState::SUSPENDED) {
        pcb.state = ProcessState::READY;
        return true;
    }
    return false;
}

bool ProcessManager::renice(int pid, int new_priority) {
    LockGuard lock(mutex_);

    if (new_priority < 0 || new_priority > 15) {
        std::cout << "[错误] 优先级必须在 0-15 之间。" << std::endl;
        return false;
    }

    auto it = pcb_table_.find(pid);
    if (it == pcb_table_.end()) return false;

    it->second.priority = new_priority;
    return true;
}

// ============================================================
// 辅助方法
// ============================================================

const PCB* ProcessManager::get_pcb(int pid) const {
    auto it = pcb_table_.find(pid);
    return (it != pcb_table_.end()) ? &it->second : nullptr;
}

PCB* ProcessManager::get_pcb_mutable(int pid) {
    auto it = pcb_table_.find(pid);
    return (it != pcb_table_.end()) ? &it->second : nullptr;
}

std::vector<int> ProcessManager::get_all_pids() const {
    std::vector<int> pids;
    for (const auto& kv : pcb_table_) {
        pids.push_back(kv.first);
    }
    return pids;
}

std::vector<int> ProcessManager::get_children(int pid) const {
    auto it = pcb_table_.find(pid);
    if (it != pcb_table_.end()) {
        return it->second.children;
    }
    return {};
}

void ProcessManager::kill_children(int pid) {
    PCB& pcb = pcb_table_[pid];
    // 复制一份子进程列表，因为递归过程中会修改
    std::vector<int> children_copy = pcb.children;
    for (int child_pid : children_copy) {
        // 递归杀死子进程的子进程
        if (pcb_table_.find(child_pid) != pcb_table_.end()) {
            kill_children(child_pid);
            // 释放子进程内存
            PCB& child = pcb_table_[child_pid];
            if (child.mem_addr >= 0 && child.mem_size > 0) {
                mem_mgr_.free_mem(child.mem_addr);
            }
            pcb_table_.erase(child_pid);
        }
    }
    pcb.children.clear();
}

void ProcessManager::ptree_recursive(int pid, const std::string& prefix,
                                      bool is_last, std::vector<int>& visited,
                                      std::string& output) const {
    auto it = pcb_table_.find(pid);
    if (it == pcb_table_.end()) return;

    // 防止循环引用
    if (std::find(visited.begin(), visited.end(), pid) != visited.end()) {
        output += prefix + (is_last ? "└── " : "├── ") + "[循环引用 PID="
                  + std::to_string(pid) + "]\n";
        return;
    }
    visited.push_back(pid);

    const PCB& pcb = it->second;

    // 打印当前进程
    output += prefix;
    output += (is_last ? "└── " : "├── ");
    output += pcb.name + "(" + std::to_string(pid) + ")";
    output += " [" + state_to_string(pcb.state);
    output += ", prio=" + std::to_string(pcb.priority);
    output += ", mem=" + std::to_string(pcb.mem_size) + "K";
    output += "]\n";

    // 递归打印子进程
    const auto& children = pcb.children;
    for (size_t i = 0; i < children.size(); ++i) {
        bool child_is_last = (i == children.size() - 1);
        std::string child_prefix = prefix + (is_last ? "    " : "│   ");
        ptree_recursive(children[i], child_prefix, child_is_last, visited, output);
    }
}
