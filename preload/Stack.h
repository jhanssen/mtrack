#pragma once

#include <cstdint>
#include <vector>
#include <sys/ucontext.h>

struct StackInitializer
{
#ifdef __x86_64__
    gregset_t gregs;
#endif
};

class Stack
{
public:
    Stack();
    Stack(const StackInitializer& initializer);

    void initialize(const StackInitializer& initializer);

    bool atEnd() const { return mIndex == mPtrs.size(); }
    void next();

    uint64_t ip() const { return mPtrs[mIndex].first; }
    uint64_t sp() const { return mPtrs[mIndex].second; }
    uint64_t index() const { return mIndex; }

private:
    long long unsigned mIndex { 0 };
    std::vector<std::pair<uint64_t, uint64_t>> mPtrs;
};

inline Stack::Stack()
{
}

inline void Stack::next()
{
    if (atEnd())
        return;
    ++mIndex;
}

class ThreadStack : public Stack
{
public:
    ThreadStack(unsigned ptid);
};
