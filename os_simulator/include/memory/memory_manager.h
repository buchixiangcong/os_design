#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <string>
#include <vector>
#include <sstream>
#include "common/types.h"
#include "common/sync.h"

// ============================================================
// 内存块结构体（动态分区链表节点）
// ============================================================
struct MemoryBlock {
    int start_addr, size, pid;
    bool is_free;
    MemoryBlock *next = nullptr, *prev = nullptr;

    MemoryBlock(int addr, int sz, bool fr, int p = -1)
        : start_addr(addr), size(sz), is_free(fr), pid(p) {}

    int end_addr() const { return start_addr + size; }

    std::string to_string() const {
        std::ostringstream oss;
        if (is_free)
            oss << "[空闲 " << size << "KB @" << start_addr << "-" << end_addr() << "]";
        else
            oss << "[已分配 " << size << "KB @" << start_addr << "-" << end_addr()
                << " PID:" << pid << "]";
        return oss.str();
    }
};

// ============================================================
// 内存管理器 — 动态分区分配（FF/BF/WF）
// ============================================================
class MemoryManager {
public:
    MemoryManager() : current_algo_(MemAlgo::FIRST_FIT) {
        head_ = new MemoryBlock(0, TOTAL_MEMORY, true);
    }

    ~MemoryManager() { clear_all(); }

    // ========== 命令接口 ==========
    int alloc(int size, int pid);
    bool free_mem(int addr);
    std::string show_mem() const;
    void compact();
    void set_alloc_algo(MemAlgo a) { LockGuard lock(mutex_); current_algo_ = a; }
    MemAlgo get_algo() const { return current_algo_; }
    std::string pgfault(int pid) const;
    bool swap_out(int pid);

    struct MemStat { int total, used, free; double fragmentation_rate; };
    MemStat mem_stat() const;

    // ========== 持久化 / 调试接口 ==========
    void reset();
    void clear_all();
    void append_block(int addr, int size, bool fr, int pid);
    const MemoryBlock* get_head() const { return head_; }
    Mutex& mutex() { return mutex_; }

private:
    MemoryBlock* head_ = nullptr;
    MemAlgo current_algo_;
    mutable Mutex mutex_;

    MemoryBlock* find_free_block(int size) const;
    MemoryBlock* find_first_fit(int size) const;
    MemoryBlock* find_best_fit(int size) const;
    MemoryBlock* find_worst_fit(int size) const;
    void insert_after(MemoryBlock* t, MemoryBlock* n);
    void remove_block(MemoryBlock* b);
    void merge_adjacent_free(MemoryBlock* b);
};

#endif
