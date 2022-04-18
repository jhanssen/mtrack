#pragma once

#include "Waiter.h"

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
    ScopedSpinlock(Spinlock& lock)
        : mLock(lock)
    {
        mLock.lock();
    }

    ~ScopedSpinlock()
    {
        mLock.unlock();
    }

private:
    Spinlock& mLock;
};
