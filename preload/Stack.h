#pragma once

#include <cstdint>

class Stack
{
public:
    Stack(uint32_t ptid);

    bool atEnd() const { return mAtEnd; }
    void next();

private:
    bool mAtEnd { true };
};
