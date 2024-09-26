#include "Preload.h"
#include "NoHook.h"
#include "PipeEmitter.h"
#include "Spinlock.h"
#include "Stack.h"
#include <common/MmapTracker.h>
#include <common/RecordType.h>
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
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <execinfo.h>
#include <pthread.h>

#include <cstdarg>
#include <atomic>
#include <mutex>
#include <thread>
#include <tuple>

#ifndef SYS_userfaultfd
#if defined(__x86_64__)
#define SYS_userfaultfd 323
#elif defined(__i386__)
#define SYS_userfaultfd 374
#elif defined(__arm__)
#define SYS_userfaultfd 388
#else
#error "Unsupported architecture"
#endif
#endif

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
typedef int (*PthreadSetnameSig)(pthread_t, const char*);
typedef void* (*MallocSig)(size_t);
typedef void (*FreeSig)(void*);
typedef void* (*CallocSig)(size_t, size_t);
typedef void* (*ReallocSig)(void*, size_t);
typedef void* (*ReallocArraySig)(void*, size_t, size_t);
typedef int (*Posix_MemalignSig)(void **, size_t, size_t);
typedef void* (*Aligned_AllocSig)(size_t, size_t);

namespace {
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
} // anonymous namespace

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
    int faultFd {};
    pid_t pid {};
    std::thread thread;
    uint8_t appId { 1 };
    uint32_t started { 0 };
    std::atomic_flag isShutdown = ATOMIC_FLAG_INIT;
    std::atomic<bool> modulesDirty = true;

    int pfThreadPipe[2] { -1, -1 };
    int emitPipe[2] { -1, -1 };

    Spinlock mmapTrackerLock;
    MmapTracker mmapTracker;
} *data = nullptr;

namespace {
inline uint32_t timestamp()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
    return static_cast<uint32_t>((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000)) - data->started;
}
}

namespace {
bool safePrint(const char *string)
{
    const ssize_t len = strlen(string);
    return ::write(STDOUT_FILENO, string, len) == len;
}

struct TLSData
{
    bool hooked = true;
    bool inMallocFree = false;
};

struct TLSInit
{
    TLSInit();

    pthread_key_t tlsKey;
};

TLSInit::TLSInit()
{
    pthread_key_create(&tlsKey, nullptr);
}

static TLSData* tlsData()
{
    static TLSInit tlsInit;

    void* ptr = pthread_getspecific(tlsInit.tlsKey);
    if (ptr == nullptr) {
        static MallocSig realMalloc = nullptr;
        if (realMalloc == nullptr) {
            realMalloc = reinterpret_cast<MallocSig>(dlsym(RTLD_NEXT, "malloc"));
        }
        ptr = realMalloc(sizeof(TLSData));
        new (ptr) TLSData();
        pthread_setspecific(tlsInit.tlsKey, ptr);
    }
    return static_cast<TLSData*>(ptr);
}
} // anonymous namespace

NoHook::NoHook()
    : wasHooked(::tlsData()->hooked)
{
    ::tlsData()->hooked = false;
}
NoHook::~NoHook()
{
    ::tlsData()->hooked = wasHooked;
}

class MallocFree
{
public:
    MallocFree()
        : mPrev(::tlsData()->inMallocFree)
    {
        ::tlsData()->inMallocFree = true;
    }

    ~MallocFree()
    {
        ::tlsData()->inMallocFree = mPrev;
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
    emitter.emit(RecordType::Library, data->appId, Emitter::String(fileName), static_cast<uint64_t>(info->dlpi_addr));

    for (int i = 0; i < info->dlpi_phnum; i++) {
        const auto& phdr = info->dlpi_phdr[i];
        if (phdr.p_type == PT_LOAD) {
            emitter.emit(RecordType::LibraryHeader, data->appId, static_cast<uint64_t>(phdr.p_vaddr), static_cast<uint64_t>(phdr.p_memsz));
        }
    }

    return 0;
}

static void hookThread()
{
    ::tlsData()->hooked = false;

    PipeEmitter emitter(data->emitPipe[1]);

    pollfd evt[] = {
        { .fd = data->faultFd, .events = POLLIN, .revents = 0 },
        { .fd = data->pfThreadPipe[0], .events = POLLIN, .revents = 0 }
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
            uffd_msg fault_msg = {};
            const auto r = read(data->faultFd, &fault_msg, sizeof(fault_msg));
            if (r == sizeof(fault_msg)) {
                // printf("- fault thread 3 0x%x\n", fault_msg.event);
                switch (fault_msg.event) {
                case UFFD_EVENT_PAGEFAULT: {
                    const auto place = static_cast<uint64_t>(fault_msg.arg.pagefault.address);
                    const auto ptid = static_cast<uint32_t>(fault_msg.arg.pagefault.feat.ptid);
                    // printf("  - pagefault %u\n", ptid);
                    emitter.emit(RecordType::PageFault, data->appId, timestamp(), place, ptid, Stack(2, ptid));
                    uffdio_zeropage zero = {
                        .range = {
                            .start = place & ~(Limits::PageSize - 1),
                            .len = Limits::PageSize
                        },
                        .mode = 0,
                        .zeropage = 0
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
                case UFFD_EVENT_REMAP: {
                    const auto from = static_cast<uint64_t>(fault_msg.arg.remap.from);
                    const auto to = static_cast<uint64_t>(fault_msg.arg.remap.to);
                    const auto len = static_cast<uint64_t>(fault_msg.arg.remap.len);
                    emitter.emit(RecordType::PageRemap, data->appId, from, to, len);
                    break; }
                case UFFD_EVENT_REMOVE:
                case UFFD_EVENT_UNMAP: {
                    const auto start = static_cast<uint64_t>(fault_msg.arg.remove.start);
                    const auto end = static_cast<uint64_t>(fault_msg.arg.remove.end);
                    emitter.emit(RecordType::PageRemove, data->appId, start, end);
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
        EINTRWRAP(w, ::write(data->pfThreadPipe[1], "q", 1));
        data->thread.join();
    }
    NoHook noHook;
    Data *d = data;
    data = nullptr;

    if (d->emitPipe[1] != -1) {
        int e;
        EINTRWRAP(e, ::close(d->emitPipe[1]));
        d->emitPipe[1] = -1;
    }
    printf("Calling waitpid %d\n", d->pid);
    int r;
    int wstatus = 0;
    EINTRWRAP(r, ::waitpid(d->pid, &wstatus, 0));
    printf("Waitpid returned %d -> %d (%d)\n", r, wstatus, WEXITSTATUS(wstatus));
    delete d;
}

void Hooks::hook()
{
    unsetenv("LD_PRELOAD");

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

    callbacks.posix_memalign = reinterpret_cast<Posix_MemalignSig>(dlsym(RTLD_NEXT, "posix_memalign"));
    if (callbacks.posix_memalign == nullptr) {
        safePrint("no posix_memalign\n");
        abort();
    }

    callbacks.aligned_alloc = reinterpret_cast<Aligned_AllocSig>(dlsym(RTLD_NEXT, "aligned_alloc"));
    if (callbacks.aligned_alloc == nullptr) {
        safePrint("no aligned_alloc\n");
        abort();
    }

    callbacks.reallocarray = reinterpret_cast<ReallocArraySig>(dlsym(RTLD_NEXT, "reallocarray"));

    data = new Data();

    if (::pipe2(data->emitPipe, O_DIRECT) == -1) {
        safePrint("no emitPipe\n");
        abort();
    }

    const auto maybeNoMmap = getenv("MTRACK_NO_MMAP_STACKS");
    if (maybeNoMmap != nullptr) {
        if (!strncasecmp(maybeNoMmap, "true", 4) || !strncmp(maybeNoMmap, "1", 1)) {
            Stack::setNoMmap();
        }
    }

    const auto ppid = getpid();

    int e;
    pid_t pid = fork();
    if (pid == 0) {
        // child

        // ignore sigint, the parent will tell the child when it's time to quit
        signal(SIGINT, SIG_IGN);

        NoHook nohook;

        EINTRWRAP(e, ::close(data->emitPipe[1]));
        EINTRWRAP(e, ::dup2(data->emitPipe[0], STDIN_FILENO));
        EINTRWRAP(e, ::close(data->emitPipe[0]));

        std::string parser;
        const char* cparser = getenv("MTRACK_PARSER");
        if (cparser != nullptr) {
            parser = cparser;
        }

        char buf[4096];
        if (parser.empty()) {
            std::string self;
            dl_iterate_phdr([](struct dl_phdr_info* info, size_t /*size*/, void* d) {
                if (strstr(info->dlpi_name, "libmtrack_preload") != nullptr
                    || strstr(info->dlpi_name, "libmtrack32_preload") != nullptr
                    || strstr(info->dlpi_name, "libmtrack64_preload") != nullptr) {
                    *reinterpret_cast<std::string*>(d) = std::string(info->dlpi_name);
                    return 1;
                }
                return 0;
            }, &self);

            if (self.empty()) {
                fprintf(stderr, "could not find the preload path\n");
                abort();
            }
            if (realpath(self.c_str(), buf)) {
                self = buf;
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

        char* args[14] = {};
        size_t argIdx = 0;
        args[argIdx++] = strdup(parser.c_str());
        args[argIdx++] = strdup("--packet-mode");
        const char* log = getenv("MTRACK_LOG_FILE");
        if (log) {
            args[argIdx++] = strdup("--log-file");
            args[argIdx++] = strdup(log);
        }
        const char* output = getenv("MTRACK_OUTPUT");
        if (output) {
            args[argIdx++] = strdup("--output");
            args[argIdx++] = strdup(output);
        }
        const char* dump = getenv("MTRACK_DUMP");
        if (dump) {
            args[argIdx++] = strdup("--dump");
            args[argIdx++] = strdup(dump);
        }
        const char* nob = getenv("MTRACK_NO_BUNDLE");
        if (nob) {
            args[argIdx++] = strdup("--no-bundle");
        }
        const char* threshold = getenv("MTRACK_THRESHOLD");
        if (threshold) {
            args[argIdx++] = strdup("--threshold");
            args[argIdx++] = strdup(threshold);
        }
        snprintf(buf, sizeof(buf), "%u", ppid);
        args[argIdx++] = strdup("--pid");
        args[argIdx++] = strdup(buf);
        args[argIdx++] = nullptr;
        char* envs[1] = {};
        envs[0] = nullptr;
        const int ret = execve(parser.c_str(), args, envs);
        fprintf(stderr, "unable to execve '%s' %d %m\n", parser.c_str(), ret);
        abort();
    } else {
        data->pid = pid;
        // parent
        EINTRWRAP(e, ::close(data->emitPipe[0]));
    }

    data->faultFd = syscall(SYS_userfaultfd, O_NONBLOCK);
    if (data->faultFd == -1) {
        safePrint("could not initialize userfaultfd\nyou might have to run sysctl -w vm.unprivileged_userfaultfd=1\n");
        abort();
    }

    uffdio_api api = {
        .api = UFFD_API,
        .features = UFFD_FEATURE_THREAD_ID,
        .ioctls = 0
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

    PipeEmitter emitter(data->emitPipe[1]);
    emitter.emit(RecordType::Start, data->appId, 0);

    data->thread = std::thread(hookThread);
    data->started = timestamp();
    atexit(hookCleanup);

    // record the executable file
    char buf1[512];
    char buf2[4096];
    snprintf(buf1, sizeof(buf1), "/proc/%u/exe", ppid);
    ssize_t l = readlink(buf1, buf2, sizeof(buf2));
    if (l == -1 || l == sizeof(buf2)) {
        // badness
        fprintf(stderr, "no exe\n");
    } else {
        emitter.emit(RecordType::Executable, data->appId, Emitter::String(buf2, l));
    }

    // record the working directory
    // ### should probably hook chdir and fchdir to record runtime changes
    if (getcwd(buf2, sizeof(buf2)) == nullptr) {
        // badness
        fprintf(stderr, "no cwd\n");
    } else {
        emitter.emit(RecordType::WorkingDirectory, data->appId, Emitter::String(buf2));
    }

    safePrint("hook.\n");
}

static void trackMmap(void* addr, size_t length, int prot, int flags)
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

    if ((prot & (PROT_READ | PROT_WRITE)) == (PROT_READ | PROT_WRITE)) {
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

        // if (reg.ioctls != UFFD_API_RANGE_IOCTLS) {
        //     printf("no range (1) 0x%llx\n", reg.ioctls);
        //     return;
        // }
    }
}

static void reportMalloc(void* ptr, size_t size)
{
    NoHook nohook;

    if (data->modulesDirty.load(std::memory_order_acquire)) {
        dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);
        data->modulesDirty.store(false, std::memory_order_release);
    }

    PipeEmitter emitter(data->emitPipe[1]);
    emitter.emit(RecordType::Malloc,
                 data->appId,
                 timestamp(),
                 static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)),
                 static_cast<uint64_t>(size),
                 static_cast<uint32_t>(syscall(SYS_gettid)),
                 Stack(3));
}

static void reportFree(void* ptr)
{
    NoHook nohook;

    if (data->modulesDirty.load(std::memory_order_acquire)) {
        dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);
        data->modulesDirty.store(false, std::memory_order_release);
    }

    PipeEmitter emitter(data->emitPipe[1]);
    emitter.emit(RecordType::Free, data->appId, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)));
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

    if (!::tlsData()->hooked || ret == MAP_FAILED)
        return ret;

    NoHook nohook;

    if (!mallocFree.wasInMallocFree()
        && (flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS)
        && fd == -1) {
        trackMmap(ret, length, prot, flags);

        if (flags & MAP_FIXED) {
            PipeEmitter emitter(data->emitPipe[1]);
            emitter.emit(RecordType::PageRemove, data->appId, mmap_ptr_cast(addr), mmap_ptr_cast(addr) + alignToPage(length));
        }
    }

    if (data->modulesDirty.load(std::memory_order_acquire)) {
        dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);
        data->modulesDirty.store(false, std::memory_order_release);
    }
    PipeEmitter emitter(data->emitPipe[1]);
    emitter.emit(RecordType::Mmap, data->appId,
                 mmap_ptr_cast(ret), alignToPage(length), prot, flags,
                 static_cast<uint32_t>(syscall(SYS_gettid)), Stack(2));
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

    if (!::tlsData()->hooked || ret == MAP_FAILED)
        return ret;

    NoHook nohook;

    if (!mallocFree.wasInMallocFree()
        && (flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS)
        && fd == -1) {
        trackMmap(ret, length, prot, flags);

        if (flags & MAP_FIXED) {
            PipeEmitter emitter(data->emitPipe[1]);
            emitter.emit(RecordType::PageRemove, data->appId, mmap_ptr_cast(addr), mmap_ptr_cast(addr) + alignToPage(length));
        }
    }

    if (data->modulesDirty.load(std::memory_order_acquire)) {
        dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);
        data->modulesDirty.store(false, std::memory_order_release);
    }
    PipeEmitter emitter(data->emitPipe[1]);
    emitter.emit(RecordType::Mmap,data->appId,
                 mmap_ptr_cast(ret), alignToPage(length), prot, flags,
                 static_cast<uint32_t>(syscall(SYS_gettid)), Stack(2));

    return ret;
}

int munmap(void* addr, size_t length)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

    if (!::tlsData()->hooked)
        return callbacks.munmap(addr, length);

    NoHook nohook;

    {
        ScopedSpinlock lock(data->mmapTrackerLock);
        data->mmapTracker.munmap(addr, length);
    }

    PipeEmitter emitter(data->emitPipe[1]);
    emitter.emit(RecordType::Munmap, data->appId,
                 mmap_ptr_cast(addr), alignToPage(length));

    return callbacks.munmap(addr, length);
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
    if (((prot & (PROT_READ | PROT_WRITE)) == (PROT_READ | PROT_WRITE)) && (flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS)) {
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

        // ### don't forget me
        // if (reg.ioctls != UFFD_API_RANGE_IOCTLS) {
        //     printf("no range (2) 0x%llx\n", reg.ioctls);
        //     return callbacks.mprotect(addr, len, prot);
        // }
    }

    return callbacks.mprotect(addr, len, prot);
}

void* mremap(void* addr, size_t old_size, size_t new_size, int flags, ...)
{
    abort();
    void* ret;
    if (flags & MREMAP_FIXED) {
        va_list ap;
        va_start(ap, flags);
        void* new_address = va_arg(ap, void*);
        va_end(ap);

        ret = callbacks.mremap(addr, old_size, new_size, flags, new_address);
        if (ret != MAP_FAILED) {
            {
                ScopedSpinlock lock(data->mmapTrackerLock);
                data->mmapTracker.munmap(new_address, new_size);
            }
            PipeEmitter emitter(data->emitPipe[1]);
            emitter.emit(RecordType::Munmap, data->appId,
                         mmap_ptr_cast(new_address), alignToPage(new_size));
        }
    } else {
        ret = callbacks.mremap(addr, old_size, new_size, flags);
    }
    if (ret == MAP_FAILED) {
        return ret;
    }

    {
        ScopedSpinlock lock(data->mmapTrackerLock);
        data->mmapTracker.mremap(addr, ret, old_size, new_size, 0);
    }

    PipeEmitter emitter(data->emitPipe[1]);
    emitter.emit(RecordType::Mremap, data->appId, mmap_ptr_cast(addr), alignToPage(old_size),
                 mmap_ptr_cast(ret), alignToPage(new_size), flags, syscall(SYS_gettid), Stack(2));

    return ret;
}

int madvise(void* addr, size_t length, int advice)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

    if (!::tlsData()->hooked)
        return callbacks.madvise(addr, length, advice);

    NoHook nohook;

    if (advice == MADV_DONTNEED || advice == MADV_REMOVE) {
        {
            ScopedSpinlock lock(data->mmapTrackerLock);
            data->mmapTracker.madvise(addr, length);
        }

        {
            PipeEmitter emitter(data->emitPipe[1]);
            emitter.emit(RecordType::PageRemove, data->appId, mmap_ptr_cast(addr), mmap_ptr_cast(addr) + alignToPage(length));
        }
    }

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
        emitter.emit(RecordType::ThreadName, data->appId, static_cast<uint32_t>(syscall(SYS_gettid)), Emitter::String(name));
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
    if (!::tlsData()->hooked || !ret)
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

    if (!::tlsData()->hooked)
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
    if (!::tlsData()->hooked || !ret)
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
    if (!::tlsData()->hooked || !ret)
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
    if (!::tlsData()->hooked || !ret)
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
    if (!::tlsData()->hooked || !*memptr || ret != 0)
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
    if (!::tlsData()->hooked || !ret)
        return ret;

    if (!mallocFree.wasInMallocFree() && data)
        reportMalloc(ret, alignToSize(size, alignment));
    return ret;
}

void mtrack_writeBytes(const unsigned char *bytes, size_t size)
{
    if(data) {
        MallocFree mallocFree;
        PipeEmitter emitter(data->emitPipe[1]);
        emitter.writeBytes(bytes, size, Emitter::WriteType::Last);
    }
}

void mtrack_snapshot(const char* name, size_t nameSize)
{
    if (data) {
        PipeEmitter emitter(data->emitPipe[1]);
        if (name != nullptr) {
            if (nameSize == 0)
                nameSize = strlen(name);
            emitter.emit(RecordType::Command, CommandType::Snapshot, Emitter::String(name, nameSize));
        } else {
            emitter.emit(RecordType::Command, CommandType::Snapshot, static_cast<uint32_t>(0));
        }
    }
}

void mtrack_disable_snapshots()
{
    if (data) {
        PipeEmitter emitter(data->emitPipe[1]);
        emitter.emit(RecordType::Command, CommandType::DisableSnapshots);
    }
}

void mtrack_enable_snapshots()
{
    if (data) {
        PipeEmitter emitter(data->emitPipe[1]);
        emitter.emit(RecordType::Command, CommandType::EnableSnapshots);
    }
}
} // extern "C"
