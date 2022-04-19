#pragma once

#include "Spinlock.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

class Recorder
{
public:
    Recorder() = default;

    void record(const char* fmt, ...);

    void initialize(const char* filename);
    void cleanup();

private:
    static void process(Recorder* recorder);

private:
    Spinlock mLock;
    uint32_t mOffset { 0 };
    std::vector<uint8_t> mData;
    std::thread mThread;
    std::atomic<bool> mRunning;
    FILE* mFile { nullptr };
};

inline void Recorder::record(const char* fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    const int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    // printf("recorded %d\n", len);

    ScopedSpinlock lock(mLock);
    if (mOffset + len >= mData.size()) {
        mData.resize(mOffset + len);
    }
    memcpy(&mData[mOffset], buf, len);
    mOffset += len;
}

inline void Recorder::cleanup()
{
    if (mRunning.load(std::memory_order_acquire)) {
        mRunning.store(false, std::memory_order_release);
        mThread.join();
    }
}
