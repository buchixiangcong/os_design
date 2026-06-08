#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <string>
#include <vector>
#include "common/types.h"
#include "common/sync.h"
#include "memory/memory_block.h"

/// 内存管理器 — 动态分区分配（FF/BF/WF）
class MemoryManager {
public:
    MemoryManager();
    ~MemoryManager();

    // 禁止拷贝
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    // ========== 命令接口 ==========

    /// 分配指定大小的内存块，返回起始地址；失败返回 -1
    /// @param size  请求大小 (KB)
    /// @param pid   归属进程ID
    int alloc(int size, int pid);

    /// 释放指定地址的内存块，自动合并相邻空闲块
    /// @param addr  释放的起始地址
    /// @return 是否成功
    bool free_mem(int addr);

    /// 显示当前内存布局（已分配块和空闲块链表）
    std::string show_mem() const;

    /// 内存碎片紧缩：将所有已分配块向低地址移动，空闲块合并到高地址
    void compact();

    /// 内存使用统计：返回 {总内存, 已用, 空闲, 碎片率(%)}
    struct MemStat {
        int total;
        int used;
        int free;
        double fragmentation_rate; // 碎片率 = (1 - 最大连续空闲/总空闲) * 100
    };
    MemStat mem_stat() const;

    /// 切换分配算法
    void set_alloc_algo(MemAlgo algo);
    MemAlgo get_algo() const { return current_algo_; }

    /// 模拟缺页中断
    std::string pgfault(int pid) const;

    /// 模拟换出：释放指定进程的内存
    bool swap_out(int pid);

    // ========== 辅助接口 ==========

    /// 重置内存为初始状态
    void reset();

    /// 获取互斥锁引用（供外部锁定用）
    Mutex& mutex() { return mutex_; }

    // ========== 持久化接口 ==========
    /// 获取内存块链表头（只读）
    const MemoryBlock* get_head() const { return head_; }
    /// 从链表尾追加块（load 恢复用）
    void append_block(int addr, int size, bool free, int pid);
    /// 清空当前链表（仅删除所有块，不创建新块）
    void clear_all();

private:
    MemoryBlock* head_;           // 内存块链表头
    MemAlgo current_algo_;       // 当前分配算法
    mutable Mutex mutex_;         // 线程安全锁

    // ========== 内部辅助方法 ==========

    /// 首次适应算法查找空闲块
    MemoryBlock* find_first_fit(int size) const;

    /// 最佳适应算法查找空闲块
    MemoryBlock* find_best_fit(int size) const;

    /// 最坏适应算法查找空闲块
    MemoryBlock* find_worst_fit(int size) const;

    /// 按当前算法查找合适空闲块
    MemoryBlock* find_free_block(int size) const;

    /// 在指定块后插入新块
    void insert_after(MemoryBlock* target, MemoryBlock* new_block);

    /// 从链表中移除块
    void remove_block(MemoryBlock* block);

    /// 尝试与相邻空闲块合并
    void merge_adjacent_free(MemoryBlock* block);
};

#endif // MEMORY_MANAGER_H
