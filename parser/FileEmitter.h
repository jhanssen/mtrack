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
    enum WriteMode { None = 0x0, GZip = 0x1, Base64 = 0x2 };

    FileEmitter() = default;
    FileEmitter(FILE* file, uint8_t writeMode)
    {
        setFile(file, writeMode);
    }
    ~FileEmitter();

    void cleanup();

    uint64_t offset() const { return mOffset; }

    void setFile(FILE* file, uint8_t writeMode);

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
