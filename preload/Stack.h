#pragma once

#include <cstdint>
#include <array>
#include <sys/ucontext.h>

class Stack
{
public:
    enum { MaxFrames = 512 };

#ifdef __APPLE__
    Stack();
#else
    Stack(unsigned ptid = 0);
#endif

    const void *data() const { return mPtrs.data(); }
    uint32_t size() const { return mCount * sizeof(void*); }

private:
#ifndef __APPLE__
    struct StackInitializer
    {
        gregset_t gregs;
    };
    inline void initialize(const StackInitializer& initializer);
#endif

    Stack(const Stack &) = delete;
    Stack &operator=(const Stack &) = delete;

    uint32_t mCount { 0 };
    std::array<void *, MaxFrames> mPtrs;
};
