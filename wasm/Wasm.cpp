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

#include <emscripten/bind.h>
#include <emscripten/emscripten.h>

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
    Stack stack(7);
    HostEmitter emitter;
    for(size_t i = 0; i < stack.count(); ++i) {
        const std::string &url = stack.url(i);
        const uint64_t ptr = *(stack.ptrs() + i);
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
    //printf("Malloc %p [%d]\n", ptr, size);
}

static void reportFree(void* ptr)
{
    NoHook nohook;
    HostEmitter emitter;
    emitter.emit(RecordType::Free, data->appId, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)));
    //printf("Free %p\n", ptr);
}

extern "C" {

EMSCRIPTEN_KEEPALIVE void mtrack_report_malloc(void *ptr, size_t size)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree())
        std::call_once(hookOnce, Hooks::hook);
    if (::hooked && !mallocFree.wasInMallocFree() && data)
        reportMalloc(ptr, size);
}

EMSCRIPTEN_KEEPALIVE void mtrack_report_free(void *ptr)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree())
        std::call_once(hookOnce, Hooks::hook);
    if (::hooked && !mallocFree.wasInMallocFree() && ptr && data)
        reportFree(ptr);
}

EMSCRIPTEN_KEEPALIVE void mtrack_report_realloc(void *oldPtr, void *newPtr, size_t size)
{
    MallocFree mallocFree;
    if (!mallocFree.wasInMallocFree())
        std::call_once(hookOnce, Hooks::hook);
    if (::hooked && !mallocFree.wasInMallocFree() && data) {
        if(oldPtr)
            reportFree(oldPtr);
        reportMalloc(newPtr, size);
    }
}

} // extern "C"

EMSCRIPTEN_BINDINGS(Mtrack) {
    emscripten::function("mtrack_report_realloc", &mtrack_report_realloc, emscripten::allow_raw_pointers());
    emscripten::function("mtrack_report_malloc", &mtrack_report_malloc, emscripten::allow_raw_pointers());
    emscripten::function("mtrack_report_free", &mtrack_report_free, emscripten::allow_raw_pointers());
}

extern "C" {
void *mtrack_force_bindings()
{
    return (void*)embind_init_Mtrack;
}
}
