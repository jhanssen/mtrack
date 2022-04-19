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
    gregset_t gregs;
    std::atomic<bool> handled = false;
    bool siginstalled = false;
} data;

static void handler(int sig)
{
    ucontext_t ctx;
    getcontext(&ctx);

    memcpy(&data.gregs, &ctx.uc_mcontext.gregs, sizeof(gregset_t));

    Waiter wl(data.handled);
    wl.notify();
}

Stack::Stack(const StackInitializer& initializer)
{
    initialize(initializer);
}

void Stack::initialize(const StackInitializer& initializer)
{
    unw_context_t context = {};
    unw_getcontext(&context);
    unw_cursor_t cursor = {};
    unw_init_local(&cursor, &context);

    // set regs from other thread
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

    // printf("hallo %u\n", gettid());
    while (unw_step(&cursor) > 0) {
        unw_word_t ip = 0, sp = 0;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        unw_get_reg(&cursor, UNW_REG_SP, &sp);
        // printf("ip %lx sp %lx\n", ip, sp);
        if (ip > 0) {
            // not sure why but ip is consistently one past where I need it to be
            mPtrs.push_back(std::make_pair<long long unsigned, long long unsigned>(ip - 1, sp));
        } else {
            break;
        }
    }
    // printf("donezo\n");
}

ThreadStack::ThreadStack(unsigned ptid)
{
    // dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);

    if (ptid == 0) {
        ucontext_t ctx;
        getcontext(&ctx);

        StackInitializer init;
        memcpy(&init.gregs, &ctx.uc_mcontext.gregs, sizeof(gregset_t));
        initialize(init);
    } else {
        if (!data.siginstalled) {
            struct sigaction sa;
            sa.sa_flags = SA_SIGINFO;
            sigemptyset(&sa.sa_mask);
            sa.sa_handler = handler;
            sigaction(SIGUSR1, &sa, nullptr);
            data.siginstalled = true;
        }

        syscall(SYS_tkill, ptid, SIGUSR1);

        Waiter wl(data.handled);
        wl.wait();

        StackInitializer init;
        memcpy(&init.gregs, &data.gregs, sizeof(gregset_t));
        initialize(init);
    }
}
