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
    Stack(unsigned ptid);

    bool atEnd() const { return mIndex == mPtrs.size(); }
    void next();

    uint64_t ip() const { return mPtrs[mIndex]; }
    uint64_t index() const { return mIndex; }

private:
    Stack(const Stack &) = delete;
    Stack &operator=(const Stack &) = delete;

    Stack(const StackInitializer& initializer);
    void initialize(const StackInitializer& initializer);

    enum { MaxFrames = 512 };
    size_t mIndex { 0 };
    size_t mCount { 0 };
    std::array<uint64_t, MaxFrames> mPtrs;
};

inline void Stack::next()
{
    if (atEnd())
        return;
    ++mIndex;
}
