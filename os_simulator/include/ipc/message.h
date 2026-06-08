#ifndef IPC_MESSAGE_H
#define IPC_MESSAGE_H

#include <string>
#include <vector>
#include "common/types.h"

/// 线程间通信的消息结构
struct Message {
    MessageType type;              // 消息类型（对应命令）
    std::vector<std::string> args; // 命令参数
    std::string raw;               // 原始输入行

    Message() : type(MessageType::CMD_UNKNOWN) {}
    Message(MessageType t, const std::vector<std::string>& a, const std::string& r)
        : type(t), args(a), raw(r) {}
};

#endif // IPC_MESSAGE_H
