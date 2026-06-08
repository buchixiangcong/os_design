#include "persistence/persistence.h"
#include "memory/memory_manager.h"
#include "process/process_manager.h"
#include "scheduler/scheduler.h"
#include "account/account.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <io.h>        // _fileno, _get_osfhandle
#endif

// C++14 要求 constexpr static 成员有外部定义
constexpr uint32_t Persistence::MAGIC;
constexpr uint32_t Persistence::VERSION;

// ============================================================
// 字符串读写
// ============================================================

bool Persistence::write_string(FILE* f, const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    if (fwrite(&len, sizeof(len), 1, f) != 1) return false;
    if (len > 0) return fwrite(s.c_str(), 1, len, f) == len;
    return true;
}

bool Persistence::read_string(FILE* f, std::string& s) {
    uint32_t len;
    if (fread(&len, sizeof(len), 1, f) != 1) return false;
    if (len > 1024) return false;  // 安全检查
    if (len > 0) {
        s.resize(len);
        return fread(&s[0], 1, len, f) == len;
    }
    s.clear();
    return true;
}

// ============================================================
// 文件存在性检查
// ============================================================

bool Persistence::file_exists(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f) { fclose(f); return true; }
    return false;
}

// ============================================================
// SAVE — 二进制序列化全部系统状态
// ============================================================

std::string Persistence::save(
    ProcessManager& pm, MemoryManager& mm,
    Scheduler& sched, AccountManager& acct,
    int next_pid, int alloc_counter,
    const std::string& path)
{
    // 加锁
    LockGuard lk_pm(pm.mutex());
    LockGuard lk_mm(mm.mutex());
    LockGuard lk_s(sched.mutex());
    LockGuard lk_a(acct.mutex());
    LockGuard lk_q(sched.mlfq().mutex());

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return "[持久化] 无法打开文件 '" + path + "'。";

    // ★ 文件锁：防止多实例同时写入 state.bin
    #ifdef _WIN32
    HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(f));
    bool locked = false;
    if (hFile != INVALID_HANDLE_VALUE) {
        OVERLAPPED ov = {0};
        if (LockFileEx(hFile, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                        0, MAXDWORD, 0, &ov)) {
            locked = true;
        } else {
            fclose(f);
            return "[持久化] 文件正被其他实例写入，请稍后重试。";
        }
    }
    #endif

    bool ok = true;

    // ---- Header ----
    ok = ok && (fwrite(&MAGIC, 4, 1, f) == 1);
    ok = ok && (fwrite(&VERSION, 4, 1, f) == 1);
    uint64_t ts = static_cast<uint64_t>(time(nullptr));
    ok = ok && (fwrite(&ts, 8, 1, f) == 1);
    uint32_t zero = 0;
    ok = ok && (fwrite(&zero, 4, 1, f) == 1);  // checksum placeholder

    // ---- PCB Table ----
    ok = ok && (fwrite(&next_pid, 4, 1, f) == 1);
    const auto& pcb_table = pm.get_pcb_table();
    uint32_t pcb_cnt = static_cast<uint32_t>(pcb_table.size());
    ok = ok && (fwrite(&pcb_cnt, 4, 1, f) == 1);
    for (const auto& kv : pcb_table) {
        const PCB& p = kv.second;
        ok = ok && (fwrite(&p.pid, 4, 1, f) == 1);
        ok = ok && (fwrite(&p.ppid, 4, 1, f) == 1);
        ok = ok && write_string(f, p.name);
        int st = static_cast<int>(p.state);
        ok = ok && (fwrite(&st, 4, 1, f) == 1);
        ok = ok && (fwrite(&p.priority, 4, 1, f) == 1);
        ok = ok && (fwrite(&p.cpu_time, 4, 1, f) == 1);  // ★ 动态数据
        ok = ok && (fwrite(&p.mem_addr, 4, 1, f) == 1);
        ok = ok && (fwrite(&p.mem_size, 4, 1, f) == 1);
        uint32_t cc = static_cast<uint32_t>(p.children.size());
        ok = ok && (fwrite(&cc, 4, 1, f) == 1);
        for (int c : p.children)
            ok = ok && (fwrite(&c, 4, 1, f) == 1);
    }

    // ---- Memory Table ----
    int algo = static_cast<int>(mm.get_algo());
    ok = ok && (fwrite(&algo, 4, 1, f) == 1);
    uint32_t blk_cnt = 0;
    for (const MemoryBlock* b = mm.get_head(); b; b = b->next) ++blk_cnt;
    ok = ok && (fwrite(&blk_cnt, 4, 1, f) == 1);
    for (const MemoryBlock* b = mm.get_head(); b; b = b->next) {
        ok = ok && (fwrite(&b->start_addr, 4, 1, f) == 1);
        ok = ok && (fwrite(&b->size, 4, 1, f) == 1);
        uint8_t ff = b->is_free ? 1 : 0;
        ok = ok && (fwrite(&ff, 1, 1, f) == 1);
        ok = ok && (fwrite(&b->pid, 4, 1, f) == 1);
    }

    // ---- MLFQ State (★ 队列顺序动态数据) ----
    const MLFQ& mlfq = sched.mlfq();
    for (int i = 0; i < MLFQ_LEVELS; ++i) {
        auto pids = mlfq.get_queue_pids(i);
        int sz = static_cast<int>(pids.size());
        ok = ok && (fwrite(&sz, 4, 1, f) == 1);
        for (int pid : pids)
            ok = ok && (fwrite(&pid, 4, 1, f) == 1);
    }

    // ---- Account Data ----
    const auto& users = acct.get_users();
    uint32_t ucnt = static_cast<uint32_t>(users.size());
    ok = ok && (fwrite(&ucnt, 4, 1, f) == 1);
    for (const auto& kv : users) {
        const UserInfo& u = kv.second;
        ok = ok && write_string(f, u.username);
        ok = ok && write_string(f, u.password_hash);
        ok = ok && (fwrite(&u.failed_attempts, 4, 1, f) == 1);
        uint8_t lk = u.locked ? 1 : 0;
        ok = ok && (fwrite(&lk, 1, 1, f) == 1);
    }

    // ---- Alloc Counter ----
    ok = ok && (fwrite(&alloc_counter, 4, 1, f) == 1);

    // 解锁并关闭
    #ifdef _WIN32
    if (locked) {
        OVERLAPPED ov = {0};
        UnlockFileEx(hFile, 0, MAXDWORD, 0, &ov);
    }
    #endif
    fclose(f);

    if (ok) {
        std::cout << "[持久化] 已保存: " << pcb_cnt << " 进程, "
                  << blk_cnt << " 内存块, " << ucnt << " 用户" << std::endl;
        return "";
    }
    return "[持久化] 写入失败!";
}

// ============================================================
// LOAD — 从二进制文件反序列化恢复系统状态
// ============================================================

std::string Persistence::load(
    ProcessManager& pm, MemoryManager& mm,
    Scheduler& sched, AccountManager& acct,
    int& next_pid, int& alloc_counter,
    const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "[持久化] 无法打开文件 '" + path + "'。";

    // ---- Header ----
    uint32_t magic, version;
    uint64_t ts;
    uint32_t checksum;
    if (fread(&magic, 4, 1, f) != 1) { fclose(f); return "[持久化] 读取文件头失败。"; }
    if (magic != MAGIC) { fclose(f); return "[持久化] 文件格式错误 (magic mismatch)。"; }
    if (fread(&version, 4, 1, f) != 1) { fclose(f); return "[持久化] 读取版本失败。"; }
    if (version != VERSION) { fclose(f); return "[持久化] 文件版本不兼容。"; }
    fread(&ts, 8, 1, f);
    fread(&checksum, 4, 1, f);

    // 锁住所有模块进行恢复
    LockGuard lk_pm(pm.mutex());
    LockGuard lk_mm(mm.mutex());
    LockGuard lk_s(sched.mutex());
    LockGuard lk_a(acct.mutex());
    LockGuard lk_q(sched.mlfq().mutex());

    // ---- PCB Table ----
    fread(&next_pid, 4, 1, f);
    uint32_t pcb_cnt;
    fread(&pcb_cnt, 4, 1, f);
    std::map<int, PCB> new_pcb_table;
    for (uint32_t i = 0; i < pcb_cnt; ++i) {
        PCB p;
        fread(&p.pid, 4, 1, f);
        fread(&p.ppid, 4, 1, f);
        read_string(f, p.name);
        int st;
        fread(&st, 4, 1, f);
        p.state = static_cast<ProcessState>(st);
        fread(&p.priority, 4, 1, f);
        fread(&p.cpu_time, 4, 1, f);  // ★ 恢复动态数据
        fread(&p.mem_addr, 4, 1, f);
        fread(&p.mem_size, 4, 1, f);
        uint32_t cc;
        fread(&cc, 4, 1, f);
        p.children.resize(cc);
        for (uint32_t j = 0; j < cc; ++j)
            fread(&p.children[j], 4, 1, f);
        new_pcb_table[p.pid] = p;
    }
    pm.set_pcb_table(new_pcb_table);

    // ---- Memory Table ----
    int algo;
    fread(&algo, 4, 1, f);
    mm.set_alloc_algo(static_cast<MemAlgo>(algo));
    uint32_t blk_cnt;
    fread(&blk_cnt, 4, 1, f);
    // 清空并重建内存链表
    mm.clear_all();
    for (uint32_t i = 0; i < blk_cnt; ++i) {
        int addr, size, pid;
        uint8_t ff;
        fread(&addr, 4, 1, f);
        fread(&size, 4, 1, f);
        fread(&ff, 1, 1, f);
        fread(&pid, 4, 1, f);
        mm.append_block(addr, size, ff == 1, pid);
    }

    // ---- MLFQ State ----
    // 清空 MLFQ
    MLFQ& mlfq = sched.mlfq();
    for (int i = 0; i < MLFQ_LEVELS; ++i) {
        auto pids = mlfq.get_queue_pids(i);
        for (int pid : pids) mlfq.remove(pid);
    }
    // 恢复 MLFQ
    for (int i = 0; i < MLFQ_LEVELS; ++i) {
        int sz;
        fread(&sz, 4, 1, f);
        for (int j = 0; j < sz; ++j) {
            int pid;
            fread(&pid, 4, 1, f);
            const PCB* pcb = pm.get_pcb(pid);
            if (pcb) mlfq.enqueue(pid, pcb->priority);
        }
    }

    // ---- Account Data ----
    uint32_t ucnt;
    fread(&ucnt, 4, 1, f);
    std::map<std::string, UserInfo> new_users;
    for (uint32_t i = 0; i < ucnt; ++i) {
        UserInfo u;
        read_string(f, u.username);
        read_string(f, u.password_hash);
        fread(&u.failed_attempts, 4, 1, f);
        uint8_t lk;
        fread(&lk, 1, 1, f);
        u.locked = (lk == 1);
        new_users[u.username] = u;
    }
    acct.set_users(new_users);

    // ---- Alloc Counter ----
    fread(&alloc_counter, 4, 1, f);

    fclose(f);

    std::cout << "[持久化] 已恢复: " << pcb_cnt << " 进程, "
              << blk_cnt << " 内存块, " << ucnt << " 用户" << std::endl;
    return "";
}
