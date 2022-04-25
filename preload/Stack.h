#pragma once

#include <cstdint>
#include <array>
#include <sys/ucontext.h>

struct StackInitializer
{
#if defined(__x86_64__) || defined(__i386__)
    gregset_t gregs;
#endif
};

class Stack
{
public:
    enum { MaxFrames = 512 };

    Stack(unsigned ptid);

    bool atEnd() const { return mIndex == mCount; }
    void next();

    // not sure why but ip is consistently one past where I need it to be
    uint64_t ip() const { return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(mPtrs[mIndex])) - 1; }
    uint64_t index() const { return mIndex; }

private:
    Stack(const Stack &) = delete;
    Stack &operator=(const Stack &) = delete;

    inline void initialize(const StackInitializer& initializer);

    size_t mIndex { 0 };
    size_t mCount { 0 };
    std::array<void *, MaxFrames> mPtrs;
};

inline void Stack::next()
{
    if (atEnd())
        return;
    ++mIndex;
}
