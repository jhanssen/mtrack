#pragma once

#include "Emitter.h"
#include "Types.h"
#include "Spinlock.h"
#include <common/Version.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
#include <concepts>

class Recorder
{
public:
    Recorder();

    template<typename... Ts>
    void record(RecordType type, Ts&&... args);

    void initialize(const char* filename);
    void cleanup();

    struct String
    {
        String(const char* s);
        String(const char* s, uint32_t sz);
        String(const std::string& s);

        const void *data() const { return str; }
        uint32_t size() const { return len; }

        const char* const str { nullptr };
        const uint32_t len { 0 };
    };

    class Scope
    {
    public:
        Scope(Recorder* recorder);
        ~Scope();

    private:
        Recorder* mRecorder;
        ScopedSpinlock mLock;
        bool mWasLocked;
    };

    bool isScoped() const;

private:
    static void process(Recorder* recorder);

private:
    Spinlock mLock;
    Emitter mEmitter;
    std::thread mThread;
    std::atomic<bool> mRunning;
    static thread_local bool tScoped;
    FILE* mFile { nullptr };
};

inline Recorder::Scope::Scope(Recorder* recorder)
    : mRecorder(recorder), mLock(recorder->mLock, false)
{
    mWasLocked = recorder->tScoped;
    if (!mWasLocked) {
        recorder->tScoped = true;
        mLock.lock();
    }
}

inline Recorder::Scope::~Scope()
{
    if (!mWasLocked)
        mRecorder->tScoped = mWasLocked;
}

inline bool Recorder::isScoped() const
{
    return tScoped;
}

inline Recorder::Recorder()
{
}

inline Recorder::String::String(const char* s)
    : str(s), len(static_cast<uint32_t>(strlen(s)))
{
}

inline Recorder::String::String(const char* s, uint32_t sz)
    : str(s), len(sz)
{
}

inline Recorder::String::String(const std::string& s)
    : str(s.c_str()), len(static_cast<uint32_t>(s.size()))
{
}

template<typename... Ts>
inline void Recorder::record(RecordType type, Ts&&... args)
{
    const auto size = Emitter::emitSize(std::forward<Ts>(args)...) + 1;

    if (!tScoped)
        mLock.lock();

    mEmitter.emitWithSize(size, type, std::forward<Ts>(args)...);

    if (!tScoped)
        mLock.unlock();
}

inline void Recorder::cleanup()
{
    if (mRunning.load(std::memory_order_acquire)) {
        mRunning.store(false, std::memory_order_release);
        mThread.join();
    }
}
