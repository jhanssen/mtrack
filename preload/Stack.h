#pragma once

#include <cstdint>
#include <array>
#include <sys/ucontext.h>

class Stack
{
public:
    enum { MaxFrames = 255 };

    Stack(unsigned skip, unsigned ptid = 0);

    void* const* ptrs() const { return mPtrs.data(); }
    const void* data() const { return mPtrs.data(); }
    uint32_t size() const { return mCount * sizeof(void*); }

    static void setNoMmap() { sNoMmap = true; }

private:
    Stack(const Stack &) = delete;
    Stack &operator=(const Stack &) = delete;

    uint32_t mCount { 0 };
    std::array<void *, MaxFrames> mPtrs;

    static bool sNoMmap;
};
