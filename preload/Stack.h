#pragma once

#include <cstdint>
#include <array>
#include <sys/ucontext.h>

class Stack
{
public:
    enum { MaxFrames = 255 };

    Stack(unsigned ptid = 0);

    void* const* ptrs() const { return mPtrs.data(); }
    const void* data() const { return mPtrs.data(); }
    uint32_t size() const { return mCount * sizeof(void*); }

private:
    Stack(const Stack &) = delete;
    Stack &operator=(const Stack &) = delete;

    uint32_t mCount { 0 };
    std::array<void *, MaxFrames> mPtrs;
};
