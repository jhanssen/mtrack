#pragma once

#include <cstdint>
#include <array>
#include <sys/ucontext.h>

class Stack
{
public:
    enum { MaxFrames = 255 };

    Stack(unsigned skip);

    uint64_t const* ptrs() const { return mPtrs.data(); }
    const uint64_t* data() const { return mPtrs.data(); }
    uint32_t size() const { return mCount * sizeof(uint64_t); }

private:
    Stack(const Stack &) = delete;
    Stack &operator=(const Stack &) = delete;

    uint32_t mCount { 0 };
    std::array<uint64_t, MaxFrames> mPtrs;
};
