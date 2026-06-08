#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <string>
#include <vector>
#include <cstdint>

// ============================================================
// 全局常量定义
// ============================================================

/// 系统总内存大小 (KB)，建议 1024KB = 1MB
constexpr int TOTAL_MEMORY = 1024;

/// 最大进程数量
constexpr int MAX_PCB_COUNT = 256;

/// MLFQ 队列数量
constexpr int MLFQ_LEVELS = 3;

/// 默认进程内存分配大小 (KB)
constexpr int DEFAULT_PROC_MEM = 64;

/// 状态持久化文件名
constexpr const char* STATE_FILE = "data/state.bin";

/// 多实例文件检查间隔 (毫秒)
constexpr int FILE_CHECK_INTERVAL_MS = 500;

/// 最大登录尝试次数
constexpr int MAX_LOGIN_ATTEMPTS = 3;

// ============================================================
// 枚举类型定义
// ============================================================

/// 进程状态枚举
enum class ProcessState {
    CREATED,    // 新建
    READY,      // 就绪
    RUNNING,    // 运行中
    BLOCKED,    // 阻塞
    SUSPENDED,  // 挂起
    TERMINATED  // 终止
};

/// 内存分配算法枚举
enum class MemAlgo {
    FIRST_FIT,   // 首次适应
    BEST_FIT,    // 最佳适应
    WORST_FIT    // 最坏适应
};

/// 消息类型枚举（用于前后台通信）
enum class MessageType {
    CMD_CREATE_PCB,
    CMD_KILL_PCB,
    CMD_BLOCK_PCB,
    CMD_WAKEUP_PCB,
    CMD_SHOW_PCB,
    CMD_LIST_PCB,
    CMD_PTREE,
    CMD_SUSPEND,
    CMD_RESUME,
    CMD_RENICE,
    CMD_START_SCHED,
    CMD_STOP_SCHED,
    CMD_RESTART_SCHED,
    CMD_STEP,
    CMD_ALLOC,
    CMD_FREE_MEM,
    CMD_SHOW_MEM,
    CMD_COMPACT,
    CMD_MEM_STAT,
    CMD_SET_ALLOC_ALGO,
    CMD_PGFAULT,
    CMD_SWAP_OUT,
    CMD_SAVE,
    CMD_LOAD,
    CMD_OVERVIEW,
    CMD_REGISTER,
    CMD_LOGIN,
    CMD_LOGOUT,
    CMD_EXIT,
    CMD_UNKNOWN
};

// ============================================================
// MLFQ 配置
// ============================================================

/// 各队列的时间片 (秒)
constexpr int MLFQ_TIME_SLICE[MLFQ_LEVELS] = {2, 4, 8};

/// 各队列的优先级范围
constexpr int MLFQ_PRIO_MIN[MLFQ_LEVELS] = {0, 4, 8};
constexpr int MLFQ_PRIO_MAX[MLFQ_LEVELS] = {3, 7, 15};

/// 根据优先级获取所属队列级别
inline int get_queue_level(int priority) {
    for (int i = 0; i < MLFQ_LEVELS; ++i) {
        if (priority >= MLFQ_PRIO_MIN[i] && priority <= MLFQ_PRIO_MAX[i]) {
            return i;
        }
    }
    return MLFQ_LEVELS - 1; // 兜底：最低优先级队列
}

/// 将进程状态转换为字符串
inline std::string state_to_string(ProcessState s) {
    switch (s) {
        case ProcessState::CREATED:    return "CREATED";
        case ProcessState::READY:      return "READY";
        case ProcessState::RUNNING:    return "RUNNING";
        case ProcessState::BLOCKED:    return "BLOCKED";
        case ProcessState::SUSPENDED:  return "SUSPENDED";
        case ProcessState::TERMINATED: return "TERMINATED";
        default: return "UNKNOWN";
    }
}

/// 将内存算法转换为字符串
inline std::string algo_to_string(MemAlgo a) {
    switch (a) {
        case MemAlgo::FIRST_FIT: return "FIRST_FIT";
        case MemAlgo::BEST_FIT:  return "BEST_FIT";
        case MemAlgo::WORST_FIT: return "WORST_FIT";
        default: return "UNKNOWN";
    }
}

/// 将消息类型转换为字符串
inline std::string msgtype_to_string(MessageType t) {
    switch (t) {
        case MessageType::CMD_CREATE_PCB:     return "create_pcb";
        case MessageType::CMD_KILL_PCB:       return "kill_pcb";
        case MessageType::CMD_BLOCK_PCB:      return "block_pcb";
        case MessageType::CMD_WAKEUP_PCB:     return "wakeup_pcb";
        case MessageType::CMD_SHOW_PCB:       return "show_pcb";
        case MessageType::CMD_LIST_PCB:       return "list_pcb";
        case MessageType::CMD_PTREE:          return "ptree";
        case MessageType::CMD_SUSPEND:        return "suspend";
        case MessageType::CMD_RESUME:         return "resume";
        case MessageType::CMD_RENICE:         return "renice";
        case MessageType::CMD_START_SCHED:    return "start_sched";
        case MessageType::CMD_STOP_SCHED:     return "stop_sched";
        case MessageType::CMD_RESTART_SCHED:  return "restart_sched";
        case MessageType::CMD_STEP:           return "step";
        case MessageType::CMD_ALLOC:          return "alloc";
        case MessageType::CMD_FREE_MEM:       return "free_mem";
        case MessageType::CMD_SHOW_MEM:       return "show_mem";
        case MessageType::CMD_COMPACT:        return "compact";
        case MessageType::CMD_MEM_STAT:       return "mem_stat";
        case MessageType::CMD_SET_ALLOC_ALGO: return "set_alloc_algo";
        case MessageType::CMD_PGFAULT:        return "pgfault";
        case MessageType::CMD_SWAP_OUT:       return "swap_out";
        case MessageType::CMD_SAVE:           return "save";
        case MessageType::CMD_LOAD:           return "load";
        case MessageType::CMD_OVERVIEW:       return "overview";
        case MessageType::CMD_REGISTER:       return "register";
        case MessageType::CMD_LOGIN:          return "login";
        case MessageType::CMD_LOGOUT:         return "logout";
        case MessageType::CMD_EXIT:           return "exit";
        default: return "unknown";
    }
}

#endif // COMMON_TYPES_H
