#include "ipc/message_queue.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

MessageQueue::MessageQueue() : shutdown_(false) {
}

MessageQueue::~MessageQueue() {
    shutdown();
}

void MessageQueue::push(const Message& msg) {
    LockGuard lock(mutex_);
    queue_.push(msg);
}

Message MessageQueue::pop() {
    while (true) {
        {
            LockGuard lock(mutex_);
            if (!queue_.empty()) {
                Message msg = queue_.front();
                queue_.pop();
                return msg;
            }
            if (shutdown_) {
                return Message();  // 空消息表示关闭
            }
        }
        // 队列空时短暂休眠，避免忙等
        #ifdef _WIN32
        Sleep(10);
        #else
        usleep(10000);
        #endif
    }
}

Message MessageQueue::try_pop() {
    LockGuard lock(mutex_);
    if (queue_.empty()) {
        return Message();
    }
    Message msg = queue_.front();
    queue_.pop();
    return msg;
}

bool MessageQueue::empty() const {
    LockGuard lock(mutex_);
    return queue_.empty();
}

void MessageQueue::shutdown() {
    LockGuard lock(mutex_);
    shutdown_ = true;
}
