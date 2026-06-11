#ifndef COMMON_SYNC_H
#define COMMON_SYNC_H

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>

    class Mutex {
        CRITICAL_SECTION cs_;
    public:
        Mutex()  { InitializeCriticalSection(&cs_); }
        ~Mutex() { DeleteCriticalSection(&cs_); }
        void lock()   { EnterCriticalSection(&cs_); }
        void unlock() { LeaveCriticalSection(&cs_); }
    };
#else
    #include <pthread.h>
    class Mutex {
        pthread_mutex_t mtx_;
    public:
        Mutex()  { pthread_mutex_init(&mtx_, nullptr); }
        ~Mutex() { pthread_mutex_destroy(&mtx_); }
        void lock()   { pthread_mutex_lock(&mtx_); }
        void unlock() { pthread_mutex_unlock(&mtx_); }
    };
#endif

class LockGuard {
    Mutex& m_;
public:
    explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }
};

#endif
