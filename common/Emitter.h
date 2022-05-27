#pragma once

#include <concepts>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

namespace detail {
template<typename T>
concept HasDataSize = requires(T& t)
{
    { t.data() } -> std::same_as<const void*>;
    { t.size() } -> std::same_as<uint32_t>;
};
} // namespace detail

class Emitter
{
public:
    enum class WriteType {
        Continuation,
        Last
    };

    Emitter() = default;
    virtual ~Emitter() = default;

    template<typename T>
    size_t emit(T&& arg);

    template<typename T, typename... Ts>
    size_t emit(T&& arg, Ts&&... args);

    template<typename... Ts>
    size_t emit(Ts&&... args);

    template<typename... Ts>
    size_t emitWithSize(size_t size, Ts&&... args);

    template<typename... Ts>
    static inline size_t emitSize(Ts&&... args);

    struct String
    {
        String(const char* s);
        String(const char* s, uint32_t sz);
        String(const std::string& s);

        const void* data() const { return str; }
        uint32_t size() const { return len; }

        const char* const str { nullptr };
        const uint32_t len { 0 };
    };

    struct Data
    {
        Data(const void* d, uint32_t sz);

        const void* data() const { return dta; }
        uint32_t size() const { return siz; }

        const void* const dta { nullptr };
        const uint32_t siz { 0 };
    };

    virtual void writeBytes(const void* data, size_t size, WriteType type) = 0;

private:
    template<typename T>
    size_t emitSize_helper(T&&) requires (!detail::HasDataSize<std::decay_t<T>>);

    template<typename T>
    size_t emitSize_helper(T&& str) requires detail::HasDataSize<std::decay_t<T>>;

    size_t emitSize();

    template<typename T>
    size_t emitSize(T&& arg);

    template<typename T, typename... Ts>
    size_t emitSize(T&& arg, Ts&&... args);

    template<typename T>
    size_t emit_helper(T&& arg, WriteType type) requires (!detail::HasDataSize<std::decay_t<T>>);

    template<typename T>
    size_t emit_helper(T&& str, WriteType type) requires detail::HasDataSize<std::decay_t<T>>;
};

template<typename T>
inline size_t Emitter::emitSize_helper(T&&) requires (!detail::HasDataSize<std::decay_t<T>>)
{
    static_assert(std::is_enum_v<std::decay_t<T>> || std::is_arithmetic_v<std::decay_t<T>>, "Must be arithmetic or enum");
    return sizeof(T);
}

template<typename T>
inline size_t Emitter::emitSize_helper(T&& str) requires detail::HasDataSize<std::decay_t<T>>
{
    return str.size() + sizeof(uint32_t);
}

inline size_t Emitter::emitSize()
{
    return 0;
}

template<typename T>
inline size_t Emitter::emitSize(T&& arg)
{
    return emitSize_helper<T>(std::forward<T>(arg));
}

template<typename T, typename... Ts>
inline size_t Emitter::emitSize(T&& arg, Ts&&... args)
{
    return emitSize_helper<T>(std::forward<T>(arg)) + emitSize(std::forward<Ts>(args)...);
}

template<typename T>
inline size_t Emitter::emit_helper(T&& arg, WriteType type) requires (!detail::HasDataSize<std::decay_t<T>>)
{
    static_assert(std::is_enum_v<std::decay_t<T>> || std::is_arithmetic_v<std::decay_t<T>>, "Must be arithmetic or enum");
    writeBytes(&arg, sizeof(T), type);
    return sizeof(T);
}

template<typename T>
inline size_t Emitter::emit_helper(T&& str, WriteType type) requires detail::HasDataSize<std::decay_t<T>>
{
    const size_t ret = emit_helper(str.size(), WriteType::Continuation);
    if (str.size() == 0)
        return ret;
    writeBytes(str.data(), str.size(), type);
    return ret + str.size();
}

template<typename T>
inline size_t Emitter::emit(T&& arg)
{
    return emit_helper<T>(std::forward<T>(arg), WriteType::Last);
}

template<typename T, typename... Ts>
inline size_t Emitter::emit(T&& arg, Ts&&... args)
{
    return emit_helper<T>(std::forward<T>(arg), WriteType::Continuation) + emit(std::forward<Ts>(args)...);
}

template<typename... Ts>
inline size_t Emitter::emitSize(Ts&&... args)
{
    return emitSize(std::forward<Ts>(args)...);
}

template<typename... Ts>
inline size_t Emitter::emitWithSize(size_t /* size */, Ts&&... args)
{
    return emit(std::forward<Ts>(args)...);
}

template<typename... Ts>
inline size_t Emitter::emit(Ts&&... args)
{
    const auto size = emitSize(std::forward<Ts>(args)...);
    return emitWithSize(size, std::forward<Ts>(args)...);
}

inline Emitter::String::String(const char* s)
    : str(s), len(static_cast<uint32_t>(strlen(s)))
{
}

inline Emitter::String::String(const char* s, uint32_t sz)
    : str(s), len(sz)
{
}

inline Emitter::String::String(const std::string& s)
    : str(s.c_str()), len(static_cast<uint32_t>(s.size()))
{
}

inline Emitter::Data::Data(const void* d, uint32_t sz)
    : dta(d), siz(sz)
{
}
