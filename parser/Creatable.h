#pragma once

#include <memory>

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
        return ptr;
    }
};
