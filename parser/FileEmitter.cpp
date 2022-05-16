#include "FileEmitter.h"
#include "base64.h"
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

    if (writeMode == WriteMode::GZip || writeMode == WriteMode::Html) {
        mZStream = new z_stream;
        mZStream->zalloc = nullptr;
        mZStream->zfree = nullptr;
        mZStream->opaque = nullptr;
        deflateInit2(mZStream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     15 | 16, 8, Z_DEFAULT_STRATEGY);
        mBase64 = writeMode == WriteMode::Html;
    } else {
        mZStream = nullptr;
        mBase64 = false;
    }

    mFile = fopen(file.c_str(), "w");
}

void FileEmitter::flush(bool finalize)
{
    if ((!mZStream || !finalize) && mBufferOffset == 0)
        return;

    if (mZStream) {
        uint8_t out[BufferSize];
        uint8_t b64[BufferSize * 2];
        mZStream->next_in = mBuffer.data();
        mZStream->avail_in = mBufferOffset;

        int32_t have;
        for (;;) {
            mZStream->avail_out = BufferSize;
            mZStream->next_out = out;
            deflate(mZStream, finalize ? Z_FINISH : Z_NO_FLUSH);
            have = BufferSize - mZStream->avail_out;
            if (have == 0)
                break;

            if (mBase64) {
                // printf("deflated %u to %d\n", mBufferOffset, have);

                uint64_t outOffset = 0;
                if (mNumBBuffer > 0) {
                    if (mNumBBuffer == 1) {
                        mBBuffer[1] = out[0];
                        --have;
                        ++outOffset;

                        if (have >= 1) {
                            mBBuffer[2] = out[1];
                            --have;
                            ++outOffset;
                        } else {
                            mNumBBuffer = 2;
                            break;
                        }
                    } else if (mNumBBuffer == 2) {
                        mBBuffer[2] = out[0];
                        --have;
                        ++outOffset;
                    }

                    const ssize_t blen = base64_encode(mBBuffer, 3, b64, sizeof(b64), false);
                    const auto written = fwrite(b64, blen, 1, mFile);
                    if (written != 1) {
                        fprintf(stderr, "file write error %d %m (%d vs %lu)\n", errno, have, written);
                        abort();
                    }

                    mBOffset += blen;
                    mNumBBuffer = 0;
                }

                const auto have3 = have % 3;
                if (have3 == 1) {
                    mBBuffer[0] = (out + outOffset)[have - 1];
                    mNumBBuffer = 1;
                    --have;
                } else if (have3 == 2) {
                    mBBuffer[0] = (out + outOffset)[have - 2];
                    mBBuffer[1] = (out + outOffset)[have - 1];
                    mNumBBuffer = 2;
                    have -= 2;
                }

                if (have == 0)
                    break;

                assert(!(have % 3));
                const ssize_t blen = base64_encode(out + outOffset, have, b64, sizeof(b64), false);
                const auto written = fwrite(b64, blen, 1, mFile);
                if (written != 1) {
                    fprintf(stderr, "file write error %d %m (%d vs %lu)\n", errno, have, written);
                    abort();
                }

                mBOffset += blen;
            } else {
                const auto written = fwrite(out, have, 1, mFile);
                if (written != 1) {
                    fprintf(stderr, "file write error %d %m (%d vs %lu)\n", errno, have, written);
                    abort();
                }
            }
        }

        if (mBase64 && finalize && mNumBBuffer) {
            const ssize_t blen = base64_encode(mBBuffer, mNumBBuffer, b64, sizeof(b64), true);
            const auto written = fwrite(b64, blen, 1, mFile);
            if (written != 1) {
                fprintf(stderr, "file write error %d %m (%d vs %lu)\n", errno, have, written);
                abort();
            }
            mNumBBuffer = 0;
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
    assert(mBufferOffset > 0 || mBufferOffset + size <= BufferSize);
    if (mBufferOffset + size <= BufferSize) {
        memcpy(mBuffer.data() + mBufferOffset, data, size);
        mBufferOffset += size;
        return;
    }

    flush(false);

    memcpy(mBuffer.data(), data, size);
    mBufferOffset = size;

    mOffset += size;
}
