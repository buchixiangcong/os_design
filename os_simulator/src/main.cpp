#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <map>
#include <functional>
#include <sys/stat.h>
#include <cstdio>

#include "common/types.h"
#include "common/sync.h"
#include "account/account.h"
#include "memory/memory_manager.h"
#include "process/process_manager.h"
#include "scheduler/scheduler.h"
#include "ipc/message_queue.h"
#include "persistence/persistence.h"

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#endif

// ============================================================
// 全局状态
// ============================================================
static bool g_running = true;
static MemoryManager g_memory;
static ProcessManager g_process(g_memory);
static Scheduler g_scheduler(g_process, g_memory);
static AccountManager g_account;
static MessageQueue g_msg_queue;
static time_t g_last_save_mtime = 0;

// ============================================================
// 命令行解析（内联，替代原 parser.h/cpp）
// ============================================================
struct ParsedCommand {
    std::string cmd;
    std::vector<std::string> args;
};

ParsedCommand parse_line(const std::string& line) {
    ParsedCommand pc;
    std::istringstream iss(line);
    iss >> pc.cmd;
    std::transform(pc.cmd.begin(), pc.cmd.end(), pc.cmd.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::string arg;
    while (iss >> arg) pc.args.push_back(arg);
    return pc;
}

// ============================================================
// Banner / Help
// ============================================================
void print_help() {
    const char* help =
    "\n========== 可用命令 ==========\n"
    "  --- 账户管理 ---\n"
    "  register <user> <pass>  - 注册新用户\n"
    "  login <user> <pass>     - 用户登录\n"
    "  logout                  - 用户登出\n"
    "  --- 进程管理 ---\n"
    "  create_pcb <name> <pri> [mem] [ppid] - 创建进程\n"
    "  kill_pcb <pid>           - 终止进程\n"
    "  block_pcb <pid>          - 阻塞进程\n"
    "  wakeup_pcb <pid>         - 唤醒进程\n"
    "  show_pcb <pid>           - 查看进程详情\n"
    "  list_pcb                 - 列出所有进程\n"
    "  ptree                    - 进程树形结构\n"
    "  suspend <pid>            - 挂起进程\n"
    "  resume <pid>             - 恢复进程\n"
    "  renice <pid> <pri>       - 修改优先级\n"
    "  --- 调度器 ---\n"
    "  start_sched              - 启动自动调度\n"
    "  stop_sched               - 暂停调度\n"
    "  restart_sched            - 重启调度\n"
    "  step                     - 单步执行调度\n"
    "  --- 内存管理 ---\n"
    "  alloc <size>             - 分配内存\n"
    "  free_mem <addr>          - 释放内存\n"
    "  show_mem                 - 显示内存布局\n"
    "  compact                  - 内存碎片紧缩\n"
    "  mem_stat                 - 内存使用统计\n"
    "  set_alloc_algo <FF|BF|WF>- 切换分配算法\n"
    "  pgfault <pid>            - 模拟缺页中断\n"
    "  swap_out <pid>           - 进程内存换出\n"
    "  --- 持久化 ---\n"
    "  save                     - 保存系统状态\n"
    "  load                     - 加载系统状态\n"
    "  --- 可视化 ---\n"
    "  overview                 - 系统全景视图\n"
    "  --- 系统 ---\n"
    "  help                     - 显示此帮助\n"
    "  exit                     - 退出系统\n"
    "===========================================\n";
    std::cout << help << std::endl;
}

// ============================================================
// 持久化辅助
// ============================================================
void auto_save() {
    int next_pid = g_process.get_next_pid(), dummy = 0;
    Persistence::save(g_process, g_memory, g_scheduler, g_account, next_pid, dummy);
    struct stat st;
    if (stat(STATE_FILE, &st) == 0) g_last_save_mtime = st.st_mtime;
}

// ============================================================
// 命令处理函数（按模块分组）
// ============================================================
struct CmdCtx {
    const ParsedCommand& pc;
    bool& modified;   // 是否有状态变更（触发 auto_save）
};

void cmd_account(const CmdCtx& ctx) {
    auto& a = ctx.pc.args;
    if (ctx.pc.cmd == "register") {
        if (a.size() < 2) { std::cout << "[用法] register <用户名> <密码>" << std::endl; return; }
        std::string err = g_account.register_user(a[0], a[1]);
        std::cout << (err.empty() ? "[账户] '" + a[0] + "' 注册成功！\n" : err + "\n");
        ctx.modified = !err.empty();
    } else if (ctx.pc.cmd == "login") {
        if (a.size() < 2) { std::cout << "[用法] login <用户名> <密码>" << std::endl; return; }
        std::string err = g_account.login(a[0], a[1]);
        std::cout << (err.empty() ? "[账户] 欢迎，" + a[0] + "！\n" : err + "\n");
    } else if (ctx.pc.cmd == "logout") {
        std::cout << "[账户] " << (g_account.is_logged_in()
            ? "'" + g_account.current_user() + "' 已登出。" : "当前未登录。") << std::endl;
        g_account.logout();
    }
}

void cmd_process(const CmdCtx& ctx) {
    auto& a = ctx.pc.args;
    int pid;
    if (ctx.pc.cmd == "create_pcb") {
        if (a.size() < 2) { std::cout << "[用法] create_pcb <name> <pri> [mem_size] [ppid]" << std::endl; return; }
        int mem = a.size() >= 3 ? std::stoi(a[2]) : DEFAULT_PROC_MEM;
        int pp  = a.size() >= 4 ? std::stoi(a[3]) : 1;
        pid = g_process.create_pcb(a[0], std::stoi(a[1]), mem, pp);
        if (pid > 0) std::cout << "[进程] " << a[0] << " 创建成功 PID=" << pid << " 内存=" << mem << "KB" << std::endl;
        ctx.modified = true;
    } else if (ctx.pc.cmd == "kill_pcb") {
        if (a.empty()) return;
        g_process.kill_pcb(std::stoi(a[0])); ctx.modified = true;
    } else if (ctx.pc.cmd == "block_pcb") {
        pid = std::stoi(a[0]);
        std::cout << (g_process.block_pcb(pid) ? "[进程] " + std::to_string(pid) + " 已阻塞。\n" : "[错误] 阻塞失败。\n");
        ctx.modified = true;
    } else if (ctx.pc.cmd == "wakeup_pcb") {
        pid = std::stoi(a[0]);
        std::cout << (g_process.wakeup_pcb(pid) ? "[进程] " + std::to_string(pid) + " 已唤醒。\n" : "[错误] 唤醒失败。\n");
        ctx.modified = true;
    } else if (ctx.pc.cmd == "show_pcb") {
        std::cout << g_process.show_pcb(std::stoi(a[0])) << std::endl;
    } else if (ctx.pc.cmd == "list_pcb") {
        std::cout << g_process.list_pcb() << std::endl;
    } else if (ctx.pc.cmd == "ptree") {
        std::cout << g_process.ptree() << std::endl;
    } else if (ctx.pc.cmd == "suspend") {
        pid = std::stoi(a[0]);
        std::cout << (g_process.suspend(pid) ? "[进程] " + std::to_string(pid) + " 已挂起。\n" : "[错误] 挂起失败。\n");
        ctx.modified = true;
    } else if (ctx.pc.cmd == "resume") {
        pid = std::stoi(a[0]);
        std::cout << (g_process.resume(pid) ? "[进程] " + std::to_string(pid) + " 已恢复。\n" : "[错误] 恢复失败。\n");
        ctx.modified = true;
    } else if (ctx.pc.cmd == "renice") {
        if (a.size() < 2) return;
        std::cout << (g_process.renice(std::stoi(a[0]), std::stoi(a[1]))
            ? "[进程] 优先级已修改。\n" : "[错误] 修改失败。\n");
        ctx.modified = true;
    }
}

void cmd_memory(const CmdCtx& ctx) {
    auto& a = ctx.pc.args;
    if (ctx.pc.cmd == "alloc") {
        int sz = std::stoi(a[0]);
        int addr = g_memory.alloc(sz, -1);
        if (addr >= 0) std::cout << "[内存] 内核分配 " << sz << "KB @地址" << addr << "KB" << std::endl;
        else           std::cout << "[内存] 分配失败！内存不足。" << std::endl;
        ctx.modified = true;
    } else if (ctx.pc.cmd == "free_mem") {
        int addr = std::stoi(a[0]);
        std::cout << (g_memory.free_mem(addr) ? "[内存] 地址" + std::to_string(addr) + " 已释放。\n" : "[错误] 释放失败。\n");
        ctx.modified = true;
    } else if (ctx.pc.cmd == "show_mem") {
        std::cout << g_memory.show_mem() << std::endl;
    } else if (ctx.pc.cmd == "compact") {
        auto b = g_memory.mem_stat();
        g_memory.compact();
        auto after = g_memory.mem_stat();
        std::cout << "[内存] 碎片率: " << b.fragmentation_rate << "% -> " << after.fragmentation_rate << "%" << std::endl;
        ctx.modified = true;
    } else if (ctx.pc.cmd == "mem_stat") {
        auto s = g_memory.mem_stat();
        std::cout << "总:" << s.total << "K 已用:" << s.used << "K 空闲:" << s.free
                  << "K 碎片率:" << s.fragmentation_rate << "% 算法:" << algo_to_string(g_memory.get_algo()) << std::endl;
    } else if (ctx.pc.cmd == "set_alloc_algo") {
        std::string al = a[0];
        std::transform(al.begin(), al.end(), al.begin(), ::toupper);
        MemAlgo ma = (al == "BF" || al == "BEST_FIT") ? MemAlgo::BEST_FIT
                   : (al == "WF" || al == "WORST_FIT") ? MemAlgo::WORST_FIT : MemAlgo::FIRST_FIT;
        g_memory.set_alloc_algo(ma);
        std::cout << "[内存] 算法切换为: " << algo_to_string(ma) << std::endl;
    } else if (ctx.pc.cmd == "pgfault") {
        int pid = a.empty() ? -1 : std::stoi(a[0]);
        std::cout << g_memory.pgfault(pid) << std::endl;
    } else if (ctx.pc.cmd == "swap_out") {
        int pid = std::stoi(a[0]);
        std::cout << (g_memory.swap_out(pid) ? "[内存] 进程" + std::to_string(pid) + " 已换出。\n" : "[错误] 未找到该进程内存。\n");
        ctx.modified = true;
    }
}

void cmd_scheduler(const CmdCtx& ctx) {
    if (ctx.pc.cmd == "start_sched")      g_scheduler.start_sched();
    else if (ctx.pc.cmd == "stop_sched")  g_scheduler.stop_sched();
    else if (ctx.pc.cmd == "restart_sched") g_scheduler.restart_sched();
    else if (ctx.pc.cmd == "step")         std::cout << g_scheduler.step() << std::endl;
}

void cmd_persistence(const CmdCtx& ctx) {
    int next_pid = g_process.get_next_pid(), dummy = 0;
    if (ctx.pc.cmd == "save") {
        Persistence::save(g_process, g_memory, g_scheduler, g_account, next_pid, dummy);
    } else if (ctx.pc.cmd == "load") {
        Persistence::load(g_process, g_memory, g_scheduler, g_account, next_pid, dummy);
        g_process.set_next_pid(next_pid);
    }
}

void cmd_overview(const CmdCtx&) {
    std::cout << "\n=== System Overview ===\n"
              << g_process.ptree() << "\n"
              << g_memory.show_mem() << "\n"
              << g_scheduler.mlfq().to_string()
              << "(高优队列优先调度)" << std::endl;
}

// ============================================================
// 命令派发（后台线程）
// ============================================================
void dispatch(const ParsedCommand& pc) {
    if (pc.cmd.empty()) return;

    bool modified = false;
    CmdCtx ctx = { pc, modified };

    // 系统命令
    if (pc.cmd == "help")        { print_help(); return; }
    if (pc.cmd == "exit")        { std::cout << "[系统] 正在关闭...\n"; g_running = false; g_msg_queue.shutdown(); return; }

    // 按模块路由
    if (pc.cmd == "register" || pc.cmd == "login" || pc.cmd == "logout")
        cmd_account(ctx);
    else if (pc.cmd == "create_pcb" || pc.cmd == "kill_pcb" || pc.cmd == "block_pcb" ||
             pc.cmd == "wakeup_pcb" || pc.cmd == "show_pcb" || pc.cmd == "list_pcb" ||
             pc.cmd == "ptree" || pc.cmd == "suspend" || pc.cmd == "resume" || pc.cmd == "renice")
        cmd_process(ctx);
    else if (pc.cmd == "alloc" || pc.cmd == "free_mem" || pc.cmd == "show_mem" ||
             pc.cmd == "compact" || pc.cmd == "mem_stat" || pc.cmd == "set_alloc_algo" ||
             pc.cmd == "pgfault" || pc.cmd == "swap_out")
        cmd_memory(ctx);
    else if (pc.cmd == "start_sched" || pc.cmd == "stop_sched" ||
             pc.cmd == "restart_sched" || pc.cmd == "step")
        cmd_scheduler(ctx);
    else if (pc.cmd == "save" || pc.cmd == "load")
        cmd_persistence(ctx);
    else if (pc.cmd == "overview")
        cmd_overview(ctx);
    else
        std::cout << "[提示] 命令 '" << pc.cmd << "' 尚未实现。" << std::endl;

    // 状态变更后自动持久化（多实例共享）
    if (modified) auto_save();
}

// ============================================================
// 后台工作线程
// ============================================================
#ifdef _WIN32
unsigned __stdcall backend_worker(void*) {
    std::cout << "[系统] 后台线程启动 (TID=" << GetCurrentThreadId() << ")\n";
    while (g_running) {
        Message msg = g_msg_queue.pop();
        if (msg.raw.empty() && msg.type == MessageType::CMD_UNKNOWN) break;
        ParsedCommand pc = parse_line(msg.raw);
        dispatch(pc);
    }
    std::cout << "[系统] 后台线程退出。" << std::endl;
    return 0;
}
#endif

// ============================================================
// 文件监听线程（多实例共享）
// ============================================================
#ifdef _WIN32
unsigned __stdcall file_watcher(void*) {
    while (g_running) {
        Sleep(500);
        struct stat st;
        if (stat(STATE_FILE, &st) != 0 || st.st_mtime <= g_last_save_mtime) continue;
        std::cout << "\n[共享] 外部变更，自动同步..." << std::endl;
        int next_pid, dummy;
        Persistence::load(g_process, g_memory, g_scheduler, g_account, next_pid, dummy);
        g_process.set_next_pid(next_pid);
        g_last_save_mtime = st.st_mtime;
    }
    return 0;
}
#endif

// ============================================================
// 主函数
// ============================================================
int main() {
    // 设置控制台为 UTF-8 编码（修复中文/特殊字符乱码）
    #ifdef _WIN32
    system("chcp 65001 > nul");
    #endif

    std::cout << R"(
==================================================
      操作系统核心模拟器 - OS Simulator v1.0
      北京林业大学 信息学院 操作系统A课程设计
      多线程 | MLFQ调度 | 动态分区 | 持久化
==================================================
输入 'help' 查看命令，'exit' 退出系统
)" << std::endl;

    // 启动时状态恢复
    if (Persistence::file_exists()) {
        int next_pid, dummy;
        Persistence::load(g_process, g_memory, g_scheduler, g_account, next_pid, dummy);
        g_process.set_next_pid(next_pid);
        struct stat st;
        if (stat(STATE_FILE, &st) == 0) g_last_save_mtime = st.st_mtime;
        std::cout << "[系统] 从持久化文件恢复状态。" << std::endl;
    } else {
        g_process.init();
    }

    // 启动后台线程（命令处理器）
    void* h_backend = nullptr;
    void* h_watcher = nullptr;
    #ifdef _WIN32
    h_backend = reinterpret_cast<void*>(
        _beginthreadex(nullptr, 0, backend_worker, nullptr, 0, nullptr));
    h_watcher = reinterpret_cast<void*>(
        _beginthreadex(nullptr, 0, file_watcher, nullptr, 0, nullptr));
    std::cout << "[系统] 前台线程 + 后台线程 + 文件监听 就绪\n" << std::endl;
    #endif

    // 前台交互循环
    std::string line;
    while (g_running) {
        std::cout << (g_account.is_logged_in()
            ? "[" + g_account.current_user() + "@os]> "
            : "[guest@os]> ") << std::flush;

        if (!std::getline(std::cin, line)) break;

        ParsedCommand pc = parse_line(line);
        if (pc.cmd.empty()) continue;

        // 查找消息类型并投递到队列
        MessageType t = MessageType::CMD_UNKNOWN;
        for (int i = 0; i <= static_cast<int>(MessageType::CMD_EXIT); ++i) {
            if (msgtype_to_string(static_cast<MessageType>(i)) == pc.cmd)
                { t = static_cast<MessageType>(i); break; }
        }

        g_msg_queue.push(Message{t, pc.args, line});

        if (pc.cmd == "exit") {
            Sleep(300);  // 等后台处理完
            g_running = false;
            g_msg_queue.shutdown();
            break;
        }
    }

    #ifdef _WIN32
    if (h_backend) { WaitForSingleObject(h_backend, 3000); CloseHandle(h_backend); }
    if (h_watcher) { WaitForSingleObject(h_watcher, 1000); CloseHandle(h_watcher); }
    #endif

    std::cout << "[系统] Bye!" << std::endl;
    return 0;
}
