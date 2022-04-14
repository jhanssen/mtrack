#include "Stack.h"

#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <thread>

#define PAGESIZE 4096

typedef void* (*MmapSig)(void*, size_t, int, int, int, off_t);

class Hooks
{
public:
    static bool hook();

    enum class HookStatus
    {
        Hooking,
        HookOk,
        HookFailed
    };

private:
    Hooks() = delete;
};

struct Data {
    MmapSig mmap;
    int faultFd;
    std::thread thread;
    std::atomic_flag hookCalled = ATOMIC_FLAG_INIT;
    std::atomic<Hooks::HookStatus> hookStatus = Hooks::HookStatus::Hooking;
    std::atomic_flag isShutdown = ATOMIC_FLAG_INIT;
    int pipe[2];

    static thread_local bool hooked;
} data;

thread_local bool Data::hooked = true;

static void hookThread()
{
    data.hooked = false;

    pollfd evt[] = {
        { .fd = data.faultFd, .events = POLLIN },
        { .fd = data.pipe[0], .events = POLLIN }
    };
    while (poll(evt, 1, 1000) > 0) {
        printf("- top of fault thread\n");
        if (evt[0].revents & (POLLERR | POLLHUP)) {
            // done?
            close(data.faultFd);
            data.faultFd = -1;
            return;
        }
        uffd_msg fault_msg = {0};
        if (read(data.faultFd, &fault_msg, sizeof(fault_msg)) != sizeof(fault_msg)) {
            // read error
            close(data.faultFd);
            data.faultFd = -1;
            return;
        }
        switch (fault_msg.event) {
        case UFFD_EVENT_PAGEFAULT: {
            const auto place = fault_msg.arg.pagefault.address;
            const auto ptid = fault_msg.arg.pagefault.feat.ptid;
            printf("  - pagefault %u\n", ptid);
            ThreadStack stack(ptid);
            uffdio_zeropage zero = {
                .range = {
                    .start = place,
                    .len = PAGESIZE
                }
            };
            if (ioctl(data.faultFd, UFFDIO_ZEROPAGE, &zero)) {
                // boo
                close(data.faultFd);
                data.faultFd = -1;
                return;
            }
            printf("  - handled pagefault\n");
            break; }
        }
    }
}

static void hookCleanup()
{
    // might be a race here if the process exits really quickly
    if (!data.isShutdown.test_and_set()) {
        if (data.faultFd == -1) {
            close(data.faultFd);
            data.faultFd = -1;
        }
        data.thread.join();
    }
}

inline bool Hooks::hook()
{
    if (data.hookCalled.test_and_set()) {
        for (;;) {
            const auto status = data.hookStatus.load();
            if (status == HookStatus::Hooking) {
#ifdef __x86_64__
                __builtin_ia32_pause();
#endif
                continue;
            }
            return status == HookStatus::HookOk;
        }
    }
    data.mmap = reinterpret_cast<MmapSig>(dlsym(RTLD_NEXT, "mmap"));

    if (data.mmap == nullptr) {
        data.hookStatus = HookStatus::HookFailed;
        return false;
    }

    data.faultFd = syscall(SYS_userfaultfd, O_NONBLOCK);
    if (data.faultFd == -1) {
        data.hookStatus = HookStatus::HookFailed;
        return false;
    }

    uffdio_api api = {
        .api = UFFD_API,
        .features = UFFD_FEATURE_THREAD_ID | UFFD_FEATURE_EVENT_REMAP | UFFD_FEATURE_EVENT_REMOVE | UFFD_FEATURE_EVENT_UNMAP
    };
    if (ioctl(data.faultFd, UFFDIO_API, &api)) {
        data.hookStatus = HookStatus::HookFailed;
        return false;
    }
    if (api.api != UFFD_API) {
        data.hookStatus = HookStatus::HookFailed;
        return false;
    }

    if (pipe2(data.pipe, O_NONBLOCK) == -1) {
        data.hookStatus = HookStatus::HookFailed;
        return false;
    }

    data.thread = std::thread(hookThread);
    atexit(hookCleanup);

    data.hookStatus = HookStatus::HookOk;
    return true;
}

extern "C" {
void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    if (!Hooks::hook()) {
        printf("no setup\n");
        return nullptr;
    }
    auto ret = data.mmap(addr, length, prot, flags, fd, offset);
    if (!data.hooked)
        return ret;

    if ((flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS)) {
        uffdio_register reg = {
            .range = {
                .start = reinterpret_cast<__u64>(ret),
                .len = length
            },
            .mode = UFFDIO_REGISTER_MODE_MISSING
        };

        if (ioctl(data.faultFd, UFFDIO_REGISTER, &reg)) {
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
