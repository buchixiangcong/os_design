#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>

/// 解析后的命令结构
struct ParsedCommand {
    std::string cmd;               // 命令名（小写）
    std::vector<std::string> args; // 参数列表
    std::string raw;               // 原始输入

    bool empty() const { return cmd.empty(); }
};

/// 命令行解析器
class CommandParser {
public:
    /// 解析一行输入为命令结构
    static ParsedCommand parse(const std::string& line);

    /// 从命令行参数字符串分割参数
    static std::vector<std::string> split_args(const std::string& args_str);
};

#endif // PARSER_H
