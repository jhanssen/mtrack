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
#include <atomic>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

struct {
#ifdef __i386__
    gregset_t gregs;
#else
    std::array<void*, Stack::MaxFrames> *ptrs { nullptr };
    uint32_t* count { nullptr };
#endif

    std::atomic<bool> handled = false;
    bool siginstalled = false;
} static sigData;

static void handler(int sig)
{
#ifdef __i386__
    ucontext_t ctx;
    getcontext(&ctx);

    memcpy(&sigData.gregs, &ctx.uc_mcontext.gregs, sizeof(gregset_t));
#else
    *sigData.count = unw_backtrace(sigData.ptrs->data(), Stack::MaxFrames);
#endif

    Waiter wl(sigData.handled);
    wl.notify();
}

inline void Stack::initialize(const StackInitializer& initializer)
{
    unw_context_t context = {};
    unw_getcontext(&context);
    unw_cursor_t cursor = {};
    unw_init_local(&cursor, &context);

    // set regs from other thread
#ifdef __i386__
    unw_set_reg(&cursor, UNW_X86_FS, initializer.gregs[REG_FS]);
    unw_set_reg(&cursor, UNW_X86_ES, initializer.gregs[REG_ES]);
    unw_set_reg(&cursor, UNW_X86_DS, initializer.gregs[REG_DS]);
    unw_set_reg(&cursor, UNW_X86_EDI, initializer.gregs[REG_EDI]);
    unw_set_reg(&cursor, UNW_X86_ESI, initializer.gregs[REG_ESI]);
    unw_set_reg(&cursor, UNW_X86_EBP, initializer.gregs[REG_EBP]);
    unw_set_reg(&cursor, UNW_X86_ESP, initializer.gregs[REG_ESP]);
    unw_set_reg(&cursor, UNW_X86_EBX, initializer.gregs[REG_EBX]);
    unw_set_reg(&cursor, UNW_X86_EDX, initializer.gregs[REG_EDX]);
    unw_set_reg(&cursor, UNW_X86_ECX, initializer.gregs[REG_ECX]);
    unw_set_reg(&cursor, UNW_X86_EAX, initializer.gregs[REG_EAX]);
    unw_set_reg(&cursor, UNW_X86_EIP, initializer.gregs[REG_EIP]);
    unw_set_reg(&cursor, UNW_X86_CS, initializer.gregs[REG_CS]);
    unw_set_reg(&cursor, UNW_X86_SS, initializer.gregs[REG_SS]);
#else
    unw_set_reg(&cursor, UNW_X86_64_R15, initializer.gregs[REG_R15]);
    unw_set_reg(&cursor, UNW_X86_64_R14, initializer.gregs[REG_R14]);
    unw_set_reg(&cursor, UNW_X86_64_R13, initializer.gregs[REG_R13]);
    unw_set_reg(&cursor, UNW_X86_64_R12, initializer.gregs[REG_R12]);
    unw_set_reg(&cursor, UNW_X86_64_RBP, initializer.gregs[REG_RBP]);
    unw_set_reg(&cursor, UNW_X86_64_RBX, initializer.gregs[REG_RBX]);
    unw_set_reg(&cursor, UNW_X86_64_R11, initializer.gregs[REG_R11]);
    unw_set_reg(&cursor, UNW_X86_64_R10, initializer.gregs[REG_R10]);
    unw_set_reg(&cursor, UNW_X86_64_R9,  initializer.gregs[REG_R9]);
    unw_set_reg(&cursor, UNW_X86_64_R8,  initializer.gregs[REG_R8]);
    unw_set_reg(&cursor, UNW_X86_64_RAX, initializer.gregs[REG_RAX]);
    unw_set_reg(&cursor, UNW_X86_64_RCX, initializer.gregs[REG_RCX]);
    unw_set_reg(&cursor, UNW_X86_64_RDX, initializer.gregs[REG_RDX]);
    unw_set_reg(&cursor, UNW_X86_64_RSI, initializer.gregs[REG_RSI]);
    unw_set_reg(&cursor, UNW_X86_64_RDI, initializer.gregs[REG_RDI]);
    unw_set_reg(&cursor, UNW_X86_64_RIP, initializer.gregs[REG_RIP]);
    unw_set_reg(&cursor, UNW_X86_64_RSP, initializer.gregs[REG_RSP]);
#endif
    // printf("hallo %u\n", gettid());
    while (unw_step(&cursor) > 0 && mCount < MaxFrames) {
        unw_word_t ip = 0;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        // printf("ip %lx sp %lx\n", ip, sp);
        if (ip > 0) {
            mPtrs[mCount++] = reinterpret_cast<void *>(ip);
        } else {
            break;
        }
    }
    // printf("donezo\n");
}

Stack::Stack(unsigned ptid)
{
    // dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);

    if (ptid == 0) {
        mCount = unw_backtrace(mPtrs.data(), MaxFrames);
    } else {
        if (!sigData.siginstalled) {
            struct sigaction sa;
            sa.sa_flags = SA_SIGINFO;
            sigemptyset(&sa.sa_mask);
            sa.sa_handler = handler;
            sigaction(SIGUSR1, &sa, nullptr);
            sigData.siginstalled = true;
        }

#ifndef __i386__
        sigData.ptrs = &mPtrs;
        sigData.count = &mCount;
#endif

        syscall(SYS_tkill, ptid, SIGUSR1);

        Waiter wl(sigData.handled);
        wl.wait();

#ifdef __i386__
        StackInitializer init;
        memcpy(&init.gregs, &sigData.gregs, sizeof(gregset_t));
        initialize(init);
#endif
    }
}

void Stack::flushCache()
{
    unw_flush_cache(unw_local_addr_space, 0, 0);
}
