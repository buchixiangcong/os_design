#ifndef COMMON_SYNC_H
#define COMMON_SYNC_H

// ============================================================
// 跨平台互斥锁包装
// 此 MinGW 使用 win32 线程模型，std::mutex 不可用
// 故在 Windows 上使用 CRITICAL_SECTION
// ============================================================

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600  // Vista+ for CONDITION_VARIABLE
    #endif
    #include <windows.h>

    /// Windows CRITICAL_SECTION 包装
    class Mutex {
    public:
        Mutex()  { InitializeCriticalSection(&cs_); }
        ~Mutex() { DeleteCriticalSection(&cs_); }
        Mutex(const Mutex&) = delete;
        Mutex& operator=(const Mutex&) = delete;

        void lock()   { EnterCriticalSection(&cs_); }
        void unlock() { LeaveCriticalSection(&cs_); }
    private:
        CRITICAL_SECTION cs_;
    };

    /// RAII 锁包装
    class LockGuard {
    public:
        explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
        ~LockGuard() { m_.unlock(); }
        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(const LockGuard&) = delete;
    private:
        Mutex& m_;
    };

#else
    // Linux/POSIX 平台使用 pthread
    #include <pthread.h>

    class Mutex {
    public:
        Mutex()  { pthread_mutex_init(&mtx_, nullptr); }
        ~Mutex() { pthread_mutex_destroy(&mtx_); }
        Mutex(const Mutex&) = delete;
        Mutex& operator=(const Mutex&) = delete;

        void lock()   { pthread_mutex_lock(&mtx_); }
        void unlock() { pthread_mutex_unlock(&mtx_); }
    private:
        pthread_mutex_t mtx_;
    };

    class LockGuard {
    public:
        explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
        ~LockGuard() { m_.unlock(); }
        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(const LockGuard&) = delete;
    private:
        Mutex& m_;
    };
#endif

#endif // COMMON_SYNC_H
