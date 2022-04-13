#include "Stack.h"
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

Stack::Stack(uint32_t ptid)
{
    ptrace(PTRACE_ATTACH, ptid, nullptr, nullptr);
#ifdef MTRACK_HAS_USER_REGS_STRUCT
    user_regs_struct regs;
#else
    user_regs regs;
#endif
    memset(&regs, 0, sizeof(regs));
    ptrace(PTRACE_GETREGS, ptid, nullptr, &regs);
    ptrace(PTRACE_DETACH, ptid, nullptr, nullptr);
    printf("ball-1\n");

    unw_context_t context = {};
    unw_getcontext(&context);
    printf("hallo\n");
    unw_cursor_t cursor = {};
    unw_init_local(&cursor, &context);

    printf("ball0\n");

    if (ptid > 0) {
#ifdef MTRACK_HAS_USER_REGS_STRUCT
        user_regs_struct regs;
        ptrace(PTRACE_ATTACH, ptid, nullptr, nullptr);
        ptrace(PTRACE_GETREGS, ptid, nullptr, &regs);
        ptrace(PTRACE_DETACH, ptid, nullptr, nullptr);

        // set regs from other thread
        unw_set_reg(&cursor, UNW_X86_64_R15, regs.r15);
        unw_set_reg(&cursor, UNW_X86_64_R14, regs.r14);
        unw_set_reg(&cursor, UNW_X86_64_R13, regs.r13);
        unw_set_reg(&cursor, UNW_X86_64_R12, regs.r12);
        unw_set_reg(&cursor, UNW_X86_64_RBP, regs.rbp);
        unw_set_reg(&cursor, UNW_X86_64_RBX, regs.rbx);
        unw_set_reg(&cursor, UNW_X86_64_R11, regs.r11);
        unw_set_reg(&cursor, UNW_X86_64_R10, regs.r10);
        unw_set_reg(&cursor, UNW_X86_64_R9, regs.r9);
        unw_set_reg(&cursor, UNW_X86_64_R8, regs.r8);
        unw_set_reg(&cursor, UNW_X86_64_RAX, regs.rax);
        unw_set_reg(&cursor, UNW_X86_64_RCX, regs.rcx);
        unw_set_reg(&cursor, UNW_X86_64_RDX, regs.rdx);
        unw_set_reg(&cursor, UNW_X86_64_RSI, regs.rsi);
        unw_set_reg(&cursor, UNW_X86_64_RDI, regs.rdi);
        unw_set_reg(&cursor, UNW_X86_64_RIP, regs.rip);
        unw_set_reg(&cursor, UNW_X86_64_RSP, regs.rsp);
#else
        for (size_t i=0; i<sizeof(regs.uregs) / sizeof(regs.uregs[0]); ++i) {
            printf("got numbers %zu %lu\n", i, regs.uregs[i]);
        }
#endif
    }

    printf("ball1\n");
    while (unw_step(&cursor) > 0) {
        unw_word_t ip = 0;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        if (ip > 0) {
            mIps.push_back(ip);
        } else {
            break;
        }
    }
}
