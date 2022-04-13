#pragma once

#include <cstdint>
#include <vector>

class Stack
{
public:
    Stack(uint32_t ptid);

    bool atEnd() const { return mIndex == mIps.size(); }
    void next();

    uint64_t ip() const { return mIps[mIndex]; }
    uint64_t index() const { return mIndex; }

private:
    uint64_t mIndex { 0 };
    std::vector<uint64_t> mIps;
};

inline void Stack::next()
{
    if (atEnd())
        return;
    ++mIndex;
}
