#pragma once

#include <concepts>
#include <cstdint>
#include <cstring>
#include <vector>

class Emitter
{
public:
    Emitter() = default;

    template<typename... Ts>
    void emit(Ts&&... args);

    template<typename... Ts>
    void emitWithSize(size_t size, Ts&&... args);

    template<typename... Ts>
    static inline size_t emitSize(Ts&&... args);

    size_t size() const;
    uint8_t* data();
    const uint8_t* data() const;

    void reset();

private:
    std::vector<uint8_t> mData;
    size_t mOffset { 0 };
};

namespace detail {
template<typename T>
concept HasDataSize = requires(T& t)
{
    { t.data() } -> std::same_as<const void*>;
    { t.size() } -> std::same_as<uint32_t>;
};

template<typename T>
inline size_t emitSize_helper(T&&) requires (!HasDataSize<std::decay_t<T>>)
{
    static_assert(std::is_enum_v<std::decay_t<T>> || std::is_integral_v<std::decay_t<T>>, "Must be integral or enum");
    return sizeof(T);
}

template<typename T>
inline size_t emitSize_helper(T&& str) requires HasDataSize<std::decay_t<T>>
{
    return str.size() + sizeof(uint32_t);
}

inline size_t emitSize()
{
    return 0;
}

template<typename T>
inline size_t emitSize(T&& arg)
{
    return emitSize_helper<T>(std::forward<T>(arg));
}

template<typename T, typename... Ts>
inline size_t emitSize(T&& arg, Ts&&... args)
{
    return emitSize_helper<T>(std::forward<T>(arg)) + emitSize(std::forward<Ts>(args)...);
}

template<typename T>
inline void emit_helper(uint8_t*& data, T&& arg) requires (!HasDataSize<std::decay_t<T>>)
{
    static_assert(std::is_enum_v<std::decay_t<T>> || std::is_integral_v<std::decay_t<T>>, "Must be integral or enum");
    memcpy(data, &arg, sizeof(T));
    data += sizeof(T);
}

template<typename T>
inline void emit_helper(uint8_t*& data, T&& str) requires HasDataSize<std::decay_t<T>>
{
    emit_helper(data, str.size());
    memcpy(data, str.data(), str.size());
    data += str.size();
}

template<typename T>
inline void emit(uint8_t*& data, T&& arg)
{
    emit_helper<T>(data, std::forward<T>(arg));
}

template<typename T, typename... Ts>
inline void emit(uint8_t*& data, T&& arg, Ts&&... args)
{
    emit_helper<T>(data, std::forward<T>(arg));
    emit(data, std::forward<Ts>(args)...);
}
} // namespace detail;

template<typename... Ts>
inline size_t Emitter::emitSize(Ts&&... args)
{
    return detail::emitSize(std::forward<Ts>(args)...);
}

template<typename... Ts>
inline void Emitter::emitWithSize(size_t size, Ts&&... args)
{
    if (mOffset + size > mData.size()) {
        mData.resize(mOffset + size);
    }

    uint8_t* data = mData.data() + mOffset;
    detail::emit(data, std::forward<Ts>(args)...);
    mOffset += size;
}

template<typename... Ts>
inline void Emitter::emit(Ts&&... args)
{
    const auto size = detail::emitSize(std::forward<Ts>(args)...);
    emitWithSize(size, std::forward<Ts>(args)...);
}

inline size_t Emitter::size() const
{
    return mOffset;
}

inline uint8_t* Emitter::data()
{
    return mData.data();
}

inline const uint8_t* Emitter::data() const
{
    return mData.data();
}

inline void Emitter::reset()
{
    mOffset = 0;
}
