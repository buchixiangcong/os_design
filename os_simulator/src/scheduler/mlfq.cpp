#include "scheduler/mlfq.h"
#include <sstream>
#include <algorithm>

MLFQ::MLFQ() {
}

void MLFQ::enqueue(int pid, int priority) {
    LockGuard lock(mutex_);
    int level = get_queue_level(priority);
    // 避免重复入队
    for (int i = 0; i < MLFQ_LEVELS; ++i) {
        auto& q = queues_[i];
        auto it = std::find(q.begin(), q.end(), pid);
        if (it != q.end()) {
            q.erase(it);
        }
    }
    queues_[level].push_back(pid);
}

bool MLFQ::remove(int pid, int level) {
    LockGuard lock(mutex_);
    if (level < 0 || level >= MLFQ_LEVELS) return false;
    auto& q = queues_[level];
    auto it = std::find(q.begin(), q.end(), pid);
    if (it != q.end()) {
        q.erase(it);
        return true;
    }
    return false;
}

bool MLFQ::remove(int pid) {
    LockGuard lock(mutex_);
    for (int i = 0; i < MLFQ_LEVELS; ++i) {
        auto& q = queues_[i];
        auto it = std::find(q.begin(), q.end(), pid);
        if (it != q.end()) {
            q.erase(it);
            return true;
        }
    }
    return false;
}

bool MLFQ::is_empty(int level) const {
    LockGuard lock(mutex_);
    if (level < 0 || level >= MLFQ_LEVELS) return true;
    return queues_[level].empty();
}

int MLFQ::size(int level) const {
    LockGuard lock(mutex_);
    if (level < 0 || level >= MLFQ_LEVELS) return 0;
    return static_cast<int>(queues_[level].size());
}

int MLFQ::peek(int level) const {
    LockGuard lock(mutex_);
    if (level < 0 || level >= MLFQ_LEVELS || queues_[level].empty())
        return -1;
    return queues_[level].front();
}

int MLFQ::dequeue(int level) {
    LockGuard lock(mutex_);
    if (level < 0 || level >= MLFQ_LEVELS || queues_[level].empty())
        return -1;
    int pid = queues_[level].front();
    queues_[level].erase(queues_[level].begin());
    return pid;
}

int MLFQ::pick_next() const {
    LockGuard lock(mutex_);
    // 从高优先级到低优先级扫描
    for (int i = 0; i < MLFQ_LEVELS; ++i) {
        if (!queues_[i].empty()) {
            return queues_[i].front();
        }
    }
    return -1;
}

int MLFQ::pick_and_dequeue() {
    LockGuard lock(mutex_);
    for (int i = 0; i < MLFQ_LEVELS; ++i) {
        if (!queues_[i].empty()) {
            int pid = queues_[i].front();
            queues_[i].erase(queues_[i].begin());
            return pid;
        }
    }
    return -1;
}

int MLFQ::get_position(int pid) const {
    LockGuard lock(mutex_);
    for (int i = 0; i < MLFQ_LEVELS; ++i) {
        const auto& q = queues_[i];
        for (size_t j = 0; j < q.size(); ++j) {
            if (q[j] == pid) return static_cast<int>(j);
        }
    }
    return -1;
}

void MLFQ::rotate(int level) {
    LockGuard lock(mutex_);
    if (level < 0 || level >= MLFQ_LEVELS || queues_[level].size() <= 1)
        return;
    int front = queues_[level].front();
    queues_[level].erase(queues_[level].begin());
    queues_[level].push_back(front);
}

std::string MLFQ::to_string() const {
    LockGuard lock(mutex_);
    std::ostringstream oss;
    for (int i = 0; i < MLFQ_LEVELS; ++i) {
        oss << "  Q" << i << " (prio " << MLFQ_PRIO_MIN[i]
            << "-" << MLFQ_PRIO_MAX[i]
            << ", 时间片=" << MLFQ_TIME_SLICE[i] << "s): ";
        if (queues_[i].empty()) {
            oss << "[空]";
        } else {
            for (size_t j = 0; j < queues_[i].size(); ++j) {
                if (j > 0) oss << " -> ";
                oss << "PID:" << queues_[i][j];
            }
        }
        oss << "\n";
    }
    return oss.str();
}

std::vector<int> MLFQ::get_queue_pids(int level) const {
    LockGuard lock(mutex_);
    if (level < 0 || level >= MLFQ_LEVELS) return {};
    return queues_[level];
}

std::vector<int> MLFQ::get_all_pids() const {
    LockGuard lock(mutex_);
    std::vector<int> result;
    for (int i = 0; i < MLFQ_LEVELS; ++i) {
        for (int pid : queues_[i]) {
            result.push_back(pid);
        }
    }
    return result;
}
