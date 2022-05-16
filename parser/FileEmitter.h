#pragma once

#include <common/Emitter.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

extern "C" struct z_stream_s;

class FileEmitter : public Emitter
{
public:
    enum WriteMode { Uncompressed, GZip };

    FileEmitter() = default;
    FileEmitter(const std::string& file, WriteMode writeMode)
    {
        setFile(file, writeMode);
    }
    ~FileEmitter();

    uint64_t offset() const { return mOffset; }

    void setFile(const std::string& file, WriteMode writeMode);

    virtual void writeBytes(const void* data, size_t size, WriteType type) override;

private:
    enum { InBufferSize = 32768, OutBufferSize = 1024 * 1024 };

    void flush(bool finalize);

private:
    uint64_t mOffset {};
    FILE* mFile { nullptr };
    uint32_t mBufferOffset {};
    std::array<uint8_t, InBufferSize> mBuffer;
    z_stream_s* mZStream = nullptr;
};
