#pragma once

#include <common/Emitter.h>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <unistd.h>

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
        ::write(mPipe, mBuf, mOffset);
        mOffset = 0;
    }
}
