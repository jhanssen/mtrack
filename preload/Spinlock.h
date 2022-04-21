#pragma once

#include "Waiter.h"
#include <cassert>

class Spinlock
{
public:
    Spinlock();

    void lock() { mWaiter.wait(); }
    void unlock() { mWaiter.notify(); }

private:
   std::atomic<bool> mLock;
   Waiter mWaiter;
};

inline Spinlock::Spinlock()
    : mLock(true), mWaiter(mLock)
{
}

class ScopedSpinlock
{
public:
    ScopedSpinlock(Spinlock& lock, bool dolock = true)
        : mLock(lock), mLocked(false)
    {
        if (dolock) {
            mLocked = true;
            mLock.lock();
        }
    }

    ~ScopedSpinlock()
    {
        if (mLocked)
            mLock.unlock();
    }

    void lock()
    {
        assert(!mLocked);
        mLocked = true;
        mLock.lock();
    }

    void unlock()
    {
        assert(mLocked);
        mLocked = false;
        mLock.unlock();
    }

private:
    Spinlock& mLock;
    bool mLocked;
};
