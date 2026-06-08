#include "scheduler/scheduler.h"
#include "process/process_manager.h"
#include "memory/memory_manager.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#endif

// ============================================================
// 构造与析构
// ============================================================

Scheduler::Scheduler(ProcessManager& proc_mgr, MemoryManager& mem_mgr)
    : proc_mgr_(proc_mgr), mem_mgr_(mem_mgr)
    , running_(false), paused_(false)
    , speed_mult_(1.0)
    , sched_thread_(nullptr)
{
}

Scheduler::~Scheduler() {
    stop_sched();
}

// ============================================================
// 调度核心逻辑
// ============================================================

std::string Scheduler::execute_one_tick() {
    std::ostringstream log;
    LockGuard lock(mutex_);  // 持有锁确保原子性

    // === 第一步：扫描队列，选出下一个要运行的进程 ===
    log << "\n╔══════════════════════════════════════════╗\n";
    log << "║          MLFQ 调度决策 — 单步执行         ║\n";
    log << "╚══════════════════════════════════════════╝\n\n";

    // 打印队列快照
    log << "[队列快照]\n" << mlfq_.to_string() << "\n";

    int next_pid = mlfq_.pick_and_dequeue();

    if (next_pid < 0) {
        log << "[结果] 所有队列为空，无可调度进程。\n";
        return log.str();
    }

    PCB* pcb = proc_mgr_.get_pcb_mutable(next_pid);
    if (!pcb) {
        log << "[错误] 进程 PID=" << next_pid << " 不在进程表中。\n";
        return log.str();
    }

    int current_level = get_queue_level(pcb->priority);
    int time_slice = MLFQ_TIME_SLICE[current_level];

    log << "[选程分析]\n";
    log << "  选中进程: " << pcb->name << " (PID=" << next_pid << ")\n";
    log << "  所属队列: Q" << current_level
        << " (优先级 " << MLFQ_PRIO_MIN[current_level]
        << "-" << MLFQ_PRIO_MAX[current_level] << ")\n";
    log << "  时间片:   " << time_slice << " 秒\n";
    log << "  进程优先级: " << pcb->priority << "\n";
    log << "  已执行CPU:  " << pcb->cpu_time << " 个时间单位\n";

    // === 第二步：模拟执行 ===
    log << "\n[执行详情]\n";

    // 将其他 RUNNING 进程变为 READY
    auto all_pids = proc_mgr_.get_all_pids();
    for (int pid : all_pids) {
        PCB* other = proc_mgr_.get_pcb_mutable(pid);
        if (other && other->state == ProcessState::RUNNING && pid != next_pid) {
            other->state = ProcessState::READY;
            log << "  PID:" << pid << " (" << other->name << ") RUNNING -> READY\n";
        }
    }

    // 执行当前进程
    if (pcb->state == ProcessState::READY || pcb->state == ProcessState::RUNNING) {
        pcb->state = ProcessState::RUNNING;
        pcb->cpu_time += time_slice;
        log << "  PID:" << next_pid << " (" << pcb->name << ") 执行 " << time_slice
            << " 秒 (累计CPU: " << pcb->cpu_time << ")\n";

        // === 第三步：处理时间片耗尽后的降级 ===
        // 如果还有更低优先级的队列可以降入
        if (current_level < MLFQ_LEVELS - 1) {
            // 降级到下一级队列
            int new_level = current_level + 1;
            int new_prio_min = MLFQ_PRIO_MIN[new_level];
            // 调整优先级到新队列范围
            pcb->priority = new_prio_min;
            mlfq_.enqueue(next_pid, pcb->priority);
            log << "  ⚠ 时间片耗尽! 降级: Q" << current_level
                << " -> Q" << new_level << " (新优先级: " << pcb->priority << ")\n";
        } else {
            // 已在最低队列，轮转到队尾
            mlfq_.enqueue(next_pid, pcb->priority);
            log << "  ⚡ 已是最低队列，轮转到队尾\n";
        }

        // 进程执行后变为 READY
        pcb->state = ProcessState::READY;
    } else if (pcb->state == ProcessState::BLOCKED) {
        log << "  PID:" << next_pid << " 处于 BLOCKED 状态，跳过执行。\n";
        mlfq_.enqueue(next_pid, pcb->priority);
    } else if (pcb->state == ProcessState::SUSPENDED) {
        log << "  PID:" << next_pid << " 处于 SUSPENDED 状态，移出就绪队列。\n";
        // 已从 mlfq 中移除（pick_and_dequeue），不再放回
        log << "  ⚠ 挂起进程已移出调度队列。\n";
    }

    // === 第四步：执行后的队列状态 ===
    log << "\n[执行后队列]\n" << mlfq_.to_string() << "\n";
    log << "════════════════════════════════════════════\n";

    return log.str();
}

// ============================================================
// 辅助操作
// ============================================================

void Scheduler::add_to_mlfq(int pid) {
    PCB* pcb = proc_mgr_.get_pcb_mutable(pid);
    if (pcb && (pcb->state == ProcessState::READY || pcb->state == ProcessState::RUNNING)) {
        mlfq_.enqueue(pid, pcb->priority);
    }
}

void Scheduler::handle_time_slice_exhausted(int pid) {
    // 在 execute_one_tick 中处理
}

// ============================================================
// 命令接口
// ============================================================

bool Scheduler::start_sched() {
    if (running_) {
        std::cout << "[调度] 调度器已在运行中。" << std::endl;
        return false;
    }

    // 将所有 READY 进程加入 MLFQ
    auto pids = proc_mgr_.get_all_pids();
    for (int pid : pids) {
        PCB* pcb = proc_mgr_.get_pcb_mutable(pid);
        if (pcb && pcb->state == ProcessState::READY) {
            mlfq_.enqueue(pid, pcb->priority);
        }
    }

    // 将 init 也加入（如果只有 init 在运行）
    PCB* init = proc_mgr_.get_pcb_mutable(1);
    if (init && init->state == ProcessState::RUNNING) {
        mlfq_.enqueue(1, init->priority);
    }

    running_ = true;
    paused_ = false;

    std::cout << "[调度] MLFQ 自动调度已启动!" << std::endl;
    std::cout << mlfq_.to_string() << std::endl;

#ifdef _WIN32
    // 使用 Windows 线程创建后台调度线程
    sched_thread_ = reinterpret_cast<void*>(
        _beginthreadex(nullptr, 0, sched_thread_func, this, 0, nullptr));
    if (!sched_thread_) {
        running_ = false;
        std::cout << "[错误] 无法创建调度线程。" << std::endl;
        return false;
    }
#endif

    return true;
}

bool Scheduler::stop_sched() {
    if (!running_) {
        std::cout << "[调度] 调度器未在运行。" << std::endl;
        return false;
    }

    running_ = false;
    paused_ = true;

#ifdef _WIN32
    if (sched_thread_) {
        // 等待线程结束（最多等2秒）
        WaitForSingleObject(sched_thread_, 2000);
        CloseHandle(sched_thread_);
        sched_thread_ = nullptr;
    }
#endif

    std::cout << "[调度] 调度器已停止。所有进程和队列状态已冻结。" << std::endl;
    return true;
}

bool Scheduler::restart_sched() {
    if (running_) {
        std::cout << "[调度] 调度器已在运行中，无需重启。" << std::endl;
        return false;
    }

    paused_ = false;
    std::cout << "[调度] 调度器已重启，从冻结状态恢复。" << std::endl;
    return start_sched();
}

std::string Scheduler::step() {
    if (running_ && !paused_) {
        return "[错误] 调度器正在自动运行中。请先执行 stop_sched 暂停调度。\n";
    }

    // 如果 MLFQ 为空，将 READY 进程加入
    bool all_empty = true;
    for (int i = 0; i < MLFQ_LEVELS; ++i) {
        if (mlfq_.size(i) > 0) { all_empty = false; break; }
    }
    if (all_empty) {
        auto pids = proc_mgr_.get_all_pids();
        for (int pid : pids) {
            PCB* pcb = proc_mgr_.get_pcb_mutable(pid);
            if (pcb && (pcb->state == ProcessState::READY || pcb->state == ProcessState::RUNNING)) {
                mlfq_.enqueue(pid, pcb->priority);
            }
        }
    }

    return execute_one_tick();
}

// ============================================================
// 后台调度线程函数 (Windows)
// ============================================================

#ifdef _WIN32
unsigned __stdcall Scheduler::sched_thread_func(void* arg) {
    Scheduler* sched = static_cast<Scheduler*>(arg);

    while (sched->running_) {
        if (sched->paused_) {
            // 暂停时休眠
            Sleep(100);
            continue;
        }

        // 确保 MLFQ 中有所有 READY 进程
        auto pids = sched->proc_mgr_.get_all_pids();
        for (int pid : pids) {
            PCB* pcb = sched->proc_mgr_.get_pcb_mutable(pid);
            if (pcb && pcb->state == ProcessState::READY) {
                sched->mlfq_.enqueue(pid, pcb->priority);
            }
        }

        // 执行一次调度
        std::string log = sched->execute_one_tick();
        std::cout << log << std::endl;

        // 等待时间片（可调倍率）
        int sleep_ms = static_cast<int>(2000 * sched->speed_mult_);
        if (sleep_ms < 50) sleep_ms = 50;
        Sleep(sleep_ms);
    }

    return 0;
}
#endif
