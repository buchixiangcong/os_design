#include "cli/parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

ParsedCommand CommandParser::parse(const std::string& line) {
    ParsedCommand result;
    result.raw = line;

    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd.empty()) {
        return result;
    }

    // 转小写
    std::transform(cmd.begin(), cmd.end(), cmd.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    result.cmd = cmd;

    // 提取剩余参数
    std::string arg;
    while (iss >> arg) {
        result.args.push_back(arg);
    }

    return result;
}

std::vector<std::string> CommandParser::split_args(const std::string& args_str) {
    std::vector<std::string> result;
    std::istringstream iss(args_str);
    std::string arg;
    while (iss >> arg) {
        result.push_back(arg);
    }
    return result;
}
