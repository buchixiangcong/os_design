#ifndef PCB_H
#define PCB_H

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include "common/types.h"

/// 进程控制块 (PCB)
struct PCB {
    int pid = -1, ppid = 0, priority = 5, cpu_time = 0;
    int mem_addr = -1, mem_size = 0;
    int burst_remain = 3;  // 剩余调度次数，0 自动终止
    std::string name;
    ProcessState state = ProcessState::CREATED;
    std::vector<int> children;

    PCB() = default;
    PCB(int id, int parent, const std::string& nm, int pri = 5)
        : pid(id), ppid(parent), name(nm), priority(pri) {}

    /// 单行摘要（用于 list_pcb）
    std::string to_short_string() const {
        std::ostringstream oss;
        oss << "PID:" << std::setw(4) << pid
            << "  " << std::setw(10) << state_to_string(state)
            << "  prio:" << std::setw(2) << priority
            << "  CPU:" << std::setw(6) << cpu_time
            << "  mem:" << std::setw(4) << mem_size << "K"
            << "  " << name;
        return oss.str();
    }

    /// 详细信息（用于 show_pcb）
    std::string to_detailed_string() const {
        std::ostringstream oss;
        oss << "\n========== 进程控制块 (PCB) 详情 ==========\n"
            << "  PID: " << pid << "  名称: " << name << "\n"
            << "  父进程: " << ppid << "  状态: " << state_to_string(state) << "\n"
            << "  优先级: " << priority << "  CPU: " << cpu_time << "\n"
            << "  内存: " << mem_addr << "KB +" << mem_size << "KB\n"
            << "  所属MLFQ: Q" << get_queue_level(priority) << "\n"
            << "  子进程: [";
        for (size_t i = 0; i < children.size(); ++i)
            oss << (i ? ", " : "") << children[i];
        oss << "]\n==========================================\n";
        return oss.str();
    }
};

#endif
