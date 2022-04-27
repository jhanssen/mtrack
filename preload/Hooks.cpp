#include "Stack.h"
#include "Spinlock.h"
#include "Recorder.h"
#include "NoHook.h"
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

#define UNW_LOCAL_ONLY
#include <libunwind.h>

static constexpr uint64_t PAGESIZE = 4096;

typedef void* (*MmapSig)(void*, size_t, int, int, int, off_t);
typedef void* (*Mmap64Sig)(void*, size_t, int, int, int, __off64_t);
typedef void* (*MremapSig)(void*, size_t, size_t, int, ...);
typedef int (*MunmapSig)(void*, size_t);
typedef int (*MadviseSig)(void*, size_t, int);
typedef int (*MprotectSig)(void*, size_t, int);
typedef void* (*DlOpenSig)(const char*, int);
typedef int (*DlCloseSig)(void*);
typedef int (*PthreadSetnameSig)(pthread_t thread, const char* name);
typedef void* (*MallocSig)(size_t);
typedef void (*FreeSig)(void*);
typedef void* (*CallocSig)(size_t, size_t);
typedef void* (*ReallocSig)(void*, size_t);
typedef void* (*ReallocArraySig)(void*, size_t, size_t);
typedef int (*Posix_MemalignSig)(void **, size_t, size_t);
typedef void* (*Aligned_AllocSig)(size_t, size_t);

inline uint64_t alignToPage(uint64_t size)
{
    return size + (((~size) + 1) & (PAGESIZE - 1));
}

inline uint64_t alignToSize(uint64_t size, uint64_t align)
{
    return size + (((~size) + 1) & (align - 1));
}

inline uint64_t mmap_ptr_cast(void *ptr)
{
    const uint64_t ret = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr));
    return ret + (PAGESIZE - (ret % PAGESIZE)) - PAGESIZE;
}

template<size_t Size>
class Allocator
{
public:
    Allocator() = default;

    void* allocate(size_t n)
    {
        assert(mOffset + n <= Size);
        auto ret = reinterpret_cast<void*>(data() + mOffset);
        mOffset = alignToSize(mOffset + n, sizeof(void*));
        return ret;
    }

    bool hasData(void* d) const
    {
        return d >= data() && d < data() + Size;
    }

    char* data() {
        return reinterpret_cast<char*>(&mStorage);
    }

    const char* data() const {
        return reinterpret_cast<const char*>(&mStorage);
    }

private:
    typename std::aligned_storage_t<Size, alignof(void*)> mStorage = {};
    size_t mOffset = 0;
};

class Hooks
{
public:
    static void hook();

private:
    Hooks() = delete;
};

struct Callbacks
{
    MmapSig mmap { nullptr };
    Mmap64Sig mmap64 { nullptr };
    MunmapSig munmap { nullptr };
    MremapSig mremap { nullptr };
    MadviseSig madvise { nullptr };
    MprotectSig mprotect { nullptr };
    DlOpenSig dlopen { nullptr };
    DlCloseSig dlclose { nullptr };
    PthreadSetnameSig pthread_setname_np { nullptr };
    MallocSig malloc { nullptr };
    FreeSig free { nullptr };
    CallocSig calloc { nullptr };
    ReallocSig realloc { nullptr };
    ReallocArraySig reallocarray { nullptr };
    Posix_MemalignSig posix_memalign { nullptr };
    Aligned_AllocSig aligned_alloc { nullptr };
} callbacks;

Allocator<4096> allocator;

struct Data {
    int faultFd;
    std::thread thread;
    std::atomic_flag isShutdown = ATOMIC_FLAG_INIT;
    std::atomic<bool> modulesDirty = true;
    int pipe[2];

    Recorder recorder;

    Spinlock mmapRangeLock;
    std::vector<std::tuple<void*, size_t, int>> mmapRanges;
} *data = nullptr;

namespace {
bool safePrint(const char *string)
{
    const size_t len = strlen(string);
    return ::write(STDOUT_FILENO, string, len) == len;
}

thread_local bool hooked = true;
thread_local bool inMallocFree = false;
}

NoHook::NoHook()
    : wasHooked(::hooked)
{
    ::hooked = false;
}
NoHook::~NoHook()
{
    ::hooked = wasHooked;
}

class MallocFree
{
public:
    MallocFree()
        : mPrev(::inMallocFree)
    {
        ::inMallocFree = true;
    }

    ~MallocFree()
    {
        ::inMallocFree = mPrev;
    }

    bool wasInMallocFree() const
    {
        return mPrev;
    }
private:
    const bool mPrev;
};

static std::once_flag hookOnce = {};

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
            if (rem > 0 && it != data->mmapRanges.end() && addr >= reinterpret_cast<uint8_t*>(std::get<0>(*it)) && addr < reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it)) {
                newstart = reinterpret_cast<uint8_t*>(std::get<0>(*it));
                assert(reinterpret_cast<uint8_t*>(newstart) - reinterpret_cast<uint8_t*>(oldend) >= rem);
                rem -= reinterpret_cast<uint8_t*>(newstart) - reinterpret_cast<uint8_t*>(oldend);
                used += reinterpret_cast<uint8_t*>(newstart) - reinterpret_cast<uint8_t*>(oldend);
                // printf("used2 %zu %zu (%zu %zu)\n", used, len, remtmp, rem);
            }
            assert(used <= len);
        }
        // printf("fuckety2 %p (%p) vs %p (%p)\n",
        //        addr, reinterpret_cast<uint8_t*>(addr) + len,
        //        std::get<0>(*it), reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it));
        while (rem > 0 && it != data->mmapRanges.end() && addr <= std::get<0>(*it) && (reinterpret_cast<uint8_t*>(addr) + len >= reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it))) {
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
        if (rem > 0 && it != data->mmapRanges.end() && reinterpret_cast<uint8_t*>(addr) + used >= std::get<0>(*it) && reinterpret_cast<uint8_t*>(addr) + len < reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it)) {
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
        fileName = "s";
    }

    assert(data->recorder.isScoped());
    data->recorder.record(RecordType::Library, Recorder::String(fileName), static_cast<uint64_t>(info->dlpi_addr));

    for (int i = 0; i < info->dlpi_phnum; i++) {
        const auto& phdr = info->dlpi_phdr[i];
        if (phdr.p_type == PT_LOAD) {
            data->recorder.record(RecordType::LibraryHeader, static_cast<uint64_t>(phdr.p_vaddr), static_cast<uint64_t>(phdr.p_memsz));
        }
    }

    return 0;
}

static void hookThread()
{
    hooked = false;

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
            // need to lock the recorder scope here instead of inside the callback
            // since frames above other calls to dl_iterate_phdr may have already locked our scope
            Recorder::Scope recorderScope(&data->recorder);
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
                const auto place = static_cast<uint64_t>(fault_msg.arg.pagefault.address);
                const auto ptid = static_cast<uint32_t>(fault_msg.arg.pagefault.feat.ptid);
                // printf("  - pagefault %u\n", ptid);
                {
                    Recorder::Scope recorderScope(&data->recorder);
                    data->recorder.record(RecordType::PageFault, place, ptid);
                    Stack stack(ptid);
                    while (!stack.atEnd()) {
                        data->recorder.record(RecordType::Stack, stack.ip());
                        stack.next();
                    }
                    data->recorder.record(RecordType::Stack, std::numeric_limits<uint64_t>::max());
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
        if (evt[1].revents & POLLIN) {
            break;
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
        int w;
        do {
            w = ::write(data->pipe[1], "q", 1);
        } while (w == -1 && errno == EINTR);
        data->thread.join();
    }
    NoHook noHook;
    data->recorder.cleanup();
    delete data;
    data = nullptr;
}

void Hooks::hook()
{
    callbacks.mmap = reinterpret_cast<MmapSig>(dlsym(RTLD_NEXT, "mmap"));
    if (callbacks.mmap == nullptr) {
        safePrint("no mmap\n");
        abort();
    }
    callbacks.mmap64 = reinterpret_cast<Mmap64Sig>(dlsym(RTLD_NEXT, "mmap64"));
    if (callbacks.mmap64 == nullptr) {
        safePrint("no mmap64\n");
        abort();
    }
    callbacks.munmap = reinterpret_cast<MunmapSig>(dlsym(RTLD_NEXT, "munmap"));
    if (callbacks.munmap == nullptr) {
        safePrint("no munmap\n");
        abort();
    }
    callbacks.mremap = reinterpret_cast<MremapSig>(dlsym(RTLD_NEXT, "mremap"));
    if (callbacks.mremap == nullptr) {
        safePrint("no mremap\n");
        abort();
    }
    callbacks.madvise = reinterpret_cast<MadviseSig>(dlsym(RTLD_NEXT, "madvise"));
    if (callbacks.madvise == nullptr) {
        safePrint("no madvise\n");
        abort();
    }
    callbacks.mprotect = reinterpret_cast<MprotectSig>(dlsym(RTLD_NEXT, "mprotect"));
    if (callbacks.mprotect == nullptr) {
        safePrint("no mprotect\n");
        abort();
    }
    callbacks.dlopen = reinterpret_cast<DlOpenSig>(dlsym(RTLD_NEXT, "dlopen"));
    if (callbacks.dlopen == nullptr) {
        safePrint("no dlopen\n");
        abort();
    }
    callbacks.dlclose = reinterpret_cast<DlCloseSig>(dlsym(RTLD_NEXT, "dlclose"));
    if (callbacks.dlclose == nullptr) {
        safePrint("no dlclose\n");
        abort();
    }
    callbacks.pthread_setname_np = reinterpret_cast<PthreadSetnameSig>(dlsym(RTLD_NEXT, "pthread_setname_np"));
    if (callbacks.pthread_setname_np == nullptr) {
        safePrint("no pthread_setname_np\n");
        abort();
    }

    callbacks.malloc = reinterpret_cast<MallocSig>(dlsym(RTLD_NEXT, "malloc"));
    if (callbacks.malloc == nullptr) {
        safePrint("no malloc\n");
        abort();
    }

    callbacks.free = reinterpret_cast<FreeSig>(dlsym(RTLD_NEXT, "free"));
    if (callbacks.free == nullptr) {
        safePrint("no free\n");
        abort();
    }

    callbacks.calloc = reinterpret_cast<CallocSig>(dlsym(RTLD_NEXT, "calloc"));
    if (callbacks.calloc == nullptr) {
        safePrint("no calloc\n");
        abort();
    }

    callbacks.realloc = reinterpret_cast<ReallocSig>(dlsym(RTLD_NEXT, "realloc"));
    if (callbacks.realloc == nullptr) {
        safePrint("no realloc\n");
        abort();
    }

    callbacks.reallocarray = reinterpret_cast<ReallocArraySig>(dlsym(RTLD_NEXT, "reallocarray"));
    if (callbacks.reallocarray == nullptr) {
        safePrint("no reallocarray\n");
        abort();
    }

    callbacks.posix_memalign = reinterpret_cast<Posix_MemalignSig>(dlsym(RTLD_NEXT, "posix_memalign"));
    if (callbacks.posix_memalign == nullptr) {
        safePrint("no posix_memalign\n");
        abort();
    }

    callbacks.realloc = reinterpret_cast<ReallocSig>(dlsym(RTLD_NEXT, "realloc"));
    if (callbacks.realloc == nullptr) {
        safePrint("no realloc\n");
        abort();
    }

    data = new Data();
    data->faultFd = syscall(SYS_userfaultfd, O_NONBLOCK);
    if (data->faultFd == -1) {
        safePrint("no faultFd\n");
        abort();
    }

    uffdio_api api = {
        .api = UFFD_API,
        .features = UFFD_FEATURE_THREAD_ID //| UFFD_FEATURE_EVENT_REMAP | UFFD_FEATURE_EVENT_REMOVE | UFFD_FEATURE_EVENT_UNMAP
    };
    if (ioctl(data->faultFd, UFFDIO_API, &api)) {
        safePrint("no ioctl api\n");
        abort();
    }
    if (api.api != UFFD_API) {
        safePrint("no api api\n");
        abort();
    }

    if (pipe2(data->pipe, O_NONBLOCK) == -1) {
        safePrint("no pipe\n");
        abort();
    }

    data->thread = std::thread(hookThread);
    atexit(hookCleanup);

    data->recorder.initialize("./mtrack.data");

    // record the executable file
    char buf1[512];
    char buf2[4096];
    snprintf(buf1, sizeof(buf1), "/proc/%u/exe", getpid());
    ssize_t l = readlink(buf1, buf2, sizeof(buf2));
    if (l == -1 || l == sizeof(buf2)) {
        // badness
        fprintf(stderr, "no exe\n");
    } else {
        data->recorder.record(RecordType::Executable, Recorder::String(buf2, l));
    }

    // record the working directory
    // ### should probably hook chdir and fchdir to record runtime changes
    if (getcwd(buf2, sizeof(buf2)) == nullptr) {
        // badness
        fprintf(stderr, "no cwd\n");
    } else {
        data->recorder.record(RecordType::WorkingDirectory, Recorder::String(buf2));
    }

    NoHook nohook;

    unw_set_caching_policy(unw_local_addr_space, UNW_CACHE_PER_THREAD);
    unw_set_cache_size(unw_local_addr_space, 1024, 0);

    safePrint("hook.\n");
}

uint64_t trackMmap(void* addr, size_t length, int prot, int flags)
{
    uint64_t allocated = 0;
    // printf("-maping %p %zu flags 0x%x priv/anon %d\n", addr, length, flags, (flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS));
    MmapWalker::walk(addr, length, [flags, &allocated](MmapWalker::RangeType type, auto it, void* waddr, size_t& wlength, size_t used) {
        assert(wlength > 0);
        switch (type) {
        case MmapWalker::RangeType::Empty:
            // printf("inserting %p %zu\n", addr, len);
            allocated += wlength;
            return data->mmapRanges.insert(it, std::make_tuple(waddr, wlength, flags)) + 1;
        case MmapWalker::RangeType::Start: {
            if (std::get<2>(*it) == flags) {
                wlength -= std::min(std::get<1>(*it), wlength);
                // printf("started, len is now %zu (%d)\n", len, __LINE__);
                return it + 1;
            }
            // split out start, add new item
            const auto oldlen = std::get<1>(*it);
            const auto oldflags = std::get<2>(*it);
            std::get<1>(*it) = reinterpret_cast<uint8_t*>(waddr) - reinterpret_cast<uint8_t*>(std::get<0>(*it));
            allocated += oldlen - std::get<1>(*it);
            it = data->mmapRanges.insert(it + 1, std::make_tuple(waddr, oldlen - std::get<1>(*it), flags));
            // if addr is fully contained in this item, make another new item with old flags
            if (reinterpret_cast<uint8_t*>(waddr) + wlength < reinterpret_cast<uint8_t*>(std::get<0>(*it)) + oldlen) {
                const auto remlen = (reinterpret_cast<uint8_t*>(std::get<0>(*it)) + oldlen) - (reinterpret_cast<uint8_t*>(waddr) + wlength);
                allocated += remlen;
                it = data->mmapRanges.insert(it + 1, std::make_tuple(reinterpret_cast<uint8_t*>(waddr) + wlength, remlen, oldflags));
                wlength = 0;
                // printf("started, len is now %zu (%d)\n", len, __LINE__);
            } else {
                assert(wlength >= oldlen - std::get<1>(*it));
                wlength -= oldlen - std::get<1>(*it);
                // printf("started, len is now %zu (%d)\n", len, __LINE__);
            }
            return it + 1; }
        case MmapWalker::RangeType::Middle:
            std::get<2>(*it) = flags;
            wlength -= std::min(std::get<1>(*it), wlength);
            // printf("middled, len is now %zu (%d)\n", len, __LINE__);
            return it + 1;
        case MmapWalker::RangeType::End:
            if (std::get<2>(*it) == flags) {
                wlength -= std::min(std::get<1>(*it), wlength);
                // printf("ended, len is now %zu (%d)\n", len, __LINE__);
                return it + 1;
            }
            // add new item, split out end
            // printf("balli used %zu (%p %p) addr %p %zu\n", used, std::get<0>(*it), reinterpret_cast<uint8_t*>(std::get<0>(*it)) + used, addr, len);
            reinterpret_cast<uint8_t*&>(std::get<0>(*it)) += used + wlength;
            std::get<1>(*it) -= wlength;
            allocated += wlength;
            it = data->mmapRanges.insert(it, std::make_tuple(waddr, wlength, flags));
            wlength = 0;
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
                .len = alignToPage(length)
            },
            .mode = UFFDIO_REGISTER_MODE_MISSING,
            .ioctls = 0
        };

        if (ioctl(data->faultFd, UFFDIO_REGISTER, &reg) == -1) {
            printf("register failed (1) %m\n");
            return allocated;
        }

        if (reg.ioctls != UFFD_API_RANGE_IOCTLS) {
            printf("no range (1) 0x%llx\n", reg.ioctls);
            return allocated;
        } else {
            printf("got ok (1)\n");
        }
    }

    return allocated;
}

void reportMalloc(void* ptr, size_t size)
{
    NoHook nohook;

    Recorder::Scope recordScope(&data->recorder);

    if (data->modulesDirty.load(std::memory_order_acquire)) {
        dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);
        data->modulesDirty.store(false, std::memory_order_release);
    }

    data->recorder.record(RecordType::Malloc,
                          static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)),
                          static_cast<uint64_t>(size),
                          static_cast<uint32_t>(gettid()));
    Stack stack(0);
    while (!stack.atEnd()) {
        data->recorder.record(RecordType::Stack, stack.ip());
        stack.next();
    }
    data->recorder.record(RecordType::Stack, std::numeric_limits<uint64_t>::max());
}

void reportFree(void* ptr)
{
    NoHook nohook;

    Recorder::Scope recordScope(&data->recorder);

    if (data->modulesDirty.load(std::memory_order_acquire)) {
        dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);
        data->modulesDirty.store(false, std::memory_order_release);
    }

    data->recorder.record(RecordType::Free, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)));
}

extern "C" {
void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

    // printf("mmap?? %p\n", addr);
    auto ret = callbacks.mmap(addr, length, prot, flags, fd, offset);
    // printf("mmap %p??\n", ret);

    if (!::hooked || ret == MAP_FAILED)
        return ret;

    NoHook nohook;

    bool tracked = false;
    uint64_t allocated = 0;
    if (!::inMallocFree
        && (flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS)
        && fd == -1) {
        allocated = trackMmap(ret, length, prot, flags);
        tracked = true;
    }

    Recorder::Scope recordScope(&data->recorder);

    if (data->modulesDirty.load(std::memory_order_acquire)) {
        dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);
        data->modulesDirty.store(false, std::memory_order_release);
    }

    data->recorder.record(tracked ? RecordType::MmapTracked : RecordType::MmapUntracked,
                          mmap_ptr_cast(ret), alignToPage(length), allocated,
                          prot, flags, fd, static_cast<uint64_t>(offset), static_cast<uint32_t>(gettid()));

    Stack stack(0);
    while (!stack.atEnd()) {
        data->recorder.record(RecordType::Stack, stack.ip());
        stack.next();
    }
    data->recorder.record(RecordType::Stack, std::numeric_limits<uint64_t>::max());

    return ret;
}

void* mmap64(void* addr, size_t length, int prot, int flags, int fd, __off64_t pgoffset)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

    // printf("mmap64?? %p\n", addr);
    auto ret = callbacks.mmap64(addr, length, prot, flags, fd, pgoffset);
    // printf("mmap64 %p??\n", ret);

    if (!::hooked || ret == MAP_FAILED)
        return ret;

    NoHook nohook;

    bool tracked = false;
    uint64_t allocated = 0;
    if (!::inMallocFree
        && (flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS)
        && fd == -1) {
        allocated = trackMmap(ret, length, prot, flags);
        tracked = true;
    }

    Recorder::Scope recordScope(&data->recorder);

    if (data->modulesDirty.load(std::memory_order_acquire)) {
        dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);
        data->modulesDirty.store(false, std::memory_order_release);
    }

    data->recorder.record(tracked ? RecordType::MmapTracked : RecordType::MmapUntracked,
                          mmap_ptr_cast(ret), alignToPage(length), allocated,
                          prot, flags, fd, static_cast<uint64_t>(pgoffset) * 4096, static_cast<uint32_t>(gettid()));

    Stack stack(0);
    while (!stack.atEnd()) {
        data->recorder.record(RecordType::Stack, stack.ip());
        stack.next();
    }
    data->recorder.record(RecordType::Stack, std::numeric_limits<uint64_t>::max());

    return ret;
}

int munmap(void* addr, size_t length)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

    if (!::hooked)
        return callbacks.munmap(addr, length);

    NoHook nohook;
    // if (len > 1000000) {
    //     printf("3--\n");
    //     for (const auto& item : data->mmapRanges) {
    //         printf(" - %p(%p) %zu\n", std::get<0>(item), reinterpret_cast<uint8_t*>(std::get<0>(item)) + std::get<1>(item), std::get<1>(item));
    //     }
    // }

    bool tracked = false;
    uint64_t deallocated = 0;
    MmapWalker::walk(addr, length, [&tracked, &deallocated](MmapWalker::RangeType type, auto it, void* waddr, size_t& wlength, size_t used) {
        switch (type) {
        case MmapWalker::RangeType::Start: {
            // update start item
            // printf("FUCK %p(%p) vs %p(%p)\n", addr, reinterpret_cast<uint8_t*>(addr) + len,
            //        std::get<0>(*it), reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it));
            tracked = true;
            const auto oldlen = std::get<1>(*it);
            std::get<1>(*it) = reinterpret_cast<uint8_t*>(waddr) - reinterpret_cast<uint8_t*>(std::get<0>(*it));
            // if addr is fully contained in this item, make another new item at the end
            if (reinterpret_cast<uint8_t*>(waddr) + wlength < reinterpret_cast<uint8_t*>(std::get<0>(*it)) + oldlen) {
                const auto remlen = (reinterpret_cast<uint8_t*>(std::get<0>(*it)) + oldlen) - (reinterpret_cast<uint8_t*>(waddr) + wlength);
                it = data->mmapRanges.insert(it + 1, std::make_tuple(reinterpret_cast<uint8_t*>(waddr) + wlength, remlen, std::get<2>(*it)));
                deallocated += wlength;
                wlength = 0;
            } else {
                // printf("FUCK AGAIN %zu vs %zu (%zu %zu)\n", len, oldlen - std::get<1>(*it), oldlen, std::get<1>(*it));
                assert(wlength >= oldlen - std::get<1>(*it));
                deallocated += oldlen - std::get<1>(*it);
                assert(std::min(oldlen - std::get<1>(*it), wlength) <= wlength);
                wlength -= std::min(oldlen - std::get<1>(*it), wlength);
            }
            return it + 1; }
        case MmapWalker::RangeType::Middle:
            tracked = true;
            wlength -= std::min(std::get<1>(*it), wlength);
            deallocated += std::get<1>(*it);
            return data->mmapRanges.erase(it);
        case MmapWalker::RangeType::End:
            // update end item
            tracked = true;
            reinterpret_cast<uint8_t*&>(std::get<0>(*it)) += wlength;
            std::get<1>(*it) -= wlength;
            deallocated += wlength;
            wlength = 0;
            return it + 1;
        default:
            return it + 1;
        }
        assert(false && "invalid MmapWalker::RangeType for mmap");
        __builtin_unreachable();
    });

    data->recorder.record(tracked ? RecordType::MunmapTracked : RecordType::MunmapUntracked,
                          mmap_ptr_cast(addr), alignToPage(length), deallocated);
    // if (updated) {
    //     printf("2--\n");
    //     for (const auto& item : data->mmapRanges) {
    //         printf(" - %p(%p) %zu\n", std::get<0>(item), reinterpret_cast<uint8_t*>(std::get<0>(item)) + std::get<1>(item), std::get<1>(item));
    //     }
    // }

    const auto ret = callbacks.munmap(addr, length);
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
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

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
                .len = alignToPage(len)
            },
            .mode = UFFDIO_REGISTER_MODE_MISSING,
            .ioctls = 0
        };

        if (ioctl(data->faultFd, UFFDIO_REGISTER, &reg) == -1)
            return callbacks.mprotect(addr, len, prot);

        if (reg.ioctls != UFFD_API_RANGE_IOCTLS) {
            printf("no range (2) 0x%llx\n", reg.ioctls);
            return callbacks.mprotect(addr, len, prot);
        } else {
            printf("got ok (2)\n");
        }
    }

    return callbacks.mprotect(addr, len, prot);
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

        return callbacks.mremap(addr, old_size, new_size, flags, new_address);
    }
    return callbacks.mremap(addr, old_size, new_size, flags);
}

int madvise(void* addr, size_t length, int advice)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

    if (!::hooked)
        return callbacks.madvise(addr, length, advice);

    NoHook nohook;

    bool tracked = false;
    uint64_t deallocated = 0;
    if (advice == MADV_DONTNEED) {
        MmapWalker::walk(addr, length, [&tracked, &deallocated](MmapWalker::RangeType type, auto it, void* waddr, size_t& wlength, size_t used) {
            switch (type) {
            case MmapWalker::RangeType::Start: {
                tracked = true;
                const auto atstart = reinterpret_cast<uint8_t*>(waddr) - reinterpret_cast<uint8_t*>(std::get<0>(*it));
                if (reinterpret_cast<uint8_t*>(waddr) + wlength < reinterpret_cast<uint8_t*>(std::get<0>(*it)) + std::get<1>(*it)) {
                    deallocated += wlength;
                    wlength = 0;
                } else {
                    deallocated += std::get<1>(*it) - atstart;
                    assert(std::min(std::get<1>(*it) - atstart, wlength) <= wlength);
                    wlength -= std::min(std::get<1>(*it) - atstart, wlength);
                }
                return it + 1; }
            case MmapWalker::RangeType::Middle:
                tracked = true;
                wlength -= std::min(std::get<1>(*it), wlength);
                deallocated += std::get<1>(*it);
                return it + 1;
            case MmapWalker::RangeType::End:
                tracked = true;
                deallocated += wlength;
                wlength = 0;
                return it + 1;
            default:
                return it + 1;
            }
            assert(false && "invalid MmapWalker::RangeType for madvise");
            __builtin_unreachable();
        });
    }

    data->recorder.record(tracked ? RecordType::MadviseTracked : RecordType::MadviseUntracked,
                          mmap_ptr_cast(addr), alignToPage(length), advice, deallocated);

    return callbacks.madvise(addr, length, advice);
}

void* dlopen(const char* filename, int flags)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }
    data->modulesDirty.store(true, std::memory_order_release);
    return callbacks.dlopen(filename, flags);
}

int dlclose(void* handle)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }
    if (data) {
        data->modulesDirty.store(true, std::memory_order_release);
    }
    return callbacks.dlclose(handle);
}

int pthread_setname_np(pthread_t thread, const char* name)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }
    // ### should fix this, this will drop unless we're the same thread
    if (pthread_equal(thread, pthread_self())) {
        NoHook nohook;
        data->recorder.record(RecordType::ThreadName, static_cast<uint32_t>(gettid()), Recorder::String(name));
    }
    return callbacks.pthread_setname_np(thread, name);
}

void* malloc(size_t size)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

    if (!callbacks.malloc) {
        return allocator.allocate(size);
    }

    auto ret = callbacks.malloc(size);
    if (!::hooked || !ret)
        return ret;

    if (!mallocFree.wasInMallocFree() && data)
        reportMalloc(ret, size);
    return ret;
}

void free(void* ptr)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

    if (allocator.hasData(ptr)) {
        return;
    }

    callbacks.free(ptr);

    if (!::hooked)
        return;

    if (!mallocFree.wasInMallocFree() && data)
        reportFree(ptr);
}

void* calloc(size_t nmemb, size_t size)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

    if (!callbacks.calloc) {
        return allocator.allocate(nmemb * size);
    }

    auto ret = callbacks.calloc(nmemb, size);
    if (!::hooked || !ret)
        return ret;

    if (!mallocFree.wasInMallocFree() && data)
        reportMalloc(ret, nmemb * size);
    return ret;
}

void* realloc(void* ptr, size_t size)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

    auto ret = callbacks.realloc(ptr, size);
    if (!::hooked || !ret)
        return ret;

    // printf("mmap?? %p\n", addr);
    if (!mallocFree.wasInMallocFree() && data) {
        if (ptr) {
            reportFree(ptr);
        }
        reportMalloc(ret, size);
    }
    return ret;
}

void* reallocarray(void* ptr, size_t nmemb, size_t size)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

    auto ret = callbacks.reallocarray(ptr, nmemb, size);
    if (!::hooked || !ret)
        return ret;

    // printf("mmap?? %p\n", addr);
    if (ptr) {
        reportFree(ptr);
    }

    if (!mallocFree.wasInMallocFree() && data)
        reportMalloc(ret, size * nmemb);
    return ret;
}

int posix_memalign(void** memptr, size_t alignment, size_t size)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

    auto ret = callbacks.posix_memalign(memptr, alignment, size);
    if (!::hooked || !*memptr || ret != 0)
        return ret;

    if (!mallocFree.wasInMallocFree() && data)
        reportMalloc(*memptr, alignToSize(size, alignment));
    return ret;
}

void* aligned_alloc(size_t alignment, size_t size)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

    auto ret = callbacks.aligned_alloc(alignment, size);
    if (!::hooked || !ret)
        return ret;

    if (!mallocFree.wasInMallocFree() && data)
        reportMalloc(ret, alignToSize(size, alignment));
    return ret;
}
} // extern "C"
