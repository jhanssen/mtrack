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
        {
            ScopedSpinlock lock(r->mLock);

            // printf("loop %u\n", r->mOffset);

            if (r->mOffset == 0 && !r->mRunning.load(std::memory_order_acquire)) {
                fclose(r->mFile);
                r->mFile = nullptr;
                return;
            }

            if (r->mOffset > 0) {
                const auto now = timestamp() - start;
                memcpy(&timeData[1], &now, sizeof(now));
                if (fwrite(timeData, std::size(timeData), 1, r->mFile) != 1) {
                    printf("recorder ts error\n");
                    fclose(r->mFile);
                    r->mFile = nullptr;
                    return;
                }

                if (fwrite(&r->mData[0], r->mOffset, 1, r->mFile) != 1) {
                    printf("recorder data error\n");
                    fclose(r->mFile);
                    r->mFile = nullptr;
                    return;
                }
                fflush(r->mFile);
                r->mOffset = 0;
            }
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
    mRunning.store(true, std::memory_order_release);

    mThread = std::thread(Recorder::process, this);
}
