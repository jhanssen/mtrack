#include "Stack.h"

#ifdef __EMSCRIPTEN__

extern "C" {
void *emscripten_stack_snapshot();
uint32_t emscripten_stack_unwind_buffer(const void *pc, void *buffer, uint32_t depth);
}

Stack::Stack(unsigned skip) : mCount(0)
{
    const void *pc = emscripten_stack_snapshot();
    std::array<void *, MaxFrames> frames;
    mCount = emscripten_stack_unwind_buffer(pc, frames.data(), MaxFrames);
    frames[0] = (void*)pc;
    for(size_t i = 0; i < mCount; ++i)
        mPtrs[i] = (uint64_t)frames[i];
}

#else

#include <asan_unwind.h>

Stack::Stack(unsigned skip) : mCount(0)
{
    asan_unwind::StackTrace st(mPtrs.data(), MaxFrames);
    mCount = st.unwindSlow(skip);
}
#endif
