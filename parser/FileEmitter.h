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
    enum WriteMode { Uncompressed, GZip, Html };

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
    enum { BufferSize = 32768 };

    void flush(bool finalize);

private:
    uint64_t mOffset {}, mBOffset {};
    FILE* mFile { nullptr };
    uint32_t mBufferOffset {};
    std::array<uint8_t, BufferSize> mBuffer;
    z_stream_s* mZStream = nullptr;
    uint8_t mBBuffer[3] {};
    uint8_t mNumBBuffer {};
    bool mBase64 {};
};
