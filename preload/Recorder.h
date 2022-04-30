#pragma once

#include "Spinlock.h"
#include <common/Version.h>
#include <common/RecordType.h>
#include <common/Emitter.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>
#include <concepts>

class RecorderEmitter
{
public:
    RecorderEmitter(Recorder* recorder);

    virtual void writeBytes(const void* data, size_t size, WriteType type) override;

private:
    Recorder* mRecorder;
};

class Recorder
{
public:
    Recorder(int fd);

    template<typename... Ts>
    void record(RecordType type, Ts... args);

    void initialize(const char* filename);
    void cleanup();

    struct String
    {
        String(const char* s);
        String(const char* s, size_t sz);

        const char* const str { nullptr };
        const uint32_t size { 0 };
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
    uint32_t mOffset { 0 };
    std::vector<uint8_t> mData;
    std::thread mThread;
    std::atomic<bool> mRunning;
    static thread_local bool tScoped;
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

inline bool Recorder::isScoped() const;

inline Recorder::Recorder()
{
    mData.resize(sizeof(FileVersion));
    const auto version = FileVersion::Current;
    memcpy(mData.data(), &version, sizeof(FileVersion));
    mOffset = sizeof(FileVersion);
}

inline Recorder::String::String(const char* s)
    : str(s), size(static_cast<uint32_t>(strlen(s)))
{
}

inline Recorder::String::String(const char* s, size_t sz)
    : str(s), size(sz)
{
}

namespace detail {
template<typename T>
inline size_t recordSize_helper(T) requires std::integral<T>
{
    return sizeof(T);
}

template<typename T>
inline size_t recordSize_helper(T) requires std::is_enum_v<T>
{
    return sizeof(T);
}

template<typename T>
inline size_t recordSize_helper(const T str) requires std::same_as<T, Recorder::String>
{
    return str.size + sizeof(uint32_t);
}

template<typename T>
inline size_t recordSize(T arg)
{
    return recordSize_helper<T>(arg);
}

template<typename T, typename... Ts>
inline size_t recordSize(T arg, Ts... args)
{
    return recordSize_helper<T>(arg) + recordSize(args...);
}

template<typename T>
inline void record_helper(uint8_t*& data, T arg) requires std::integral<T>
{
    memcpy(data, &arg, sizeof(T));
    data += sizeof(T);
}

template<typename T>
inline void record_helper(uint8_t*& data, T arg) requires std::is_enum_v<T>
{
    memcpy(data, &arg, sizeof(T));
    data += sizeof(T);
}

template<typename T>
inline void record_helper(uint8_t*& data, const T str) requires std::same_as<T, Recorder::String>
{
    record_helper(data, str.size);
    memcpy(data, str.str, str.size);
    data += str.size;
}

template<typename T>
inline void record(uint8_t*& data, T arg)
{
    record_helper<T>(data, arg);
}

template<typename T, typename... Ts>
inline void record(uint8_t*& data, T arg, Ts... args)
{
    record_helper<T>(data, arg);
    record(data, args...);
}
} // namespace detail

template<typename... Ts>
inline void Recorder::record(RecordType type, Ts... args)
{
    const auto size = detail::recordSize(args...) + 1;

    if (!tScoped)
        mLock.lock();

    if (mOffset + size >= mData.size()) {
        mData.resize(mOffset + size);
    }

    uint8_t* data = mData.data() + mOffset;
    *data++ = static_cast<uint8_t>(type);
    detail::record(data, args...);
    mOffset += size;

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
