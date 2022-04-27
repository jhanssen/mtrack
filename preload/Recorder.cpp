#include "Recorder.h"
#include <unistd.h>

thread_local bool Recorder::tScoped = false;

void Recorder::process(Recorder* r)
{
    enum { SleepInterval = 250 };

    uint8_t timeData[5];

    auto timestamp = []() {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
        return static_cast<uint32_t>((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
    };

    const auto start = timestamp();
    timeData[0] = static_cast<uint8_t>(RecordType::Time);

    for (;;) {
        const char *error = nullptr;
        {
            ScopedSpinlock lock(r->mLock);

            // printf("loop %u\n", r->mOffset);

            if (r->mOffset > 0) {
                const auto now = timestamp() - start;
                memcpy(&timeData[1], &now, sizeof(now));
                if (fwrite(timeData, std::size(timeData), 1, r->mFile) != 1) {
                    error = "recorder ts error\n";
                } else if (fwrite(&r->mData[0], r->mOffset, 1, r->mFile) != 1) {
                    printf("recorder data error\n");
                } else {
                    fflush(r->mFile);
                    r->mOffset = 0;
                }
            }
        }

        if (error || !r->mRunning.load(std::memory_order_acquire)) {
            fclose(r->mFile);
            r->mFile = nullptr;
            return;
        }

        usleep(SleepInterval * 1000);
    }

    ScopedSpinlock lock(r->mLock);
    fclose(r->mFile);
    r->mFile = nullptr;
}

void Recorder::initialize(const char* file)
{
    mFile = fopen(file, "w");
    if (!mFile) {
        fprintf(stderr, "Unable to open recorder file %s %d %m\n", file, errno);
        abort();
    }

    const uint32_t version = static_cast<uint32_t>(FileVersion::Current);
    if (fwrite(&version, sizeof(uint32_t), 1, mFile) != 1) {
        fprintf(stderr, "Failed to write file version to %s %d %m\n", file, errno);
        abort();
    }

    mRunning.store(true, std::memory_order_release);

    mThread = std::thread(Recorder::process, this);
}
