#pragma once

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

    long long unsigned ip() const { return mPtrs[mIndex].first; }
    long long unsigned sp() const { return mPtrs[mIndex].second; }
    long long unsigned index() const { return mIndex; }

private:
    long long unsigned mIndex { 0 };
    std::vector<std::pair<long long unsigned, long long unsigned>> mPtrs;
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
