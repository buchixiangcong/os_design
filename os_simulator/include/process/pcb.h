#ifndef PCB_H
#define PCB_H

#include <string>
#include <vector>
#include <cstdint>
#include "common/types.h"

/// 进程控制块 (PCB)
struct PCB {
    int pid;                   // 进程ID
    int ppid;                  // 父进程ID (0 表示 init 或无父进程)
    std::string name;          // 进程名称
    ProcessState state;        // 当前状态
    int priority;              // 优先级 (0-15)
    int cpu_time;              // 已执行CPU时间（时间片累积）
    int mem_addr;              // 分配的内存起始地址 (-1 表示未分配)
    int mem_size;              // 分配的内存大小 (KB, 0 表示未分配)
    std::vector<int> children; // 子进程PID列表

    PCB() : pid(-1), ppid(0), name(""), state(ProcessState::CREATED),
            priority(5), cpu_time(0), mem_addr(-1), mem_size(0) {}

    PCB(int id, int parent, const std::string& nm, int pri = 5)
        : pid(id), ppid(parent), name(nm), state(ProcessState::CREATED),
          priority(pri), cpu_time(0), mem_addr(-1), mem_size(0) {}

    /// 格式化为单行摘要（用于 list_pcb）
    std::string to_short_string() const;

    /// 格式化为详细信息（用于 show_pcb）
    std::string to_detailed_string() const;
};

#endif // PCB_H
