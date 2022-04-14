#include "Stack.h"
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/user.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <atomic>
#include <execinfo.h>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

static int dl_iterate_phdr_callback(struct dl_phdr_info* info, size_t /*size*/, void* data)
{
    const char* fileName = info->dlpi_name;
    if (!fileName || !fileName[0]) {
        fileName = "x";
    }

    printf("dl it %s %zx\n", fileName, info->dlpi_addr);

    for (int i = 0; i < info->dlpi_phnum; i++) {
        const auto& phdr = info->dlpi_phdr[i];
        if (phdr.p_type == PT_LOAD) {
            printf(" - %zx %zx\n", phdr.p_vaddr, phdr.p_memsz);
        }
    }

    return 0;
}

struct Waiter
{
    Waiter(std::atomic<bool>& l)
        : lock_(l)
    {
    }

    std::atomic<bool>& lock_;

    void wait()
    {
        for (;;) {
            if (lock_.exchange(false, std::memory_order_acquire) == true) {
                break;
            }
            while (lock_.load(std::memory_order_relaxed) == false) {
                __builtin_ia32_pause();
            }
        }
    }

    void notify() {
        lock_.store(true, std::memory_order_release);
    }
};


gregset_t gregset;
std::atomic<bool> handled = false;

static void handler(int sig)
{
    ucontext_t ctx;
    getcontext(&ctx);

    memcpy(&gregset, &ctx.uc_mcontext.gregs, sizeof(gregset_t));

    Waiter wl(handled);
    wl.notify();
}

Stack::Stack(uint32_t ptid)
{
    dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);

    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = handler;
    sigaction(SIGUSR1, &sa, nullptr);

    unw_context_t context = {};
    unw_getcontext(&context);
    unw_cursor_t cursor = {};
    unw_init_local(&cursor, &context);

    if (ptid > 0) {
#ifdef MTRACK_HAS_USER_REGS_STRUCT
        syscall(SYS_tkill, ptid, SIGUSR1);

        Waiter wl(handled);
        wl.wait();

        // set regs from other thread
        unw_set_reg(&cursor, UNW_X86_64_R15, gregset[REG_R15]);
        unw_set_reg(&cursor, UNW_X86_64_R14, gregset[REG_R14]);
        unw_set_reg(&cursor, UNW_X86_64_R13, gregset[REG_R13]);
        unw_set_reg(&cursor, UNW_X86_64_R12, gregset[REG_R12]);
        unw_set_reg(&cursor, UNW_X86_64_RBP, gregset[REG_RBP]);
        unw_set_reg(&cursor, UNW_X86_64_RBX, gregset[REG_RBX]);
        unw_set_reg(&cursor, UNW_X86_64_R11, gregset[REG_R11]);
        unw_set_reg(&cursor, UNW_X86_64_R10, gregset[REG_R10]);
        unw_set_reg(&cursor, UNW_X86_64_R9, gregset[REG_R9]);
        unw_set_reg(&cursor, UNW_X86_64_R8, gregset[REG_R8]);
        unw_set_reg(&cursor, UNW_X86_64_RAX, gregset[REG_RAX]);
        unw_set_reg(&cursor, UNW_X86_64_RCX, gregset[REG_RCX]);
        unw_set_reg(&cursor, UNW_X86_64_RDX, gregset[REG_RDX]);
        unw_set_reg(&cursor, UNW_X86_64_RSI, gregset[REG_RSI]);
        unw_set_reg(&cursor, UNW_X86_64_RDI, gregset[REG_RDI]);
        unw_set_reg(&cursor, UNW_X86_64_RIP, gregset[REG_RIP]);
        unw_set_reg(&cursor, UNW_X86_64_RSP, gregset[REG_RSP]);

        // printf("got rip %lx %lx\n", faen.rip, faen.rsp);

        //printf("read %llx %llx %llx from %s\n", stack, sp, ip, buf);

        //printf("rrr ip %llx sp %llx\n", ip, sp);
#else
        for (size_t i=0; i<sizeof(regs.uregs) / sizeof(regs.uregs[0]); ++i) {
            printf("got numbers %zu %lu\n", i, regs.uregs[i]);
        }
#endif
    }

    printf("hallo %u\n", gettid());
    while (unw_step(&cursor) > 0) {
        unw_word_t ip = 0, sp = 0;
        // printf("DFFLFKFKFK 2\n");
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        // printf("DFFLFKFKFK 3\n");
        unw_get_reg(&cursor, UNW_REG_SP, &sp);
        printf("ip %lx sp %lx\n", ip, sp);
        if (ip > 0) {
            mPtrs.push_back(std::make_pair(ip, sp));
            //break;
        }
    }
    printf("donezo\n");
}
