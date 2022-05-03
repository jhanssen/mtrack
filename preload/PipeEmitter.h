#pragma once

#include "NoHook.h"
#include "Spinlock.h"
#include <common/Emitter.h>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <unistd.h>

#ifndef PIPE_BUF
#define PIPE_BUF 4096
#endif

class PipeEmitter : public Emitter
{
public:
    PipeEmitter() = default;
    PipeEmitter(int pipe)
        : mPipe(pipe)
    {
    }

    void setPipe(int pipe)
    {
        mPipe = pipe;
    }

    virtual void writeBytes(const void* data, size_t size, WriteType type) override;

private:
    NoHook mNoHook;
    int mPipe { -1 };

    uint8_t mBuf[PIPE_BUF];
    size_t mOffset { 0 };
};

void PipeEmitter::writeBytes(const void* data, size_t size, WriteType type)
{
    if (mOffset + size > sizeof(mBuf)) {
        fprintf(stderr, "packet too large %zu (%zu + %zu) > %zu\n", mOffset + size, mOffset, size, sizeof(mBuf));
        abort();
    }
    ::memcpy(mBuf + mOffset, data, size);
    mOffset += size;
    if (type == WriteType::Last) {
        if (::write(mPipe, mBuf, mOffset) != mOffset) {
            fprintf(stderr, "Failed to write %zu bytes to pipe %m\n", mOffset);
        }
        mOffset = 0;
    }
}
