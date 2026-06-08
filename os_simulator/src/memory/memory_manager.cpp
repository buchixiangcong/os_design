#include "memory/memory_manager.h"

void MemoryManager::reset() {
    LockGuard lock(mutex_);
    while (head_) {
        MemoryBlock* next = head_->next;
        delete head_;
        head_ = next;
    }
    head_ = new MemoryBlock(0, TOTAL_MEMORY, true);
    current_algo_ = MemAlgo::FIRST_FIT;
}

// ============================================================
// 分配算法实现
// ============================================================

MemoryBlock* MemoryManager::find_first_fit(int size) const {
    MemoryBlock* curr = head_;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            return curr;
        }
        curr = curr->next;
    }
    return nullptr;
}

MemoryBlock* MemoryManager::find_best_fit(int size) const {
    MemoryBlock* best = nullptr;
    MemoryBlock* curr = head_;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            if (!best || curr->size < best->size) {
                best = curr;
            }
        }
        curr = curr->next;
    }
    return best;
}

MemoryBlock* MemoryManager::find_worst_fit(int size) const {
    MemoryBlock* worst = nullptr;
    MemoryBlock* curr = head_;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            if (!worst || curr->size > worst->size) {
                worst = curr;
            }
        }
        curr = curr->next;
    }
    return worst;
}

MemoryBlock* MemoryManager::find_free_block(int size) const {
    switch (current_algo_) {
        case MemAlgo::FIRST_FIT: return find_first_fit(size);
        case MemAlgo::BEST_FIT:  return find_best_fit(size);
        case MemAlgo::WORST_FIT: return find_worst_fit(size);
        default: return find_first_fit(size);
    }
}

// ============================================================
// 链表操作
// ============================================================

void MemoryManager::insert_after(MemoryBlock* target, MemoryBlock* new_block) {
    new_block->next = target->next;
    new_block->prev = target;
    if (target->next) {
        target->next->prev = new_block;
    }
    target->next = new_block;
}

void MemoryManager::remove_block(MemoryBlock* block) {
    if (!block) return;

    if (block->prev) {
        block->prev->next = block->next;
    } else {
        head_ = block->next; // 移除头节点
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
}

void MemoryManager::merge_adjacent_free(MemoryBlock* block) {
    if (!block || !block->is_free) return;

    // 与下一个空闲块合并
    MemoryBlock* next = block->next;
    while (next && next->is_free) {
        block->size += next->size;
        MemoryBlock* to_delete = next;
        next = next->next;
        block->next = next;
        if (next) next->prev = block;
        delete to_delete;
    }

    // 与前一个空闲块合并
    MemoryBlock* prev = block->prev;
    while (prev && prev->is_free) {
        prev->size += block->size;
        prev->next = block->next;
        if (block->next) block->next->prev = prev;
        delete block;
        block = prev;
        prev = prev->prev;
    }
}

// ============================================================
// 命令接口实现
// ============================================================

int MemoryManager::alloc(int size, int pid) {
    LockGuard lock(mutex_);

    if (size <= 0 || size > TOTAL_MEMORY) {
        return -1;
    }

    MemoryBlock* target = find_free_block(size);
    if (!target) {
        return -1; // 无足够空间
    }

    int alloc_addr = target->start_addr;

    if (target->size == size) {
        // 刚好匹配，直接标记为已分配
        target->is_free = false;
        target->pid = pid;
    } else {
        // 切分：前面分配给进程，后面保持空闲
        MemoryBlock* allocated = new MemoryBlock(
            target->start_addr, size, false, pid);
        target->start_addr += size;
        target->size -= size;

        // 将已分配块插入到空闲块前面
        if (target->prev) {
            target->prev->next = allocated;
            allocated->prev = target->prev;
        } else {
            head_ = allocated;
        }
        allocated->next = target;
        target->prev = allocated;
    }

    return alloc_addr;
}

bool MemoryManager::free_mem(int addr) {
    LockGuard lock(mutex_);

    MemoryBlock* curr = head_;
    while (curr) {
        if (curr->start_addr == addr && !curr->is_free) {
            curr->is_free = true;
            curr->pid = -1;
            merge_adjacent_free(curr);
            return true;
        }
        curr = curr->next;
    }
    return false; // 未找到该地址或已经是空闲块
}

std::string MemoryManager::show_mem() const {
    LockGuard lock(mutex_);

    std::ostringstream oss;
    oss << "\n========== 内存布局 (共 " << TOTAL_MEMORY << "KB) ==========\n";

    MemoryBlock* curr = head_;
    while (curr) {
        oss << "  " << curr->to_string() << "\n";
        curr = curr->next;
    }

    // ASCII 可视化
    oss << "\n  内存映射图 (0-" << TOTAL_MEMORY << "KB):\n  |";
    curr = head_;
    while (curr) {
        if (curr->is_free) {
            oss << "--free(" << curr->size << "K)--";
        } else {
            oss << "##pid" << curr->pid << "(" << curr->size << "K)";
        }
        oss << "|";
        curr = curr->next;
    }

    oss << "\n==========================================\n";
    return oss.str();
}

void MemoryManager::compact() {
    LockGuard lock(mutex_);

    // 第一阶段：收集已分配块的信息（必须在销毁链表前保存）
    struct AllocInfo {
        int size;
        int pid;
    };
    std::vector<AllocInfo> alloc_info;
    MemoryBlock* curr = head_;
    while (curr) {
        if (!curr->is_free) {
            alloc_info.push_back({curr->size, curr->pid});
        }
        curr = curr->next;
    }

    // 无需紧缩
    if (alloc_info.empty()) return;

    // 第二阶段：重建整个链表
    while (head_) {
        MemoryBlock* next = head_->next;
        delete head_;
        head_ = next;
    }
    head_ = nullptr;

    // 第三阶段：紧凑放置已分配块
    int current_addr = 0;
    MemoryBlock* tail = nullptr;
    for (auto& info : alloc_info) {
        auto* new_blk = new MemoryBlock(current_addr, info.size, false, info.pid);
        if (!head_) {
            head_ = new_blk;
            tail = new_blk;
        } else {
            tail->next = new_blk;
            new_blk->prev = tail;
            tail = new_blk;
        }
        current_addr += info.size;
    }

    // 剩余空间作为空闲块
    if (current_addr < TOTAL_MEMORY) {
        auto* free_blk = new MemoryBlock(current_addr,
            TOTAL_MEMORY - current_addr, true);
        if (tail) {
            tail->next = free_blk;
            free_blk->prev = tail;
        } else {
            head_ = free_blk;
        }
    }
}

MemoryManager::MemStat MemoryManager::mem_stat() const {
    LockGuard lock(mutex_);

    MemStat stat;
    stat.total = TOTAL_MEMORY;
    stat.used = 0;
    stat.free = 0;
    int max_contiguous_free = 0;

    MemoryBlock* curr = head_;
    while (curr) {
        if (curr->is_free) {
            stat.free += curr->size;
            if (curr->size > max_contiguous_free) {
                max_contiguous_free = curr->size;
            }
        } else {
            stat.used += curr->size;
        }
        curr = curr->next;
    }

    // 碎片率 = (1 - 最大连续空闲/总空闲) * 100
    if (stat.free > 0) {
        stat.fragmentation_rate =
            (1.0 - static_cast<double>(max_contiguous_free) / stat.free) * 100.0;
    } else {
        stat.fragmentation_rate = 0.0;
    }

    return stat;
}

void MemoryManager::clear_all() {
    while (head_) {
        MemoryBlock* next = head_->next;
        delete head_;
        head_ = next;
    }
}

void MemoryManager::append_block(int addr, int size, bool free, int pid) {
    // 用于 load 恢复，直接在链表尾部添加
    if (!head_) {
        head_ = new MemoryBlock(addr, size, free, pid);
        return;
    }
    MemoryBlock* curr = head_;
    while (curr->next) curr = curr->next;
    auto* blk = new MemoryBlock(addr, size, free, pid);
    curr->next = blk;
    blk->prev = curr;
}

std::string MemoryManager::pgfault(int pid) const {
    std::ostringstream oss;
    oss << "[缺页中断] 进程 " << pid
        << " 访问的页面不在物理内存中！触发页面调入...";
    return oss.str();
}

bool MemoryManager::swap_out(int pid) {
    LockGuard lock(mutex_);

    bool found = false;
    MemoryBlock* curr = head_;
    while (curr) {
        if (!curr->is_free && curr->pid == pid) {
            curr->is_free = true;
            curr->pid = -1;
            found = true;
        }
        curr = curr->next;
    }

    if (found) {
        // 合并所有空闲块
        curr = head_;
        while (curr) {
            if (curr->is_free) {
                merge_adjacent_free(curr);
            }
            curr = curr->next;
        }
    }

    return found;
}
