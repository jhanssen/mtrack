#pragma once

#include "Stack.h"

class StackWalker
{
public:
    template<typename Fun>
    static void walk(Stack& stack, Fun&& fun)
    {
        while (!stack.atEnd()) {
            fun(stack.ip(), stack.index());
            stack.next();
        }
    }
};
