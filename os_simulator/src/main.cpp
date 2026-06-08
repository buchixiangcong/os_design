#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <sys/stat.h>

#include "common/types.h"
#include "memory/memory_manager.h"
#include "process/process_manager.h"
#include "scheduler/scheduler.h"
#include "cli/parser.h"
#include "account/account.h"
#include "ipc/message_queue.h"
#include "persistence/persistence.h"
// #include "cli/display.h"

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#endif

// ============================================================
// 全局系统状态
// ============================================================

static bool g_running = true;
static MemoryManager g_memory;
static ProcessManager g_process(g_memory);
static Scheduler g_scheduler(g_process, g_memory);
static AccountManager g_account;
static MessageQueue g_msg_queue;  // 前后台线程通信的消息队列
// ========== 多实例共享 (阶段8) ==========
static time_t g_last_save_mtime = 0;   // 上次 save 后的文件修改时间
static bool g_multi_instance = true;    // 是否启用多实例模式

/// 自动保存（供后台线程调用，不经过消息队列）
void auto_save() {
    int next_pid = g_process.get_next_pid();
    std::string err = Persistence::save(
        g_process, g_memory, g_scheduler, g_account,
        next_pid, 0);
    if (err.empty()) {
        // 记录保存后的文件时间戳
        struct stat st;
        if (stat(STATE_FILE, &st) == 0) {
            g_last_save_mtime = st.st_mtime;
        }
    }
}

/// 文件监听线程：定期检查 state.bin 是否被其他实例修改
#ifdef _WIN32
unsigned __stdcall file_watcher(void* /*arg*/) {
    std::cout << "[共享] 文件监听线程已启动 (多实例模式)" << std::endl;
    while (g_running) {
        Sleep(FILE_CHECK_INTERVAL_MS);

        if (!Persistence::file_exists()) continue;

        struct stat st;
        if (stat(STATE_FILE, &st) != 0) continue;

        // 如果文件修改时间比我们上次保存的时间新，说明被其他实例修改了
        if (st.st_mtime > g_last_save_mtime) {
            std::cout << "\n[共享] 检测到外部状态变更，自动同步..." << std::endl;
            int next_pid;
            int dummy;
            std::string err = Persistence::load(
                g_process, g_memory, g_scheduler, g_account,
                next_pid, dummy);
            if (err.empty()) {
                g_process.set_next_pid(next_pid);
                g_last_save_mtime = st.st_mtime;
                std::cout << "[共享] 同步完成!" << std::endl;
            }
        }
    }
    return 0;
}
#endif

// ============================================================
// Banner 和 Help
// ============================================================

void print_banner() {
    std::cout << R"(
╔══════════════════════════════════════════════════╗
║      操作系统核心模拟器 - OS Simulator v1.0       ║
║      北京林业大学 信息学院 操作系统A课程设计       ║
║      C++ 实现 | MLFQ调度 | 动态分区 | 持久化      ║
╚══════════════════════════════════════════════════╝
)";
    std::cout << " 输入 'help' 查看可用命令，'exit' 退出系统\n" << std::endl;
}

void print_help() {
    std::cout << "\n========== 可用命令 ==========\n";
    std::cout << "  --- 账户管理 ---\n";
    std::cout << "  register <user> <pass>  - 注册新用户\n";
    std::cout << "  login <user> <pass>     - 用户登录\n";
    std::cout << "  logout                  - 用户登出\n";
    std::cout << "  --- 进程管理 ---\n";
    std::cout << "  create_pcb <name> <pri> [mem] [ppid] - 创建进程\n";
    std::cout << "  kill_pcb <pid>          - 终止进程\n";
    std::cout << "  block_pcb <pid>         - 阻塞进程\n";
    std::cout << "  wakeup_pcb <pid>        - 唤醒进程\n";
    std::cout << "  show_pcb <pid>          - 查看进程详情\n";
    std::cout << "  list_pcb                - 列出所有进程\n";
    std::cout << "  ptree                   - 进程树形结构\n";
    std::cout << "  suspend <pid>           - 挂起进程\n";
    std::cout << "  resume <pid>            - 恢复进程\n";
    std::cout << "  renice <pid> <pri>      - 修改优先级\n";
    std::cout << "  --- 调度器 ---\n";
    std::cout << "  start_sched             - 启动自动调度\n";
    std::cout << "  stop_sched              - 暂停调度\n";
    std::cout << "  restart_sched           - 重启调度\n";
    std::cout << "  step                    - 单步执行调度\n";
    std::cout << "  --- 内存管理 ---\n";
    std::cout << "  alloc <size>            - 分配内存\n";
    std::cout << "  free_mem <addr>         - 释放内存\n";
    std::cout << "  show_mem                - 显示内存布局\n";
    std::cout << "  compact                 - 内存碎片紧缩\n";
    std::cout << "  mem_stat                - 内存使用统计\n";
    std::cout << "  set_alloc_algo <algo>   - 切换分配算法(FF/BF/WF)\n";
    std::cout << "  pgfault                 - 模拟缺页中断\n";
    std::cout << "  swap_out <pid>          - 进程内存换出\n";
    std::cout << "  --- 持久化 ---\n";
    std::cout << "  save                    - 保存系统状态\n";
    std::cout << "  load                    - 加载系统状态\n";
    std::cout << "  --- 可视化 ---\n";
    std::cout << "  overview                - 系统全景视图\n";
    std::cout << "  --- 系统 ---\n";
    std::cout << "  help                    - 显示此帮助\n";
    std::cout << "  exit                    - 退出系统\n";
    std::cout << "===============================\n" << std::endl;
}

// ============================================================
// 命令分发（后台线程执行）
// 所有对共享数据的操作都在这里完成
// ============================================================

void dispatch_command(const ParsedCommand& pc) {
    const std::string& cmd = pc.cmd;

    if (cmd.empty()) return;

    // ========== 系统命令 ==========
    if (cmd == "help") {
        print_help();
    }
    else if (cmd == "exit") {
        std::cout << "[系统] 正在关闭..." << std::endl;
        g_running = false;
        g_msg_queue.shutdown();  // 唤醒后台线程使其退出
    }

    // ========== 账户管理 ==========
    else if (cmd == "register") {
        if (pc.args.size() < 2) {
            std::cout << "[错误] 用法: register <用户名> <密码>" << std::endl;
        } else {
            std::string err = g_account.register_user(pc.args[0], pc.args[1]);
            if (err.empty())
                std::cout << "[账户] 用户 '" << pc.args[0] << "' 注册成功！" << std::endl;
            else
                std::cout << err << std::endl;
        }
    }
    else if (cmd == "login") {
        if (pc.args.size() < 2) {
            std::cout << "[错误] 用法: login <用户名> <密码>" << std::endl;
        } else {
            std::string err = g_account.login(pc.args[0], pc.args[1]);
            if (err.empty())
                std::cout << "[账户] 欢迎回来，" << pc.args[0] << "！" << std::endl;
            else
                std::cout << err << std::endl;
        }
    }
    else if (cmd == "logout") {
        if (g_account.is_logged_in()) {
            std::string user = g_account.current_user();
            g_account.logout();
            std::cout << "[账户] 用户 '" << user << "' 已登出。" << std::endl;
        } else {
            std::cout << "[账户] 当前未登录。" << std::endl;
        }
    }

    // ========== 内存管理 ==========
    else if (cmd == "alloc") {
        if (pc.args.empty()) {
            std::cout << "[错误] 用法: alloc <size>" << std::endl;
        } else {
            int size = std::stoi(pc.args[0]);
            int addr = g_memory.alloc(size, -1);  // pid=-1 表示内核预留
            if (addr >= 0)
                std::cout << "[内存] 内核分配成功! 起始地址: " << addr << "KB, 大小: " << size << "KB" << std::endl;
            else
                std::cout << "[内存] 分配失败! 内存不足。" << std::endl;
        }
    }
    else if (cmd == "free_mem") {
        if (pc.args.empty()) {
            std::cout << "[错误] 用法: free_mem <addr>" << std::endl;
        } else {
            int addr = std::stoi(pc.args[0]);
            if (g_memory.free_mem(addr))
                std::cout << "[内存] 地址 " << addr << "KB 处的内存已释放。" << std::endl;
            else
                std::cout << "[内存] 释放失败!" << std::endl;
        }
    }
    else if (cmd == "show_mem") {
        std::cout << g_memory.show_mem() << std::endl;
    }
    else if (cmd == "compact") {
        auto before = g_memory.mem_stat();
        g_memory.compact();
        auto after = g_memory.mem_stat();
        std::cout << "[内存] 碎片紧缩完成! 碎片率: "
                  << before.fragmentation_rate << "% -> " << after.fragmentation_rate << "%" << std::endl;
    }
    else if (cmd == "mem_stat") {
        auto stat = g_memory.mem_stat();
        std::cout << "\n========== 内存统计 ==========" << std::endl;
        std::cout << "  总内存: " << stat.total << " KB  |  已用: " << stat.used
                  << " KB  |  空闲: " << stat.free << " KB" << std::endl;
        std::cout << "  碎片率: " << stat.fragmentation_rate << " %  |  算法: "
                  << algo_to_string(g_memory.get_algo()) << std::endl;
        std::cout << "==============================\n" << std::endl;
    }
    else if (cmd == "set_alloc_algo") {
        if (pc.args.empty()) {
            std::cout << "[错误] 用法: set_alloc_algo <FF|BF|WF>" << std::endl;
            return;
        }
        std::string algo = pc.args[0];
        std::transform(algo.begin(), algo.end(), algo.begin(), ::toupper);
        if (algo == "FF" || algo == "FIRST_FIT") g_memory.set_alloc_algo(MemAlgo::FIRST_FIT);
        else if (algo == "BF" || algo == "BEST_FIT") g_memory.set_alloc_algo(MemAlgo::BEST_FIT);
        else if (algo == "WF" || algo == "WORST_FIT") g_memory.set_alloc_algo(MemAlgo::WORST_FIT);
        else { std::cout << "[错误] 未知算法: " << algo << "，支持: FF / BF / WF" << std::endl; return; }
        std::cout << "[内存] 分配算法已切换为: " << algo_to_string(g_memory.get_algo()) << std::endl;
    }
    else if (cmd == "pgfault") {
        int pid = pc.args.empty() ? -1 : std::stoi(pc.args[0]);
        std::cout << g_memory.pgfault(pid) << std::endl;
    }
    else if (cmd == "swap_out") {
        if (pc.args.empty()) { std::cout << "[错误] 用法: swap_out <pid>" << std::endl; return; }
        int pid = std::stoi(pc.args[0]);
        if (g_memory.swap_out(pid))
            std::cout << "[内存] 进程 " << pid << " 的内存已换出。" << std::endl;
        else
            std::cout << "[内存] 换出失败! 未找到进程 " << pid << " 的内存块。" << std::endl;
    }

    // ========== 进程管理 ==========
    else if (cmd == "create_pcb") {
        if (pc.args.size() < 2) {
            std::cout << "[错误] 用法: create_pcb <name> <priority> [mem_size] [ppid]" << std::endl;
        } else {
            std::string name = pc.args[0];
            int priority = std::stoi(pc.args[1]);
            int mem_size = pc.args.size() >= 3 ? std::stoi(pc.args[2]) : DEFAULT_PROC_MEM;
            int ppid     = pc.args.size() >= 4 ? std::stoi(pc.args[3]) : 1;
            int pid = g_process.create_pcb(name, priority, mem_size, ppid);
            if (pid > 0)
                std::cout << "[进程] 创建成功! PID=" << pid << " 内存=" << mem_size << "KB" << std::endl;
        }
    }
    else if (cmd == "kill_pcb") {
        if (pc.args.empty()) { std::cout << "[错误] 用法: kill_pcb <pid>" << std::endl; return; }
        g_process.kill_pcb(std::stoi(pc.args[0]));
    }
    else if (cmd == "block_pcb") {
        if (pc.args.empty()) { std::cout << "[错误] 用法: block_pcb <pid>" << std::endl; return; }
        int pid = std::stoi(pc.args[0]);
        if (g_process.block_pcb(pid))
            std::cout << "[进程] 进程 " << pid << " 已阻塞。" << std::endl;
        else
            std::cout << "[错误] 阻塞失败。" << std::endl;
    }
    else if (cmd == "wakeup_pcb") {
        if (pc.args.empty()) { std::cout << "[错误] 用法: wakeup_pcb <pid>" << std::endl; return; }
        int pid = std::stoi(pc.args[0]);
        if (g_process.wakeup_pcb(pid))
            std::cout << "[进程] 进程 " << pid << " 已唤醒。" << std::endl;
        else
            std::cout << "[错误] 唤醒失败。" << std::endl;
    }
    else if (cmd == "show_pcb") {
        if (pc.args.empty()) { std::cout << "[错误] 用法: show_pcb <pid>" << std::endl; return; }
        std::cout << g_process.show_pcb(std::stoi(pc.args[0])) << std::endl;
    }
    else if (cmd == "list_pcb") {
        std::cout << g_process.list_pcb() << std::endl;
    }
    else if (cmd == "ptree") {
        std::cout << g_process.ptree() << std::endl;
    }
    else if (cmd == "suspend") {
        if (pc.args.empty()) { std::cout << "[错误] 用法: suspend <pid>" << std::endl; return; }
        int pid = std::stoi(pc.args[0]);
        if (g_process.suspend(pid))
            std::cout << "[进程] 进程 " << pid << " 已挂起。" << std::endl;
        else
            std::cout << "[错误] 挂起失败。" << std::endl;
    }
    else if (cmd == "resume") {
        if (pc.args.empty()) { std::cout << "[错误] 用法: resume <pid>" << std::endl; return; }
        int pid = std::stoi(pc.args[0]);
        if (g_process.resume(pid))
            std::cout << "[进程] 进程 " << pid << " 已恢复。" << std::endl;
        else
            std::cout << "[错误] 恢复失败。" << std::endl;
    }
    else if (cmd == "renice") {
        if (pc.args.size() < 2) { std::cout << "[错误] 用法: renice <pid> <new_priority>" << std::endl; return; }
        int pid = std::stoi(pc.args[0]);
        int prio = std::stoi(pc.args[1]);
        if (g_process.renice(pid, prio))
            std::cout << "[进程] 进程 " << pid << " 优先级已修改为 " << prio << "。" << std::endl;
        else
            std::cout << "[错误] 修改优先级失败。" << std::endl;
    }

    // ========== 调度器 ==========
    else if (cmd == "start_sched") {
        g_scheduler.start_sched();
    }
    else if (cmd == "stop_sched") {
        g_scheduler.stop_sched();
    }
    else if (cmd == "restart_sched") {
        g_scheduler.restart_sched();
    }
    else if (cmd == "step") {
        std::cout << g_scheduler.step() << std::endl;
    }

    // ========== 持久化 ==========
    else if (cmd == "save") {
        int next_pid = g_process.get_next_pid();
        std::string err = Persistence::save(
            g_process, g_memory, g_scheduler, g_account,
            next_pid, 0);
        if (!err.empty()) std::cout << err << std::endl;
    }
    else if (cmd == "load") {
        int next_pid, dummy;
        std::string err = Persistence::load(
            g_process, g_memory, g_scheduler, g_account,
            next_pid, dummy);
        if (err.empty()) {
            g_process.set_next_pid(next_pid);
        } else {
            std::cout << err << std::endl;
        }
    }

    // ========== 可视化 ==========
    else if (cmd == "overview") {
        std::cout << "\n╔════════════════════════════════════════════╗" << std::endl;
        std::cout << "║         System Overview — 系统全景         ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════╝" << std::endl;

        // 1. 进程树
        std::cout << "\n--- Process Tree ---" << std::endl;
        std::cout << g_process.ptree() << std::endl;

        // 2. 内存布局
        std::cout << "\n--- Memory Map ---" << std::endl;
        std::cout << g_memory.show_mem() << std::endl;

        // 3. MLFQ 队列状态
        std::cout << "\n--- MLFQ Status ---" << std::endl;
        std::cout << g_scheduler.mlfq().to_string() << std::endl;
        std::cout << "说明：高优队列无等待时，低优队列进程才会获得调度机会" << std::endl;

        std::cout << "\n════════════════════════════════════════════\n" << std::endl;
    }

    // ========== 未实现 ==========
    else {
        std::cout << "[提示] 命令 '" << cmd << "' 尚未实现。" << std::endl;
    }

    // ★ 多实例共享：状态修改后自动持久化
    if (g_multi_instance && cmd != "help" && cmd != "load" && cmd != "exit") {
        auto_save();
    }
}

// ============================================================
// 后台工作线程（消费者）
// 不断从消息队列取出命令并执行
// ============================================================

#ifdef _WIN32
unsigned __stdcall backend_worker(void* /*arg*/) {
    std::cout << "[系统] 后台维护线程已启动 (TID=" << GetCurrentThreadId() << ")\n" << std::endl;

    while (g_running) {
        Message msg = g_msg_queue.pop();
        if (msg.type == MessageType::CMD_UNKNOWN && msg.raw.empty()) {
            break; // 队列已关闭
        }

        ParsedCommand pc;
        // 如果消息类型未知（如 help 等不在 MessageType 枚举中的命令），
        // 则从原始输入重新解析，保留真正的命令名
        if (msg.type == MessageType::CMD_UNKNOWN && !msg.raw.empty()) {
            pc = CommandParser::parse(msg.raw);
        } else {
            pc.cmd = msgtype_to_string(msg.type);
            pc.args = msg.args;
            pc.raw = msg.raw;
        }

        dispatch_command(pc);
    }

    std::cout << "[系统] 后台维护线程已退出。" << std::endl;
    return 0;
}
#endif

// ============================================================
// 主函数
// ============================================================

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // 将控制台编码设为 UTF-8，避免中文乱码
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
    print_banner();

    // 检查是否有持久化文件，有则自动加载
    if (Persistence::file_exists()) {
        std::cout << "[系统] 检测到持久化文件，正在恢复状态..." << std::endl;
        int next_pid, dummy;
        std::string err = Persistence::load(
            g_process, g_memory, g_scheduler, g_account,
            next_pid, dummy);
        if (err.empty()) {
            g_process.set_next_pid(next_pid);
            std::cout << "[系统] 状态恢复成功!" << std::endl;
        } else {
            std::cout << "[系统] 加载失败: " << err << "，以初始状态启动。" << std::endl;
            g_process.init();
        }
    } else {
        // 正常启动：初始化 init 进程
        g_process.init();
    }

#ifdef _WIN32
    // 启动后台工作线程
    HANDLE h_backend = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0, backend_worker, nullptr, 0, nullptr));
    if (!h_backend) {
        std::cerr << "[错误] 无法创建后台工作线程！" << std::endl;
        return 1;
    }
    std::cout << "[系统] 多线程架构: 前台交互线程 + 后台维护线程\n" << std::endl;

    // 启动文件监听线程（多实例共享）
    HANDLE h_watcher = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0, file_watcher, nullptr, 0, nullptr));
    if (!h_watcher) {
        std::cout << "[警告] 无法启动文件监听线程，多实例共享不可用。" << std::endl;
        g_multi_instance = false;
    }
#else
    std::cout << "[系统] 单线程模式 (无原生线程支持)\n" << std::endl;
#endif

    // ========== 前台交互循环（生产者） ==========
    std::string line;
    while (g_running) {
        // 打印提示符
        if (g_account.is_logged_in()) {
            std::cout << "[" << g_account.current_user() << "@os]> ";
        } else {
            std::cout << "[guest@os]> ";
        }
        std::cout.flush();

        if (!std::getline(std::cin, line)) {
            break; // EOF
        }

        // 解析命令
        ParsedCommand pc = CommandParser::parse(line);
        if (pc.empty()) continue;

        // exit 命令也通过消息队列发送，确保前面的命令先处理完
        if (pc.cmd == "exit") {
            // 先投递 exit 消息
            Message msg(MessageType::CMD_EXIT, {}, line);
            g_msg_queue.push(msg);
            // 等待后台线程处理完所有消息
            #ifdef _WIN32
            Sleep(500);  // 给后台线程时间处理
            #endif
            g_running = false;
            g_msg_queue.shutdown();
            break;
        }

        // 其他命令封装为消息，投递到队列
        MessageType msg_type = MessageType::CMD_UNKNOWN;
        // 匹配消息类型
        for (int i = 0; i <= static_cast<int>(MessageType::CMD_EXIT); ++i) {
            MessageType t = static_cast<MessageType>(i);
            if (msgtype_to_string(t) == pc.cmd) {
                msg_type = t;
                break;
            }
        }

        Message msg(msg_type, pc.args, pc.raw);
        g_msg_queue.push(msg);
    }

#ifdef _WIN32
    // 等待后台线程结束
    g_msg_queue.shutdown();
    WaitForSingleObject(h_backend, 3000);
    CloseHandle(h_backend);
    if (h_watcher) {
        WaitForSingleObject(h_watcher, 1000);
        CloseHandle(h_watcher);
    }
#endif

    std::cout << "[系统] 再见！" << std::endl;
    return 0;
}
