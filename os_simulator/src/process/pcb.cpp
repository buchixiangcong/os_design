#include "process/pcb.h"
#include <sstream>
#include <iomanip>

std::string PCB::to_short_string() const {
    std::ostringstream oss;
    oss << "PID:" << std::setw(4) << pid
        << "  " << std::setw(10) << state_to_string(state)
        << "  prio:" << std::setw(2) << priority
        << "  CPU:" << std::setw(6) << cpu_time
        << "  mem:" << std::setw(4) << mem_size << "K"
        << "  " << name;
    return oss.str();
}

std::string PCB::to_detailed_string() const {
    std::ostringstream oss;
    oss << "\n========== 进程控制块 (PCB) 详情 ==========\n";
    oss << "  PID:        " << pid << "\n";
    oss << "  名称:       " << name << "\n";
    oss << "  父进程PID:  " << ppid << "\n";
    oss << "  状态:       " << state_to_string(state) << "\n";
    oss << "  优先级:     " << priority << "\n";
    oss << "  CPU时间:    " << cpu_time << " (累计时间片)\n";
    oss << "  内存地址:   " << mem_addr << "KB\n";
    oss << "  内存大小:   " << mem_size << "KB\n";
    oss << "  子进程:     [";
    for (size_t i = 0; i < children.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << children[i];
    }
    oss << "]\n";
    oss << "  所属MLFQ:   Q" << get_queue_level(priority) << "\n";
    oss << "==========================================\n";
    return oss.str();
}
