#include "Module.h"
#include "Creatable.h"
#include <cstring>
#include <libbacktrace/backtrace.h>
#include <cxxabi.h>

extern "C" {
#include <libbacktrace/internal.h>
}

std::unordered_map<uint64_t, std::weak_ptr<Module>> Module::sModuleByName;

static inline uint64_t sdbm(const uint8_t* str)
{
    uint64_t hash = 0;
    int c;

    while ((c = *str++)) {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

static inline std::string demangle(const char* function)
{
    if (!function) {
        return {};
    } else if (function[0] != '_' || function[1] != 'Z') {
        return function;
    }

    std::string ret;
    int status = 0;
    char* demangled = abi::__cxa_demangle(function, 0, 0, &status);
    if (demangled && status == 0) {
        ret = demangled;
        free(demangled);
    }
    return {};
}


Module::Module(const char* filename, uint64_t addr)
    : mFileName(filename), mAddr(addr)
{
    auto state = backtrace_create_state(mFileName.c_str(), false, btErrorHandler, this);
    if (!state)
        return;

    const int descriptor = backtrace_open(mFileName.c_str(), btErrorHandler, this, nullptr);
    if (descriptor >= 1) {
        int foundSym = 0;
        int foundDwarf = 0;
        auto ret = elf_add(state, mFileName.c_str(), descriptor, NULL, 0, addr, btErrorHandler, this,
                           &state->fileline_fn, &foundSym, &foundDwarf, nullptr, false, false, nullptr, 0);
        if (ret && foundSym) {
            state->syminfo_fn = &elf_syminfo;
        } else {
            state->syminfo_fn = &elf_nosyms;
        }
    }

    mState = state;
}

void Module::btErrorHandler(void* data, const char* msg, int errnum)
{
    auto module = reinterpret_cast<const Module*>(data);
    fprintf(stderr, "libbacktrace error %s: %s - %s(%d)\n", module->mFileName.c_str(), msg, strerror(errnum), errnum);
};

std::shared_ptr<Module> Module::create(const char* filename, uint64_t addr)
{
    // assume we'll never have a hash collision?
    const uint64_t hash = sdbm(reinterpret_cast<const uint8_t*>(filename));
    auto it = sModuleByName.find(hash);
    if (it != sModuleByName.end())
        return it->second.lock();
    auto mod = Creatable<Module>::create(filename, addr);
    sModuleByName[hash] = mod;
    return mod;
}

void Module::addHeader(uint64_t addr, uint64_t len)
{
    mRanges.push_back(std::make_pair(mAddr + addr, mAddr + addr + len));
}

void Module::resolveAddress(uint64_t addr)
{
    backtrace_pcinfo(mState, addr,
                     [](void* data, uintptr_t /*addr*/, const char* file, int line, const char* function) -> int {
                         printf("pc frame %s %s %d\n", demangle(function).c_str(), file ? file : "(no file)", line);
                         // Frame frame(demangle(function), file ? file : "", line);
                         // auto info = reinterpret_cast<AddressInformation*>(data);
                         // if (!info->frame.isValid()) {
                         //     info->frame = frame;
                         // } else {
                         //     info->inlined.push_back(frame);
                         // }
                         return 0;
                     },
                     [](void* /*data*/, const char* msg, int errnum) {
                         printf("pc frame bad %s %d\n", msg, errnum);
                     },
                     nullptr);
    backtrace_syminfo(
        mState, addr,
        [](void* data, uintptr_t /*pc*/, const char* symname, uintptr_t /*symval*/, uintptr_t /*symsize*/) {
            printf("syminfo instead %s\n", demangle(symname).c_str());
        },
        [](void* /*data*/, const char* msg, int errnum) {
            printf("syminfo frame bad %s %d\n", msg, errnum);
        },
        nullptr);
}
