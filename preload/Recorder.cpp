#include "Recorder.h"
#include <unistd.h>

thread_local bool Recorder::tScoped = false;

void Recorder::process(Recorder* r)
{
    enum { SleepInterval = 250 };

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
                if (fwrite(&r->mData[0], r->mOffset, 1, r->mFile) != 1) {
                    printf("recorder error\n");
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
