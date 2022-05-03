#include "NoHook.h"
#include "PipeEmitter.h"
#include "Spinlock.h"
#include "Stack.h"
#include <common/MmapTracker.h>
#include <common/RecordType.h>
#include <common/Version.h>
#include <common/Limits.h>

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

#include <cstdarg>
#include <atomic>
#include <mutex>
#include <thread>
#include <tuple>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#define EINTRWRAP(VAR, BLOCK)                   \
    do {                                        \
        VAR = BLOCK;                            \
    } while (VAR == -1 && errno == EINTR)

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
    return size + (((~size) + 1) & (Limits::PageSize - 1));
}

inline uint64_t alignToSize(uint64_t size, uint64_t align)
{
    return size + (((~size) + 1) & (align - 1));
}

inline uint64_t mmap_ptr_cast(void *ptr)
{
    const uint64_t ret = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr));
    return ret + (Limits::PageSize - (ret % Limits::PageSize)) - Limits::PageSize;
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

    int pfThreadPipe[2];
    int emitPipe[2];

    Spinlock mmapTrackerLock;
    MmapTracker mmapTracker;
} *data = nullptr;

namespace {
bool safePrint(const char *string)
{
    const ssize_t len = strlen(string);
    return ::write(STDOUT_FILENO, string, len) == len;
}

thread_local bool hooked = true;
thread_local bool inMallocFree = false;
} // anonymous namespace

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

static int dl_iterate_phdr_callback(struct dl_phdr_info* info, size_t /*size*/, void* /*data*/)
{
    const char* fileName = info->dlpi_name;
    if (!fileName || !fileName[0]) {
        fileName = "s";
    }

    PipeEmitter emitter(data->emitPipe[1]);
    emitter.emit(RecordType::Library, Emitter::String(fileName), static_cast<uint64_t>(info->dlpi_addr));

    for (int i = 0; i < info->dlpi_phnum; i++) {
        const auto& phdr = info->dlpi_phdr[i];
        if (phdr.p_type == PT_LOAD) {
            emitter.emit(RecordType::LibraryHeader, static_cast<uint64_t>(phdr.p_vaddr), static_cast<uint64_t>(phdr.p_memsz));
        }
    }

    return 0;
}

static void hookThread()
{
    hooked = false;

    PipeEmitter emitter(data->emitPipe[1]);

    pollfd evt[] = {
        { .fd = data->faultFd, .events = POLLIN },
        { .fd = data->pfThreadPipe[0], .events = POLLIN }
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
            const auto r = read(data->faultFd, &fault_msg, sizeof(fault_msg));
            if (r == sizeof(fault_msg)) {
                // printf("- fault thread 3 0x%x\n", fault_msg.event);
                switch (fault_msg.event) {
                case UFFD_EVENT_PAGEFAULT: {
                    const auto place = static_cast<uint64_t>(fault_msg.arg.pagefault.address);
                    const auto ptid = static_cast<uint32_t>(fault_msg.arg.pagefault.feat.ptid);
                    // printf("  - pagefault %u\n", ptid);
                    emitter.emit(RecordType::PageFault, place, ptid, Stack(ptid));
                    uffdio_zeropage zero = {
                        .range = {
                            .start = place,
                            .len = Limits::PageSize
                        },
                        .mode = 0
                    };
                    const auto ir = ioctl(data->faultFd, UFFDIO_ZEROPAGE, &zero);
                    if (ir == -1 && errno != EEXIST) {
                        // boo
                        close(data->faultFd);
                        data->faultFd = -1;
                        printf("- pagefault error 3 %d %d %m\n", ir, errno);
                        return;
                    }
                    // printf("  - handled pagefault\n");
                    break; }
                }
            } else if (r != -1 || (errno != EWOULDBLOCK && errno != EAGAIN)) {
                // read error
                close(data->faultFd);
                data->faultFd = -1;
                printf("- pagefault error 2\n");
                return;
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
            w = ::write(data->pfThreadPipe[1], "q", 1);
        } while (w == -1 && errno == EINTR);
        data->thread.join();
    }
    if (data->emitPipe[1] != -1) {
        int e;
        EINTRWRAP(e, ::close(data->emitPipe[1]));
        data->emitPipe[1] = -1;
    }
    NoHook noHook;
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

    if (::pipe2(data->emitPipe, O_DIRECT) == -1) {
        safePrint("no emitPipe\n");
        abort();
    }

    int e;
    const auto pid = fork();
    if (pid == 0) {
        // child
        NoHook nohook;

        EINTRWRAP(e, ::close(data->emitPipe[1]));
        EINTRWRAP(e, ::dup2(data->emitPipe[0], STDIN_FILENO));
        EINTRWRAP(e, ::close(data->emitPipe[0]));

        std::string parser;
        const char* cparser = getenv("MTRACK_PARSER");
        if (cparser != nullptr) {
            parser = cparser;
        }

        if (parser.empty()) {
            std::string self;
            dl_iterate_phdr([](struct dl_phdr_info* info, size_t /*size*/, void* data) {
                if (strstr(info->dlpi_name, "libmtrack_preload") != nullptr) {
                    *reinterpret_cast<std::string*>(data) = std::string(info->dlpi_name);
                    return 1;
                }
                return 0;
            }, &self);

            if (self.empty()) {
                fprintf(stderr, "could not find the preload path\n");
                abort();
            }

            // find the slash
            auto slash = self.find_last_of('/');
            slash = self.find_last_of('/', slash - 1);
            if (slash == std::string::npos) {
                fprintf(stderr, "invalid preload path '%s'\n", self.c_str());
                abort();
            }

            parser = self.substr(0, slash + 1) + "bin/mtrack_parser";
        }

        char* args[7] = {};
        size_t argIdx = 0;
        args[argIdx++] = strdup(parser.c_str());
        args[argIdx++] = strdup("--packet-mode");
        const char* log = getenv("MTRACK_PARSER_LOG");
        if (log) {
            args[argIdx++] = strdup("--log-file");
            args[argIdx++] = strdup(log);
        }
        const char* dump = getenv("MTRACK_PARSER_DUMP");
        if (dump) {
            args[argIdx++] = strdup("--dump");
            args[argIdx++] = strdup(dump);
        }
        args[argIdx++] = nullptr;
        char* envs[1] = {};
        envs[0] = nullptr;
        const int ret = execve(parser.c_str(), args, envs);
        fprintf(stderr, "unable to execve '%s' %d %m\n", parser.c_str(), ret);
        abort();
    } else {
        // parent
        EINTRWRAP(e, ::close(data->emitPipe[0]));
    }

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

    if (::pipe2(data->pfThreadPipe, O_NONBLOCK) == -1) {
        safePrint("no pfThreadPipe\n");
        abort();
    }

    data->thread = std::thread(hookThread);
    atexit(hookCleanup);

    PipeEmitter emitter(data->emitPipe[1]);

    // record the executable file
    char buf1[512];
    char buf2[4096];
    snprintf(buf1, sizeof(buf1), "/proc/%u/exe", getpid());
    ssize_t l = readlink(buf1, buf2, sizeof(buf2));
    if (l == -1 || l == sizeof(buf2)) {
        // badness
        fprintf(stderr, "no exe\n");
    } else {
        emitter.emit(RecordType::Executable, Emitter::String(buf2, l));
    }

    // record the working directory
    // ### should probably hook chdir and fchdir to record runtime changes
    if (getcwd(buf2, sizeof(buf2)) == nullptr) {
        // badness
        fprintf(stderr, "no cwd\n");
    } else {
        emitter.emit(RecordType::WorkingDirectory, Emitter::String(buf2));
    }

    NoHook nohook;

    unw_set_caching_policy(unw_local_addr_space, UNW_CACHE_PER_THREAD);
    unw_set_cache_size(unw_local_addr_space, 1024, 0);

    safePrint("hook.\n");
}

void trackMmap(void* addr, size_t length, int prot, int flags)
{
    // printf("-maping %p %zu flags 0x%x priv/anon %d\n", addr, length, flags, (flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS));
    {
        ScopedSpinlock lock(data->mmapTrackerLock);
        data->mmapTracker.mmap(addr, length, prot, flags, 0);
    }
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
            return;
        }

        if (reg.ioctls != UFFD_API_RANGE_IOCTLS) {
            printf("no range (1) 0x%llx\n", reg.ioctls);
            return;
        }
    }
}

void reportMalloc(void* ptr, size_t size)
{
    NoHook nohook;

    if (data->modulesDirty.load(std::memory_order_acquire)) {
        dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);
        data->modulesDirty.store(false, std::memory_order_release);
    }

    PipeEmitter emitter(data->emitPipe[1]);
    emitter.emit(RecordType::Malloc,
                 static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)),
                 static_cast<uint64_t>(size),
                 static_cast<uint32_t>(gettid()),
                 Stack());
}

void reportFree(void* ptr)
{
    NoHook nohook;

    if (data->modulesDirty.load(std::memory_order_acquire)) {
        dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);
        data->modulesDirty.store(false, std::memory_order_release);
    }

    PipeEmitter emitter(data->emitPipe[1]);
    emitter.emit(RecordType::Free, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)));
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
    if (!mallocFree.wasInMallocFree()
        && (flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS)
        && fd == -1) {
        trackMmap(ret, length, prot, flags);
        tracked = true;
    }

    if (data->modulesDirty.load(std::memory_order_acquire)) {
        dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);
        data->modulesDirty.store(false, std::memory_order_release);
    }
    PipeEmitter emitter(data->emitPipe[1]);
    emitter.emit(tracked ? RecordType::MmapTracked : RecordType::MmapUntracked,
                 mmap_ptr_cast(ret), alignToPage(length), prot, flags,
                 static_cast<uint32_t>(gettid()), Stack());
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
    if (!mallocFree.wasInMallocFree()
        && (flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS)
        && fd == -1) {
        trackMmap(ret, length, prot, flags);
        tracked = true;
    }

    if (data->modulesDirty.load(std::memory_order_acquire)) {
        dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);
        data->modulesDirty.store(false, std::memory_order_release);
    }
    PipeEmitter emitter(data->emitPipe[1]);
    emitter.emit(tracked ? RecordType::MmapTracked : RecordType::MmapUntracked,
                 mmap_ptr_cast(ret), alignToPage(length), prot, flags,
                 static_cast<uint32_t>(gettid()), Stack());

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

    uint64_t deallocated;
    {
        ScopedSpinlock lock(data->mmapTrackerLock);
        deallocated = data->mmapTracker.munmap(addr, length);
    }

    PipeEmitter emitter(data->emitPipe[1]);
    emitter.emit(deallocated > 0 ? RecordType::MunmapTracked : RecordType::MunmapUntracked,
                 mmap_ptr_cast(addr), alignToPage(length));
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

    int flags;
    {
        ScopedSpinlock lock(data->mmapTrackerLock);
        flags = data->mmapTracker.mprotect(addr, len, prot);
        // if (flags == 0) {
        //     printf("for prot %p\n", addr);
        //     data->mmapTracker.forEach([](uintptr_t start, uintptr_t end, int prot, int flags) {
        //         printf("mmap 0x%lx 0x%lx 0x%x 0x%x\n",
        //                start, end, prot, flags);
        //     });
        // }
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

    uint64_t deallocated = 0;
    if (advice == MADV_DONTNEED) {
        ScopedSpinlock lock(data->mmapTrackerLock);
        deallocated = data->mmapTracker.madvise(addr, length);
    }

    PipeEmitter emitter(data->emitPipe[1]);
    emitter.emit(deallocated > 0 ? RecordType::MadviseTracked : RecordType::MadviseUntracked,
                 mmap_ptr_cast(addr), alignToPage(length), advice);

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
        PipeEmitter emitter(data->emitPipe[1]);
        emitter.emit(RecordType::ThreadName, static_cast<uint32_t>(gettid()), Emitter::String(name));
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

    if (!mallocFree.wasInMallocFree() && ptr && data)
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
