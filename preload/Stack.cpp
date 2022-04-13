#include "Stack.h"
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

Stack::Stack(uint32_t ptid)
{
    ptrace(PTRACE_ATTACH, ptid, nullptr, nullptr);
    user_regs_struct regs;
    ptrace(PTRACE_GETREGS, ptid, nullptr, &regs);
    ptrace(PTRACE_DETACH, ptid, nullptr, nullptr);

    const auto stackStart = 0llu;
    const auto stackPtr = regs.rsp;
    const auto instrPtr = regs.rip;

    printf("got numbers %llu %llu %llu", stackStart, stackPtr, instrPtr);//, buf);
}
