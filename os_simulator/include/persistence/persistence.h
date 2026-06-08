#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <string>
#include <cstdint>
#include "common/types.h"

// 前向声明
class MemoryManager;
class ProcessManager;
class Scheduler;
class AccountManager;

/// 二进制持久化管理器
/// 文件格式: [Header][PCB][Memory][MLFQ][Account][Footer]
class Persistence {
public:
    /// 保存系统状态到二进制文件
    /// @return 错误信息，空字符串表示成功
    static std::string save(
        ProcessManager& pm, MemoryManager& mm,
        Scheduler& sched, AccountManager& acct,
        int next_pid, int alloc_counter,
        const std::string& path = STATE_FILE);

    /// 从二进制文件加载系统状态
    /// @return 错误信息，空字符串表示成功
    static std::string load(
        ProcessManager& pm, MemoryManager& mm,
        Scheduler& sched, AccountManager& acct,
        int& next_pid, int& alloc_counter,
        const std::string& path = STATE_FILE);

    /// 检查持久化文件是否存在
    static bool file_exists(const std::string& path = STATE_FILE);

private:
    // ========== 文件格式常量 ==========
    static constexpr uint32_t MAGIC = 0x4F534442; // "OSDB"
    static constexpr uint32_t VERSION = 1;

    // ========== 二进制读写辅助 ==========

    template <typename T>
    static bool write_val(FILE* f, const T& val) {
        return fwrite(&val, sizeof(T), 1, f) == 1;
    }

    template <typename T>
    static bool read_val(FILE* f, T& val) {
        return fread(&val, sizeof(T), 1, f) == 1;
    }

    static bool write_string(FILE* f, const std::string& s);
    static bool read_string(FILE* f, std::string& s);
};

#endif // PERSISTENCE_H
