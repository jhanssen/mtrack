#pragma once

#include <map>
#include <array>
#include <string>
#include <cstdint>
#include <sys/ucontext.h>

class Stack
{
public:
    enum { MaxFrames = 255 };

    Stack(unsigned skip);

    uint64_t const* ptrs() const { return mPtrs.data(); }
    const uint64_t* data() const { return mPtrs.data(); }
    uint32_t size() const { return mCount * sizeof(uint64_t); } // in bytes

    void setPtr(size_t i, uint64_t p) { mPtrs[i] = p; };
    const std::string &url(size_t i) const { return mUrls[i]; }
    uint32_t count() const { return mCount; }

private:
    Stack(const Stack &) = delete;
    Stack &operator=(const Stack &) = delete;

    uint32_t mCount { 0 };
    std::array<std::string, MaxFrames> mUrls;
    std::array<uint64_t, MaxFrames> mPtrs;
};
