#include "Stack.h"
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

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

#ifdef MTRACK_HAS_USER_REGS_STRUCT
    const auto stackStart = 0llu;
    const auto stackPtr = regs.rsp;
    const auto instrPtr = regs.rip;

    printf("got numbers %llu %llu %llu\n", stackStart, stackPtr, instrPtr);//, buf);
#else
    for (size_t i=0; i<sizeof(regs.uregs) / sizeof(regs.uregs[0]); ++i) {
        printf("got numbers %zu %lu\n", i, regs.uregs[i]);
    }
#endif
}
