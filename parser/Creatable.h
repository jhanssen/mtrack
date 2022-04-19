#pragma once

#include <memory>
#include <concepts>

namespace detail {
template<typename T>
concept HasInitPrivate = requires(T& t)
{
    { t.init() } -> std::same_as<void>;
};

template<typename T>
class InitCaller : public T
{
public:
    void init()
    {
        T::init();
    }
};
} // namespace detail

template<typename T>
class HasInitTester
{
public:
    constexpr static bool hasInit = detail::HasInitPrivate<T>;
};

template<typename T>
concept HasInit = HasInitTester<T>::hasInit;

template<typename T>
class Creatable
{
public:
    template<typename... Args>
    static std::shared_ptr<T> create(Args&& ...args)
    {
        struct EnableMakeShared : public T
        {
        public:
            EnableMakeShared(Args&& ...args)
                : T(std::forward<Args>(args)...)
            {
            }
        };

        auto ptr = std::make_shared<EnableMakeShared>(std::forward<Args>(args)...);
        if constexpr (HasInit<T>) {
            reinterpret_cast<detail::InitCaller<T>*>(ptr.get())->init();
        }

        return ptr;
    }
};
