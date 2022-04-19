#include "Stack.h"
#include "Spinlock.h"
#include "Recorder.h"
#include <common/Version.h>

#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <execinfo.h>
#include <pthread.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <tuple>

#define PAGESIZE 4096

typedef void* (*MmapSig)(void*, size_t, int, int, int, off_t);
typedef void* (*MremapSig)(void*, size_t, size_t, int, ...);
typedef int (*MunmapSig)(void*, size_t);
typedef int (*MprotectSig)(void*, size_t, int);
typedef void* (*DlOpenSig)(const char*, int);
typedef int  (*DlCloseSig)(void*);
typedef int (*PthreadSetnameSig)(pthread_t thread, const char* name);

class Hooks
{
public:
    static void hook();

private:
    Hooks() = delete;
};

struct Data {
    MmapSig mmap, mmap64;
    MunmapSig munmap;
    MremapSig mremap;
    MprotectSig mprotect;
    DlOpenSig dlopen;
    DlCloseSig dlclose;
    PthreadSetnameSig pthread_setname_np;

    int faultFd;
    std::thread thread;
    std::atomic_flag isShutdown = ATOMIC_FLAG_INIT;
    std::atomic<bool> modulesDirty = true;
    int pipe[2];

    Recorder recorder;

    Spinlock mmapRangeLock;
    std::vector<std::tuple<void*, size_t, int>> mmapRanges;

    static thread_local bool hooked;
} *data = nullptr;

static std::once_flag hookOnce = {};

thread_local bool Data::hooked = true;

struct MmapWalker
{
    enum class RangeType
    {
        Empty,
        Start,
        Middle,
        End
    };

    template<typename Func>
    static void walk(void* addr, size_t len, Func&& func);
};


template<typename Func>
void MmapWalker::walk(void* addr, size_t len, Func&& func)
{
    ScopedSpinlock lock(data->mmapRangeLock);
    auto foundit = std::upper_bound(data->mmapRanges.begin(), data->mmapRanges.end(), addr, [](auto addr, const auto& item) {
        return addr < std::get<0>(item);
    });

    auto it = foundit;
    if (foundit != data->mmapRanges.begin())
        --it;

    size_t rem = len;
    size_t used = 0;
    size_t remtmp;
    void* oldend;
    void* newstart;
    if (it != data->mmapRanges.end() && addr >= reinterpret_cast<uint8_t*>(std::get<0>(*it)) && addr < reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it)) {
        if (addr > std::get<0>(*it)) {
            oldend = reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it);
            remtmp = rem;
            // printf("fuckety1 %p (%p) vs %p (%p)\n",
            //        addr, reinterpret_cast<uint8_t*>(addr) + len,
            //        std::get<0>(*it), reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it));
            it = func(RangeType::Start, it, addr, rem, used);
            used += remtmp - rem;
            // printf("used1 %zu %zu (%zu %zu)\n", used, len, remtmp, rem);
            if (it != data->mmapRanges.end() && addr >= reinterpret_cast<uint8_t*>(std::get<0>(*it)) && addr < reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it)) {
                newstart = reinterpret_cast<uint8_t*>(std::get<0>(*it));
                rem -= reinterpret_cast<uint8_t*>(newstart) - reinterpret_cast<uint8_t*>(oldend);
                used += reinterpret_cast<uint8_t*>(newstart) - reinterpret_cast<uint8_t*>(oldend);
                // printf("used2 %zu %zu (%zu %zu)\n", used, len, remtmp, rem);
            }
            assert(used <= len);
        }
        // printf("fuckety2 %p (%p) vs %p (%p)\n",
        //        addr, reinterpret_cast<uint8_t*>(addr) + len,
        //        std::get<0>(*it), reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it));
        while (it != data->mmapRanges.end() && addr <= std::get<0>(*it) && (reinterpret_cast<uint8_t*>(addr) + len >= reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it))) {
            oldend = reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it);
            remtmp = rem;
            // printf("walked right into it %p %p (%zu) -> %p %p\n",
            //        addr, reinterpret_cast<uint8_t*>(addr) + len, rem,
            //        std::get<0>(*it), reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it));
            it = func(RangeType::Middle, it, addr, rem, used);
            used += remtmp - rem;
            // printf("used3 %zu %zu (%zu %zu)\n", used, len, remtmp, rem);
            if (it != data->mmapRanges.end() && addr <= std::get<0>(*it) && (reinterpret_cast<uint8_t*>(addr) + len >= reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it))) {
                newstart = reinterpret_cast<uint8_t*>(std::get<0>(*it));
                rem -= reinterpret_cast<uint8_t*>(newstart) - reinterpret_cast<uint8_t*>(oldend);
                used += reinterpret_cast<uint8_t*>(newstart) - reinterpret_cast<uint8_t*>(oldend);
                // printf("used4 %zu %zu (%zu %zu)\n", used, len, remtmp, rem);
            }
            assert(used <= len);
        }
        if (it != data->mmapRanges.end() && rem > 0 && reinterpret_cast<uint8_t*>(addr) + used >= std::get<0>(*it) && reinterpret_cast<uint8_t*>(addr) + len < reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it)) {
            it = func(RangeType::End, it, addr, rem, used);
        }
    } else {
        it = func(RangeType::Empty, foundit, addr, len, 0);
    }
}

static int dl_iterate_phdr_callback(struct dl_phdr_info* info, size_t /*size*/, void* /*data*/)
{
    const char* fileName = info->dlpi_name;
    if (!fileName || !fileName[0]) {
        fileName = "m";
    }

    // assume no quotes in filename?
    data->recorder.record("dl \"%s\" %zx\n", fileName, info->dlpi_addr);

    for (int i = 0; i < info->dlpi_phnum; i++) {
        const auto& phdr = info->dlpi_phdr[i];
        if (phdr.p_type == PT_LOAD) {
            data->recorder.record("ph %zx %zx\n", phdr.p_vaddr, phdr.p_memsz);
        }
    }

    return 0;
}

static void hookThread()
{
    data->hooked = false;

    pollfd evt[] = {
        { .fd = data->faultFd, .events = POLLIN },
        { .fd = data->pipe[0], .events = POLLIN }
    };
    for (;;) {
        // printf("- top of fault thread\n");
        switch (poll(evt, 2, 1000)) {
        case -1:
            return;
        case 0:
            continue;
        default:
            break;
        }

        // printf("- fault thread 0\n");

        if (data->modulesDirty.load(std::memory_order_acquire)) {
            dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);
            data->modulesDirty.store(false, std::memory_order_release);
        }

        // printf("- fault thread 1 %d %d\n", evt[0].revents, evt[1].revents);

        if (evt[0].revents & (POLLERR | POLLHUP)) {
            // done?
            close(data->faultFd);
            data->faultFd = -1;
            printf("- pagefault error 1\n");
            return;
        }
        // printf("- fault thread 2\n");
        if (evt[0].revents & POLLIN) {
            uffd_msg fault_msg = {0};
            if (read(data->faultFd, &fault_msg, sizeof(fault_msg)) != sizeof(fault_msg)) {
                // read error
                close(data->faultFd);
                data->faultFd = -1;
                printf("- pagefault error 2\n");
                return;
            }

            // printf("- fault thread 3 0x%x\n", fault_msg.event);
            switch (fault_msg.event) {
            case UFFD_EVENT_PAGEFAULT: {
                const auto place = fault_msg.arg.pagefault.address;
                const auto ptid = fault_msg.arg.pagefault.feat.ptid;
                // printf("  - pagefault %u\n", ptid);
                data->recorder.record("mm %p %u\n", place, ptid);
                ThreadStack stack(ptid);
                while (!stack.atEnd()) {
                    data->recorder.record("st %llx %llx\n", stack.ip(), stack.sp());
                    stack.next();
                }
                uffdio_zeropage zero = {
                    .range = {
                        .start = place,
                        .len = PAGESIZE
                    }
                };
                if (ioctl(data->faultFd, UFFDIO_ZEROPAGE, &zero)) {
                    // boo
                    close(data->faultFd);
                    data->faultFd = -1;
                    return;
                }
                // printf("  - handled pagefault\n");
                break; }
            }
        }
        // printf("- fault thread 4\n");
    }
    printf("- end of fault thread\n");
}

static void hookCleanup()
{
    // might be a race here if the process exits really quickly
    if (!data->isShutdown.test_and_set()) {
        if (data->faultFd == -1) {
            close(data->faultFd);
            data->faultFd = -1;
        }
        data->thread.join();
    }
    data->recorder.cleanup();
    delete data;
    data = nullptr;
}

void Hooks::hook()
{
    data = new Data();
    data->mmap = reinterpret_cast<MmapSig>(dlsym(RTLD_NEXT, "mmap"));
    if (data->mmap == nullptr) {
        printf("no mmap\n");
        abort();
    }
    data->mmap64 = reinterpret_cast<MmapSig>(dlsym(RTLD_NEXT, "mmap64"));
    if (data->mmap64 == nullptr) {
        printf("no mmap64\n");
        abort();
    }
    data->munmap = reinterpret_cast<MunmapSig>(dlsym(RTLD_NEXT, "munmap"));
    if (data->munmap == nullptr) {
        printf("no munmap\n");
        abort();
    }
    data->mremap = reinterpret_cast<MremapSig>(dlsym(RTLD_NEXT, "mremap"));
    if (data->mremap == nullptr) {
        printf("no mremap\n");
        abort();
    }
    data->mprotect = reinterpret_cast<MprotectSig>(dlsym(RTLD_NEXT, "mprotect"));
    if (data->mprotect == nullptr) {
        printf("no mprotect\n");
        abort();
    }
    data->dlopen = reinterpret_cast<DlOpenSig>(dlsym(RTLD_NEXT, "dlopen"));
    if (data->dlopen == nullptr) {
        printf("no dlopen\n");
        abort();
    }
    data->dlclose = reinterpret_cast<DlCloseSig>(dlsym(RTLD_NEXT, "dlclose"));
    if (data->dlclose == nullptr) {
        printf("no dlclose\n");
        abort();
    }
    data->pthread_setname_np = reinterpret_cast<PthreadSetnameSig>(dlsym(RTLD_NEXT, "pthread_setname_np"));
    if (data->pthread_setname_np == nullptr) {
        printf("no pthread_setname_np\n");
        abort();
    }

    data->faultFd = syscall(SYS_userfaultfd, O_NONBLOCK);
    if (data->faultFd == -1) {
        printf("no faultFd\n");
        abort();
    }

    uffdio_api api = {
        .api = UFFD_API,
        .features = UFFD_FEATURE_THREAD_ID //| UFFD_FEATURE_EVENT_REMAP | UFFD_FEATURE_EVENT_REMOVE | UFFD_FEATURE_EVENT_UNMAP
    };
    if (ioctl(data->faultFd, UFFDIO_API, &api)) {
        printf("no ioctl api\n");
        abort();
    }
    if (api.api != UFFD_API) {
        printf("no api api\n");
        abort();
    }

    if (pipe2(data->pipe, O_NONBLOCK) == -1) {
        printf("no pipe\n");
        abort();
    }

    data->thread = std::thread(hookThread);
    atexit(hookCleanup);

    data->recorder.initialize("./mtrack.data");
    data->recorder.record("mt %x\n", MTrackFileVersion);

    printf("hook.\n");
}

bool trackMmap(void* addr, size_t length, int prot, int flags)
{
    // printf("-maping %p %zu flags 0x%x priv/anon %d\n", addr, length, flags, (flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS));
    MmapWalker::walk(addr, length, [flags](MmapWalker::RangeType type, auto it, void* addr, size_t& len, size_t used) {
        assert(len > 0);
        switch (type) {
        case MmapWalker::RangeType::Empty:
            // printf("inserting %p %zu\n", addr, len);
            return data->mmapRanges.insert(it, std::make_tuple(addr, len, flags)) + 1;
        case MmapWalker::RangeType::Start: {
            if (std::get<2>(*it) == flags) {
                len -= std::get<1>(*it);
                // printf("started, len is now %zu (%d)\n", len, __LINE__);
                return it + 1;
            }
            // split out start, add new item
            const auto oldlen = std::get<1>(*it);
            const auto oldflags = std::get<2>(*it);
            std::get<1>(*it) = reinterpret_cast<uint8_t*>(addr) - reinterpret_cast<uint8_t*>(std::get<0>(*it));
            it = data->mmapRanges.insert(it + 1, std::make_tuple(addr, oldlen - std::get<1>(*it), flags));
            // if addr is fully contained in this item, make another new item with old flags
            if (reinterpret_cast<uint8_t*>(addr) + len < reinterpret_cast<uint8_t*>(std::get<0>(*it)) + oldlen) {
                const auto remlen = (reinterpret_cast<uint8_t*>(std::get<0>(*it)) + oldlen) - (reinterpret_cast<uint8_t*>(addr) + len);
                it = data->mmapRanges.insert(it + 1, std::make_tuple(reinterpret_cast<uint8_t*>(addr) + len, remlen, oldflags));
                len = 0;
                // printf("started, len is now %zu (%d)\n", len, __LINE__);
            } else {
                assert(len >= oldlen - std::get<1>(*it));
                len -= oldlen - std::get<1>(*it);
                // printf("started, len is now %zu (%d)\n", len, __LINE__);
            }
            return it + 1; }
        case MmapWalker::RangeType::Middle:
            std::get<2>(*it) = flags;
            assert(len >= std::get<1>(*it));
            len -= std::get<1>(*it);
            // printf("middled, len is now %zu (%d)\n", len, __LINE__);
            return it + 1;
        case MmapWalker::RangeType::End:
            if (std::get<2>(*it) == flags) {
                len -= std::get<1>(*it);
                // printf("ended, len is now %zu (%d)\n", len, __LINE__);
                return it + 1;
            }
            // add new item, split out end
            // printf("balli used %zu (%p %p) addr %p %zu\n", used, std::get<0>(*it), reinterpret_cast<uint8_t*>(std::get<0>(*it)) + used, addr, len);
            reinterpret_cast<uint8_t*&>(std::get<0>(*it)) += used + len;
            std::get<1>(*it) -= len;
            it = data->mmapRanges.insert(it, std::make_tuple(addr, len, flags));
            len = 0;
            // printf("ended, len is now %zu (%d)\n", len, __LINE__);
            return it + 2;
        }
        assert(false && "invalid MmapWalker::RangeType for mmap");
        __builtin_unreachable();
    });
    // printf("1--\n");
    // for (const auto& item : data->mmapRanges) {
    //     printf(" - %p(%p) %zu\n", std::get<0>(item), reinterpret_cast<uint8_t*>(std::get<0>(item)) + std::get<1>(item), std::get<1>(item));
    // }

    if ((prot & (PROT_READ | PROT_WRITE)) && !(prot & PROT_EXEC)) {
        // printf("ball %zu 0x%x 0x%x\n", length, prot, flags);
        uffdio_register reg = {
            .range = {
                .start = reinterpret_cast<__u64>(addr),
                .len = length
            },
            .mode = UFFDIO_REGISTER_MODE_MISSING
        };

        if (ioctl(data->faultFd, UFFDIO_REGISTER, &reg))
            return true;

        if (reg.ioctls != UFFD_API_RANGE_IOCTLS) {
            printf("no range %m\n");
            munmap(addr, length);
            return false;
        }
    }

    return true;
}

extern "C" {
void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    std::call_once(hookOnce, Hooks::hook);

    // printf("mmap?? %p\n", addr);
    auto ret = data->mmap(addr, length, prot, flags, fd, offset);
    // printf("mmap %p??\n", ret);

    if (!data->hooked || ret == MAP_FAILED)
        return ret;

    if ((flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS)) {
        trackMmap(ret, length, prot, flags);
    }

    return ret;
}

void* mmap64(void* addr, size_t length, int prot, int flags, int fd, off_t pgoffset)
{
    std::call_once(hookOnce, Hooks::hook);

    // printf("mmap64?? %p\n", addr);
    auto ret = data->mmap64(addr, length, prot, flags, fd, pgoffset);
    // printf("mmap64 %p??\n", ret);

    if (!data->hooked || ret == MAP_FAILED)
        return ret;

    if ((flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS)) {
        trackMmap(ret, length, prot, flags);
    }

    return ret;
}

int munmap(void* addr, size_t len)
{
    std::call_once(hookOnce, Hooks::hook);

    // if (len > 1000000) {
    //     printf("3--\n");
    //     for (const auto& item : data->mmapRanges) {
    //         printf(" - %p(%p) %zu\n", std::get<0>(item), reinterpret_cast<uint8_t*>(std::get<0>(item)) + std::get<1>(item), std::get<1>(item));
    //     }
    // }

    {
        // printf("-unmaping %p(%p) %zu\n", addr, reinterpret_cast<uint8_t*>(addr) + len, len);
        bool updated = false;
        MmapWalker::walk(addr, len, [&updated](MmapWalker::RangeType type, auto it, void* addr, size_t& len, size_t used) {
            switch (type) {
            case MmapWalker::RangeType::Start: {
                // update start item
                // printf("FUCK %p(%p) vs %p(%p)\n", addr, reinterpret_cast<uint8_t*>(addr) + len,
                //        std::get<0>(*it), reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it));
                updated = true;
                const auto oldlen = std::get<1>(*it);
                std::get<1>(*it) = reinterpret_cast<uint8_t*>(addr) - reinterpret_cast<uint8_t*>(std::get<0>(*it));
                // if addr is fully contained in this item, make another new item at the end
                if (reinterpret_cast<uint8_t*>(addr) + len < reinterpret_cast<uint8_t*>(std::get<0>(*it)) + oldlen) {
                    const auto remlen = (reinterpret_cast<uint8_t*>(std::get<0>(*it)) + oldlen) - (reinterpret_cast<uint8_t*>(addr) + len);
                    it = data->mmapRanges.insert(it + 1, std::make_tuple(reinterpret_cast<uint8_t*>(addr) + len, remlen, std::get<2>(*it)));
                    len = 0;
                } else {
                    // printf("FUCK AGAIN %zu vs %zu (%zu %zu)\n", len, oldlen - std::get<1>(*it), oldlen, std::get<1>(*it));
                    assert(len >= oldlen - std::get<1>(*it));
                    len -= oldlen - std::get<1>(*it);
                }
                return it + 1; }
            case MmapWalker::RangeType::Middle:
                updated = true;
                len -= std::get<1>(*it);
                return data->mmapRanges.erase(it);
            case MmapWalker::RangeType::End:
                // update end item
                updated = true;
                reinterpret_cast<uint8_t*&>(std::get<0>(*it)) += len;
                std::get<1>(*it) -= len;
                len = 0;
                return it + 1;
            default:
                return it + 1;
            }
            assert(false && "invalid MmapWalker::RangeType for mmap");
            __builtin_unreachable();
        });
        // if (updated) {
        //     printf("2--\n");
        //     for (const auto& item : data->mmapRanges) {
        //         printf(" - %p(%p) %zu\n", std::get<0>(item), reinterpret_cast<uint8_t*>(std::get<0>(item)) + std::get<1>(item), std::get<1>(item));
        //     }
        // }
    }

    const auto ret = data->munmap(addr, len);
    // if (len > 1000000) {
    //     int j, nptrs;
    //     void *buffer[100];
    //     char **strings;

    //     nptrs = backtrace(buffer, 100);
    //     printf("backtrace() returned %d addresses\n", nptrs);

    //     /* The call backtrace_symbols_fd(buffer, nptrs, STDOUT_FILENO)
    //        would produce similar output to the following: */

    //     strings = backtrace_symbols(buffer, nptrs);
    //     if (strings == NULL) {
    //         perror("backtrace_symbols");
    //         exit(EXIT_FAILURE);
    //     }

    //     for (j = 0; j < nptrs; j++)
    //         printf("%s\n", strings[j]);

    //     free(strings);
    // }
    // printf("-unmapped %p %d\n", addr, ret);
    return ret;
}

int mprotect(void* addr, size_t len, int prot)
{
    std::call_once(hookOnce, Hooks::hook);

    int flags = 0;
    {
        ScopedSpinlock lock(data->mmapRangeLock);
        auto it = std::upper_bound(data->mmapRanges.begin(), data->mmapRanges.end(), addr, [](auto addr, const auto& item) {
            return addr < std::get<0>(item);
        });
        if (it != data->mmapRanges.begin() && !data->mmapRanges.empty())
            --it;
        // if (it != data->mmapRanges.end()) {
        //     printf("found %p %zu (%p) (%d) for %p %zu\n",
        //            std::get<0>(*it), std::get<1>(*it), reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it),
        //            std::get<2>(*it), addr, len);
        // }
        if (it != data->mmapRanges.end() && addr >= reinterpret_cast<uint8_t*>(std::get<0>(*it)) && addr < reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it)) {
            // printf(" - really found\n");
            flags = std::get<2>(*it);
        }
    }

    // printf("mprotect %p %zu %d %d\n", addr, len, prot, flags);
    if ((prot & (PROT_READ | PROT_WRITE)) && !(prot & PROT_EXEC) && (flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS)) {
        uffdio_register reg = {
            .range = {
                .start = reinterpret_cast<__u64>(addr),
                .len = len
            },
            .mode = UFFDIO_REGISTER_MODE_MISSING
        };

        if (ioctl(data->faultFd, UFFDIO_REGISTER, &reg))
            return data->mprotect(addr, len, prot);

        if (reg.ioctls != UFFD_API_RANGE_IOCTLS) {
            printf("no range %m\n");
            return -1;
        }
    }

    return data->mprotect(addr, len, prot);
}

void* mremap(void* addr, size_t old_size, size_t new_size, int flags, ...)
{
    // printf("======!~!!!!!!!==== mremap!!!\n");
    abort();
    if (flags & MREMAP_FIXED) {
        va_list ap;
        va_start(ap, flags);
        void* new_address = va_arg(ap, void*);
        va_end(ap);

        return data->mremap(addr, old_size, new_size, flags, new_address);
    }
    return data->mremap(addr, old_size, new_size, flags);
}

void* dlopen(const char* filename, int flags)
{
    std::call_once(hookOnce, Hooks::hook);
    data->modulesDirty.store(true, std::memory_order_release);
    return data->dlopen(filename, flags);
}

int dlclose(void* handle)
{
    std::call_once(hookOnce, Hooks::hook);
    data->modulesDirty.store(true, std::memory_order_release);
    return data->dlclose(handle);
}

int pthread_setname_np(pthread_t thread, const char* name)
{
    std::call_once(hookOnce, Hooks::hook);
    // ### should fix this, this will drop unless we're the same thread
    if (pthread_equal(thread, pthread_self())) {
        data->recorder.record("tn %d %s\n", gettid(), name);
    }
    return data->pthread_setname_np(thread, name);
}

} // extern "C"
