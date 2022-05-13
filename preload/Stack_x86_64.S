#define UC_MCONTEXT_GREGS_RBP   0x78
#define UC_MCONTEXT_GREGS_RBX   0x80
#define UC_MCONTEXT_GREGS_RSP   0xa0
#define UC_MCONTEXT_GREGS_RIP   0xa8

        .global mtrack_x86_64_getcontext_trace
        .type mtrack_x86_64_getcontext_trace, @function
mtrack_x86_64_getcontext_trace:
        .cfi_startproc

        /* Save only RBP, RBX, RSP, RIP - exclude this call. */
        movq %rbp, UC_MCONTEXT_GREGS_RBP(%rdi)
        movq %rbx, UC_MCONTEXT_GREGS_RBX(%rdi)

        leaq 8(%rsp), %rax
        movq %rax, UC_MCONTEXT_GREGS_RSP(%rdi)

        movq 0(%rsp), %rax
        movq %rax, UC_MCONTEXT_GREGS_RIP(%rdi)

        xorq	%rax, %rax
        retq
        .cfi_endproc
        .size mtrack_x86_64_getcontext_trace, . - mtrack_x86_64_getcontext_trace

      /* We do not need executable stack.  */
      .section        .note.GNU-stack,"",@progbits
