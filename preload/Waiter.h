#pragma once

#include <atomic>

class Waiter
{
public:
    Waiter(std::atomic<bool>& l)
        : mLock(l)
    {
    }

    void wait()
    {
        for (;;) {
            if (mLock.exchange(false, std::memory_order_acquire) == true) {
                break;
            }
            while (mLock.load(std::memory_order_relaxed) == false) {
                __builtin_ia32_pause();
            }
        }
    }

    void notify() {
        mLock.store(true, std::memory_order_release);
    }

private:
    std::atomic<bool>& mLock;
};

