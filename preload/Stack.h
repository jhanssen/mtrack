#pragma once

#include <cstdint>
#include <array>
#include <sys/ucontext.h>

class Stack
{
public:
    enum { MaxFrames = 512 };

    Stack(unsigned ptid = 0);

    const void *data() const { return mPtrs.data(); }
    uint32_t size() const { return mCount * sizeof(void*); }

private:
    struct StackInitializer
    {
#if defined(__x86_64__) || defined(__i386__)
        gregset_t gregs;
#endif
    };

    Stack(const Stack &) = delete;
    Stack &operator=(const Stack &) = delete;

    inline void initialize(const StackInitializer& initializer);

    uint32_t mCount { 0 };
    std::array<void *, MaxFrames> mPtrs;
};
