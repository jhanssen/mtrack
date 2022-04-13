#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <stdio.h>

#include <atomic>
#include <thread>

#include "Stack.h"

#define PAGESIZE 4096

typedef void* (*MmapSig)(void*, size_t, int, int, int, off_t);

struct Data
{
    std::thread thr;
    std::atomic<bool> ok = false;

    std::atomic_flag isInitialized = ATOMIC_FLAG_INIT;
    MmapSig mmap = nullptr;
    int ffd = -1;
};

static Data data;

static void faultThread()
{
    pollfd evt = { .fd = data.ffd, .events = POLLIN };
    while (poll(&evt, 1, 1000) > 0) {
        printf("- top of fault thread\n");
        if (evt.revents & (POLLERR | POLLHUP)) {
            // done?
            close(data.ffd);
            data.ffd = -1;
            return;
        }
        uffd_msg fault_msg = {0};
        if (read(data.ffd, &fault_msg, sizeof(fault_msg)) != sizeof(fault_msg)) {
            // read error
            close(data.ffd);
            data.ffd = -1;
            return;
        }
        switch (fault_msg.event) {
        case UFFD_EVENT_PAGEFAULT: {
            const auto place = fault_msg.arg.pagefault.address;
            const auto ptid = fault_msg.arg.pagefault.feat.ptid;
            printf("  - pagefault %u\n", ptid);
            Stack stack(ptid);
            uffdio_zeropage zero = {
                .range = {
                    .start = place,
                    .len = PAGESIZE
                }
            };
            // sleep(1);
            if (ioctl(data.ffd, UFFDIO_ZEROPAGE, &zero)) {
                // boo
                close(data.ffd);
                data.ffd = -1;
                return;
            }
            printf("  - handled pagefault\n");
            break; }
        }
    }
}

static void cleanupMmap()
{
    // might be a race here if the process exits really quickly
    if (data.ok) {
        assert(data.ffd != -1);
        close(data.ffd);
        data.thr.join();
    }
}

static bool setupMmap()
{
    if (data.isInitialized.test_and_set())
        return true;

    data.mmap = reinterpret_cast<MmapSig>(dlsym(RTLD_NEXT, "mmap"));
    if (data.mmap == nullptr)
        return false;
    data.ffd = syscall(SYS_userfaultfd, O_NONBLOCK);
    if (data.ffd == -1)
        return false;
    uffdio_api api = {
        .api = UFFD_API,
        .features = UFFD_FEATURE_THREAD_ID | UFFD_FEATURE_EVENT_REMAP | UFFD_FEATURE_EVENT_REMOVE | UFFD_FEATURE_EVENT_UNMAP
    };
    if (ioctl(data.ffd, UFFDIO_API, &api))
        return false;
    if (api.api != UFFD_API)
        return false;

    data.thr = std::thread(faultThread);
    atexit(cleanupMmap);

    data.ok = true;
    return true;
}

extern "C" {
void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    if (!setupMmap()) {
        printf("no setup\n");
        return nullptr;
    }
    while (!data.ok.load())
        ;
    auto ret = data.mmap(addr, length, prot, flags, fd, offset);

    if ((flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS)) {
        uffdio_register reg = {
            .range = {
                .start = reinterpret_cast<__u64>(ret),
                .len = length
            },
            .mode = UFFDIO_REGISTER_MODE_MISSING
        };

        if (ioctl(data.ffd, UFFDIO_REGISTER, &reg)) {
            printf("no register %m\n");
            munmap(ret, length);
            return nullptr;
        }

        if (reg.ioctls != UFFD_API_RANGE_IOCTLS) {
            printf("no range %m\n");
            munmap(ret, length);
            return nullptr;
        }
    }

    return ret;
}
} // extern "C"
