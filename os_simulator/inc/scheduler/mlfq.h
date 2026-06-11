#ifndef MLFQ_H
#define MLFQ_H

#include <vector>
#include <string>
#include "common/types.h"
#include "common/sync.h"

/// 多级反馈队列 (Multi-Level Feedback Queue)
/// 三级队列 Q0/Q1/Q2，不同优先级范围和时间片
class MLFQ {
public:
    MLFQ();

    /// 将进程加入对应优先级的队列
    void enqueue(int pid, int priority);

    /// 从指定队列移除进程
    bool remove(int pid, int level);

    /// 从所有队列中移除进程
    bool remove(int pid);

    /// 检查队列是否为空
    bool is_empty(int level) const;

    /// 获取指定队列的大小
    int size(int level) const;

    /// 获取指定队列第一个进程（不移除）
    int peek(int level) const;

    /// 从指定队列取出第一个进程（移除）
    int dequeue(int level);

    /// 选择下一个要运行的进程（按优先级从高到低扫描）
    /// 返回 PID，-1 表示所有队列为空
    int pick_next() const;

    /// 从最高非空队列中取出进程
    int pick_and_dequeue();

    /// 获取进程在当前队列中的位置
    int get_position(int pid) const;

    /// 将指定队列向前轮转（队头移到队尾）
    void rotate(int level);

    /// 格式化队列状态为字符串
    std::string to_string() const;

    /// 获取所有队列中的所有 PID
    std::vector<int> get_all_pids() const;

    /// 获取指定队列的 PID 列表（持久化用）
    std::vector<int> get_queue_pids(int level) const;

    /// 获取互斥锁
    Mutex& mutex() { return mutex_; }

    /// 获取各队列时间片
    static int time_slice(int level) { return MLFQ_TIME_SLICE[level]; }

private:
    // 三级队列，每级存储 PID 列表
    std::vector<int> queues_[MLFQ_LEVELS];
    mutable Mutex mutex_;
};

#endif // MLFQ_H
