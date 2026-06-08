#ifndef MEMORY_BLOCK_H
#define MEMORY_BLOCK_H

#include <string>

/// 内存块结构体（动态分区链表节点）
struct MemoryBlock {
    int start_addr;     // 起始地址 (KB)
    int size;           // 块大小 (KB)
    bool is_free;       // 是否空闲
    int pid;            // 归属进程ID，空闲时为 -1
    MemoryBlock* next;  // 链表下一节点
    MemoryBlock* prev;  // 链表上一节点

    MemoryBlock(int addr, int sz, bool free, int p = -1)
        : start_addr(addr), size(sz), is_free(free), pid(p)
        , next(nullptr), prev(nullptr) {}

    /// 块的结束地址
    int end_addr() const { return start_addr + size; }

    /// 格式化为字符串
    std::string to_string() const;
};

#endif // MEMORY_BLOCK_H
