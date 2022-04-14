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

struct Faen
{
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rip;
    uint64_t rsp;
};

Faen faen;
std::atomic<bool> done = false;

void handler(int sig, siginfo_t* info, void* ptr)
{
    //ucontext_t *ucontext = (ucontext_t*)ptr;
    ucontext_t ctx;
    getcontext(&ctx);

    ucontext_t* ucontext = &ctx;
    printf("got signal %u\n", gettid());

    faen.r15 = ucontext->uc_mcontext.gregs[REG_R15];
    faen.r14 = ucontext->uc_mcontext.gregs[REG_R14];
    faen.r13 = ucontext->uc_mcontext.gregs[REG_R13];
    faen.r12 = ucontext->uc_mcontext.gregs[REG_R12];
    faen.rbp = ucontext->uc_mcontext.gregs[REG_RBP];
    faen.rbx = ucontext->uc_mcontext.gregs[REG_RBX];
    faen.r11 = ucontext->uc_mcontext.gregs[REG_R11];
    faen.r10 = ucontext->uc_mcontext.gregs[REG_R10];
    faen.r9 = ucontext->uc_mcontext.gregs[REG_R9];
    faen.r8 = ucontext->uc_mcontext.gregs[REG_R8];
    faen.rax = ucontext->uc_mcontext.gregs[REG_RAX];
    faen.rcx = ucontext->uc_mcontext.gregs[REG_RCX];
    faen.rdx = ucontext->uc_mcontext.gregs[REG_RDX];
    faen.rsi = ucontext->uc_mcontext.gregs[REG_RSI];
    faen.rdi = ucontext->uc_mcontext.gregs[REG_RDI];
    faen.rip = ucontext->uc_mcontext.gregs[REG_RIP];
    faen.rsp = ucontext->uc_mcontext.gregs[REG_RSP];

    unw_context_t context = {};
    unw_getcontext(&context);
    unw_cursor_t cursor = {};
    unw_init_local(&cursor, &context);

    while (unw_step(&cursor) > 0) {
        unw_word_t ip = 0, sp = 0;
        // printf("DFFLFKFKFK 2\n");
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        // printf("DFFLFKFKFK 3\n");
        unw_get_reg(&cursor, UNW_REG_SP, &sp);
        printf("thhh ip %lx sp %lx\n", ip, sp);
    }

    int j, nptrs;
    void *buffer[100];
    char **strings;

    nptrs = backtrace(buffer, 100);
    printf("backtrace() returned %d addresses\n", nptrs);

    /* The call backtrace_symbols_fd(buffer, nptrs, STDOUT_FILENO)
       would produce similar output to the following: */

    strings = backtrace_symbols(buffer, nptrs);
    if (strings == NULL) {
        perror("backtrace_symbols");
        exit(EXIT_FAILURE);
    }

    for (j = 0; j < nptrs; j++)
        printf("%s\n", strings[j]);

    free(strings);

    Waiter w(done);
    w.notify();
}

Stack::Stack(uint32_t ptid)
{
    dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);

    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = handler;
    sigaction(SIGUSR1, &sa, nullptr);

    unw_context_t context = {};
    unw_getcontext(&context);
    unw_cursor_t cursor = {};
    unw_init_local(&cursor, &context);

    if (ptid > 0) {
#ifdef MTRACK_HAS_USER_REGS_STRUCT
        syscall(SYS_tkill, ptid, SIGUSR1);

        Waiter w(done);
        w.wait();

        // set regs from other thread
        unw_set_reg(&cursor, UNW_X86_64_R15, faen.r15);
        unw_set_reg(&cursor, UNW_X86_64_R14, faen.r14);
        unw_set_reg(&cursor, UNW_X86_64_R13, faen.r13);
        unw_set_reg(&cursor, UNW_X86_64_R12, faen.r12);
        unw_set_reg(&cursor, UNW_X86_64_RBP, faen.rbp);
        unw_set_reg(&cursor, UNW_X86_64_RBX, faen.rbx);
        unw_set_reg(&cursor, UNW_X86_64_R11, faen.r11);
        unw_set_reg(&cursor, UNW_X86_64_R10, faen.r10);
        unw_set_reg(&cursor, UNW_X86_64_R9, faen.r9);
        unw_set_reg(&cursor, UNW_X86_64_R8, faen.r8);
        unw_set_reg(&cursor, UNW_X86_64_RAX, faen.rax);
        unw_set_reg(&cursor, UNW_X86_64_RCX, faen.rcx);
        unw_set_reg(&cursor, UNW_X86_64_RDX, faen.rdx);
        unw_set_reg(&cursor, UNW_X86_64_RSI, faen.rsi);
        unw_set_reg(&cursor, UNW_X86_64_RDI, faen.rdi);
        unw_set_reg(&cursor, UNW_X86_64_RIP, faen.rip);
        unw_set_reg(&cursor, UNW_X86_64_RSP, faen.rsp);

        printf("got rip %lx %lx\n", faen.rip, faen.rsp);

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
