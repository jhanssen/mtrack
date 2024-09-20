#pragma once

#include "NoHook.h"
#include <common/Emitter.h>

class HostEmitter : public Emitter
{
public:
    virtual void writeBytes(const void* data, size_t size, WriteType type) override;

private:
    void commitBytes();
    NoHook mNoHook;

    uint8_t mBuf[PIPE_BUF];
    size_t mOffset { 0 };
};

void HostEmitter::writeBytes(const void* data, size_t size, WriteType type)
{
    if (mOffset + size > sizeof(mBuf)) {
        fprintf(stderr, "packet too large %zu (%zu + %zu) > %zu\n", mOffset + size, mOffset, size, sizeof(mBuf));
        abort();
    }
    ::memcpy(mBuf + mOffset, data, size);
    mOffset += size;
    if (type == WriteType::Last) {
        commitBytes();
        mOffset = 0;
    }
}

#ifdef __EMSCRIPTEN__
extern "C" {
void *mtrack_writeBytes(const void *data, size_t size);
}
void HostEmitter::commitBytes()
{
    mtrack_writeBytes(mBuf, mOffset);
}
#else
#error "Need to implement"
#endif
