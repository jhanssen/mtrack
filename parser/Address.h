#pragma once

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
struct backtrace_state;
};

template <typename T>
T frameDefault();

template <>
inline std::string frameDefault()
{
    return {};
}

template <>
inline constexpr int32_t frameDefault()
{
    return -1;
}

template <typename T>
struct Frame
{
    T function { frameDefault<T>() };
    T file { frameDefault<T>() };
    int32_t line { frameDefault<int32_t>() };

    bool operator==(const Frame& other) const
    {
        return function == other.function && file == other.file && line == other.line;
    }
};

struct InstructionPointer
{
    uint8_t aid {};
    uint64_t ip {};

    int compare(const InstructionPointer &other) const
    {
        if(aid < other.aid)
            return -1;
        if(aid > other.aid)
            return 1;
        if(ip < other.ip)
            return -1;
        if(ip > other.ip)
            return 1;
        return 0;
    }
    bool operator==(const InstructionPointer &other) const
    {
        return !compare(other);
    }
};
static inline bool operator<(const InstructionPointer &l, const InstructionPointer &r)
{
    return l.compare(r) < 1;
}
static inline bool operator>(const InstructionPointer &l, const InstructionPointer &r)
{
    return l.compare(r) > 1;
}
static inline bool operator>=(const InstructionPointer &l, const InstructionPointer &r)
{
    return l.compare(r) >= 0;
}
static inline bool operator<=(const InstructionPointer &l, const InstructionPointer &r)
{
    return l.compare(r) <= 0;
}

template <typename T>
struct Address : public InstructionPointer
{
    Frame<T> frame;
    std::vector<Frame<T>> inlined;

    bool valid() const;

    bool operator==(const Address &other) const
    {
        return InstructionPointer::operator==(other) && frame == other.frame && inlined == other.inlined;
    }
};

template <typename T>
inline bool Address<T>::valid() const
{
    return frame.function != frameDefault<T>();
}

struct UnresolvedAddress : public InstructionPointer
{
    backtrace_state* state {};
};
