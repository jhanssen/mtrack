#include "Stack.h"
#include "Waiter.h"

#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <array>
#include <atomic>
#include <mutex>
#include <dlfcn.h>
#include <sched.h>
#include <cassert>

#include <asan_unwind.h>

bool Stack::sNoMmap = false;

namespace {
struct SigData {
    std::array<uintptr_t, Stack::MaxFrames> stack;
    size_t stackSize = 0;

    std::atomic<uint32_t> ptid = 0;
    std::atomic<bool> handled = false;
};

enum { MaxSigDatas = 20 };

struct {
    std::array<SigData, MaxSigDatas> datas;
    std::atomic_flag siginstalled = ATOMIC_FLAG_INIT;
} static sigDatas;

static inline SigData* findSigData(uint32_t ptid)
{
    for (auto& sig : sigDatas.datas) {
        if (sig.ptid.load(std::memory_order_acquire) == ptid)
            return &sig;
    }
    return nullptr;
}

void handler(int /*sig*/)
{
    auto sigData = findSigData(syscall(SYS_gettid));

    asan_unwind::StackTrace st(sigData->stack.data(), Stack::MaxFrames);
    sigData->stackSize = st.unwindSlow(0);

    Waiter wl(sigData->handled);
    wl.notify();
}
} // anonymous namespace

Stack::Stack(unsigned skip, unsigned ptid)
{
    // dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);

    if (ptid == 0) {
        //mCount = unw_backtrace(mPtrs.data(), MaxFrames);
        asan_unwind::StackTrace st(mPtrs.data(), MaxFrames);
        mCount = st.unwindSlow(skip);
    } else if (sNoMmap) {
        mCount = 0;
    } else {
        mCount = 1;
        if (!sigDatas.siginstalled.test_and_set()) {
            struct sigaction sa;
            sa.sa_flags = SA_SIGINFO;
            sigemptyset(&sa.sa_mask);
            sa.sa_handler = handler;
            sigaction(SIGUSR1, &sa, nullptr);
        }

        uint32_t expected = 0;
        SigData* sigData = nullptr;
        while (sigData == nullptr) {
            for (auto& candidate : sigDatas.datas) {
                if (candidate.ptid.load(std::memory_order_acquire) == expected
                    && std::atomic_compare_exchange_weak_explicit(&candidate.ptid, &expected, ptid,
                                                                  std::memory_order_release,
                                                                  std::memory_order_relaxed)) {
                    sigData = &candidate;
                    break;
                }
            }
            if (sigData == nullptr)
                sched_yield();
        }

        syscall(SYS_tkill, ptid, SIGUSR1);

        Waiter wl(sigData->handled);
        wl.wait();

        if (sigData->stackSize > 0) {
            static_assert(sizeof(uintptr_t) == sizeof(void*));
            memcpy(mPtrs.data(), static_cast<uintptr_t*>(sigData->stack.data()) + skip, (sigData->stackSize - skip) * sizeof(uintptr_t));
        }
        mCount = sigData->stackSize;

        sigData->ptid.store(0, std::memory_order_release);
    }
}
