#pragma once

#include "Spinlock.h"
#include <common/Version.h>
#include <common/RecordType.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <cstdint>
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

    size_t fileOffset() const
    {
        return ftell(mFile) + mOffset;
    }

private:
    static void process(Recorder* recorder);

private:
    Spinlock mLock;
    size_t mOffset { 0 };
    std::vector<uint8_t> mData;
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

namespace detail {
template<typename T>
concept HasDataSize = requires(T& t)
{
    { t.data() } -> std::same_as<const void*>;
    { t.size() } -> std::same_as<uint32_t>;
};

template<typename T>
inline size_t recordSize_helper(T&&) requires (!HasDataSize<std::decay_t<T>>)
{
    return sizeof(T);
}

template<typename T>
inline size_t recordSize_helper(T&& str) requires (HasDataSize<std::decay_t<T>>)
{
    return str.size() + sizeof(uint32_t);
}

template<typename T>
inline size_t recordSize(T&& arg)
{
    return recordSize_helper<T>(std::forward<T>(arg));
}

template<typename T, typename... Ts>
inline size_t recordSize(T&& arg, Ts&&... args)
{
    return recordSize_helper<T>(std::forward<T>(arg)) + recordSize(std::forward<Ts>(args)...);
}

template<typename T>
inline void record_helper(uint8_t*& data, T&& arg) requires (!HasDataSize<std::decay_t<T>>)
{
    memcpy(data, &arg, sizeof(T));
    data += sizeof(T);
}

template<typename T>
inline void record_helper(uint8_t*& data, T&& str) requires HasDataSize<std::decay_t<T>>
{
    record_helper(data, str.size());
    memcpy(data, str.data(), str.size());
    data += str.size();
}

template<typename T>
inline void record(uint8_t*& data, T&& arg)
{
    record_helper<T>(data, std::forward<T>(arg));
}

template<typename T, typename... Ts>
inline void record(uint8_t*& data, T&& arg, Ts&&... args)
{
    record_helper<T>(data, std::forward<T>(arg));
    record(data, std::forward<Ts>(args)...);
}
} // namespace detail

template<typename... Ts>
inline void Recorder::record(RecordType type, Ts&&... args)
{
    const auto size = detail::recordSize(std::forward<Ts>(args)...) + 1;

    if (!tScoped)
        mLock.lock();

    if (mOffset + size > mData.size()) {
        mData.resize(mOffset + size);
    }

    uint8_t* data = mData.data() + mOffset;
    *data++ = static_cast<uint8_t>(type);
    detail::record(data, std::forward<Ts>(args)...);
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
