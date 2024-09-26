#include "Wasm.h"
#include "NoHook.h"
#include "Stack.h"
#include "HostEmitter.h"
#include <common/RecordType.h>
#include <common/Limits.h>

#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <atomic>
#include <mutex>

#include "dlmalloc.h"

extern "C" {
bool mtrack_enabled();
}

namespace {
inline uint32_t timestamp()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}
} // anonymous namespace

class Hooks
{
public:
    static void hook();

private:
    Hooks() = delete;
};

struct Data {
    uint8_t appId { 2 };
    std::map<std::string, uint64_t> urls;
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

static void hookCleanup()
{
}

void Hooks::hook()
{
    if(!mtrack_enabled()) {
        safePrint("Mtrack: unable to hook\n");
        ::hooked = false;
        return;
    }
    data = new Data();
    atexit(hookCleanup);

    {
        HostEmitter emitter;
        emitter.emit(RecordType::Start, data->appId, ApplicationType::WASM, 0);
        emitter.emit(RecordType::Executable, data->appId, Emitter::String("http://www.netflix.com"));
    }
    safePrint("Mtrack: hooked\n");
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

static void reportMalloc(void* ptr, size_t size)
{
    NoHook nohook;
    Stack stack(3);
    HostEmitter emitter;
    for(size_t i = 0; i < stack.count(); ++i) {
        const std::string &url = stack.url(i);
        const uint64_t ptr = *(stack.ptrs() + i);
        printf("Got %llx\n", ptr);
        assert(!(ptr >> 32));
        auto u = data->urls.find(url);
        if(u == data->urls.end()) {
            const uint64_t id = (uint64_t(data->urls.size()) << 32);
            data->urls[url] = id;
            stack.setPtr(i, ptr | id);
            emitter.emit(RecordType::Library, data->appId, Emitter::String(url), id);
            emitter.emit(RecordType::LibraryHeader, data->appId, id, static_cast<uint64_t>(id | 0xFFFFFFFF));
        } else {
            stack.setPtr(i, ptr | u->second);
        }
    }
    emitter.emit(RecordType::Malloc,
                 data->appId, timestamp(), static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)),
                 static_cast<uint64_t>(size), static_cast<uint32_t>(gettid()), stack);
}

static void reportFree(void* ptr)
{
    NoHook nohook;
    HostEmitter emitter;
    emitter.emit(RecordType::Free, data->appId, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)));
}

extern "C" {
void *malloc(size_t size)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree()) {
        std::call_once(hookOnce, Hooks::hook);
    }

    auto ret = dlmalloc(size);
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

    dlfree(ptr);

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

    auto ret = dlcalloc(nmemb, size);
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

    auto ret = dlrealloc(ptr, size);
    if (!::hooked || !ret)
        return ret;

    if (!mallocFree.wasInMallocFree() && data) {
        if (ptr)
            reportFree(ptr);
        reportMalloc(ret, size);
    }
    return ret;
}

} // extern "C"
