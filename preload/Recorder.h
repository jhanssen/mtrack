#pragma once

#include "PipeEmitter.h"
#include "Types.h"
#include "Spinlock.h"
#include <atomic>
#include <cstdint>
#include <string>

class Recorder
{
public:
    Recorder(int fd);

    template<typename... Ts>
    void record(RecordType type, Ts&&... args);

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
    void flush();

private:
    PipeEmitter mEmitter;
    static thread_local bool tScoped;

private:
    friend class Scope;
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
    if (!mWasLocked) {
        mRecorder->tScoped = mWasLocked;
        mRecorder->flush();
    }
}

inline bool Recorder::isScoped() const
{
    return tScoped;
}

inline Recorder::Recorder(int fd)
    : mPipe(fd)
{
}

template<typename... Ts>
inline void Recorder::record(RecordType type, Ts&&... args)
{
    const auto size = Emitter::emitSize(std::forward<Ts>(args)...) + 1;

    if (!tScoped)
        mLock.lock();

    mEmitter.emitWithSize(size, type, std::forward<Ts>(args)...);

    if (!tScoped) {
        mLock.unlock();
        flush();
    }
}
