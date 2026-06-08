#ifndef IPC_MESSAGE_QUEUE_H
#define IPC_MESSAGE_QUEUE_H

#include <queue>
#include "ipc/message.h"
#include "common/sync.h"

#ifdef _WIN32
#include <windows.h>
#endif

/// 线程安全的消息队列（生产者-消费者模式）
/// 使用轮询方式避免 CONDITION_VARIABLE 兼容性问题
class MessageQueue {
public:
    MessageQueue();
    ~MessageQueue();

    // 禁止拷贝
    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;

    /// 向队列添加消息（生产者 — 前台线程）
    void push(const Message& msg);

    /// 从队列取出消息（消费者 — 后台线程，阻塞等待）
    Message pop();

    /// 尝试非阻塞取消息，空时返回空消息
    Message try_pop();

    /// 队列是否为空
    bool empty() const;

    /// 通知关闭，使 pop() 立即返回空消息
    void shutdown();

private:
    std::queue<Message> queue_;
    mutable Mutex mutex_;
    bool shutdown_;
};

#endif // IPC_MESSAGE_QUEUE_H
