#pragma once

#include <cstdint>
#include <vector>

class Stack
{
public:
    Stack(uint32_t ptid);

    bool atEnd() const { return mIndex == mPtrs.size(); }
    void next();

    uint64_t ip() const { return mPtrs[mIndex].first; }
    uint64_t sp() const { return mPtrs[mIndex].second; }
    uint64_t index() const { return mIndex; }

private:
    uint64_t mIndex { 0 };
    std::vector<std::pair<uint64_t, uint64_t>> mPtrs;
};

inline void Stack::next()
{
    if (atEnd())
        return;
    ++mIndex;
}
