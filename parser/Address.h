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

template <typename T>
struct Address
{
    uint64_t ip {};
    Frame<T> frame;
    std::vector<Frame<T>> inlined;

    bool valid() const;

    bool operator==(const Address&) const = default;
};

template <typename T>
inline bool Address<T>::valid() const
{
    return frame.function != frameDefault<T>();
}

struct UnresolvedAddress
{
    uint64_t ip {};
    backtrace_state* state {};
};
