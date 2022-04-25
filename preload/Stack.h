#pragma once

#include <cstdint>
#include <vector>
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

    long long unsigned mIndex { 0 };
    std::vector<uint64_t> mPtrs;
};

inline void Stack::next()
{
    if (atEnd())
        return;
    ++mIndex;
}
