#include "FileEmitter.h"
#include <cassert>
#include <zlib.h>

FileEmitter::~FileEmitter()
{
    if (mZStream != nullptr) {
        flush(true);
        deflateEnd(mZStream);
        delete mZStream;
    }

    if (mFile != nullptr) {
        fclose(mFile);
    }
}

void FileEmitter::setFile(const std::string& file, WriteMode writeMode)
{
    if (mZStream != nullptr) {
        flush(true);
        deflateEnd(mZStream);
        delete mZStream;
    }

    if (mFile != nullptr) {
        fclose(mFile);
    }

    if (writeMode == WriteMode::GZip) {
        mZStream = new z_stream;
        mZStream->zalloc = nullptr;
        mZStream->zfree = nullptr;
        mZStream->opaque = nullptr;
        deflateInit2(mZStream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     15 | 16, 8, Z_DEFAULT_STRATEGY);
    } else {
        mZStream = nullptr;
    }

    mFile = fopen(file.c_str(), "w");
}

void FileEmitter::flush(bool finalize)
{
    if ((!mZStream || !finalize) && mBufferOffset == 0)
        return;

    if (mZStream) {
        uint8_t out[OutBufferSize];
        mZStream->next_in = mBuffer.data();
        mZStream->avail_in = mBufferOffset;

        int32_t have;
        for (;;) {
            mZStream->avail_out = OutBufferSize;
            mZStream->next_out = out;
            deflate(mZStream, finalize ? Z_FINISH : Z_NO_FLUSH);
            have = OutBufferSize - mZStream->avail_out;
            if (have == 0)
                break;

            const auto written = fwrite(out, have, 1, mFile);
            if (written != 1) {
                fprintf(stderr, "file write error %d %m (%d vs %lu)\n", errno, have, written);
                abort();
            }
        }
    } else {
        const auto written = fwrite(mBuffer.data(), mBufferOffset, 1, mFile);
        if (written != 1) {
            fprintf(stderr, "file write error %d %m (%d vs %lu)\n", errno, mBufferOffset, written);
            abort();
        }
    }
}

void FileEmitter::writeBytes(const void* data, size_t size, WriteType)
{
    assert(mBufferOffset > 0 || mBufferOffset + size <= InBufferSize);
    if (mBufferOffset + size <= InBufferSize) {
        memcpy(mBuffer.data() + mBufferOffset, data, size);
        mBufferOffset += size;
        return;
    }

    flush(false);

    memcpy(mBuffer.data(), data, size);
    mBufferOffset = size;

    mOffset += size;
}
