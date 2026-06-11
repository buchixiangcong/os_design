#ifndef IPC_MESSAGE_QUEUE_H
#define IPC_MESSAGE_QUEUE_H

#include <queue>
#include <string>
#include <vector>
#include "common/types.h"
#include "common/sync.h"

// ============================================================
// 线程间通信的消息结构
// ============================================================
struct Message {
    MessageType type = MessageType::CMD_UNKNOWN;
    std::vector<std::string> args;
    std::string raw;
};

// ============================================================
// 线程安全的消息队列（生产者-消费者模式）
// ============================================================
class MessageQueue {
public:
    MessageQueue() : shutdown_(false) {}

    void push(const Message& msg) {
        LockGuard lock(mutex_);
        queue_.push(msg);
    }

    Message pop() {
        while (true) {
            { LockGuard lock(mutex_);
              if (!queue_.empty()) { Message m = queue_.front(); queue_.pop(); return m; }
              if (shutdown_) return Message(); }
            Sleep(10);  // 避免忙等
        }
    }

    void shutdown() { LockGuard lock(mutex_); shutdown_ = true; }

private:
    std::queue<Message> queue_;
    mutable Mutex mutex_;
    bool shutdown_;
};

#endif
