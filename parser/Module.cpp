#include "Module.h"
#include "Creatable.h"
#include <cassert>
#include <cstring>
#include <backtrace.h>
#include <cxxabi.h>

extern "C" {
#include <internal.h>
}

std::vector<Module*> Module::sModules;

static inline std::string demangle(const char* function)
{
    if (!function) {
        return {};
    } else if (function[0] != '_' || function[1] != 'Z') {
        return function;
    }

    int status = 0;
    char* demangled = abi::__cxa_demangle(function, 0, 0, &status);
    if (demangled && status == 0) {
        std::string ret = demangled;
        free(demangled);
        return ret;
    }
    return {};
}


Module::Module(Indexer<std::string>& indexer, const std::string& filename, uint64_t addr)
    : mIndexer(indexer), mFileName(filename), mAddr(addr)
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

std::shared_ptr<Module> Module::create(Indexer<std::string>& indexer, const std::string& filename, uint64_t addr)
{
    // assume we'll never have a hash collision?
    const uint32_t idx = indexer.index(filename);
    if (idx < sModules.size() && sModules[idx] != nullptr)
        return sModules[idx]->shared_from_this();
    auto mod = Creatable<Module>::create(indexer, filename, addr);
    if (idx >= sModules.size()) {
        const auto num = idx - sModules.size() + 1;
        sModules.reserve(sModules.size() + num);
        for (size_t i = 0; i < num; ++i) {
            sModules.push_back(nullptr);
        }
    }
    assert(idx < sModules.size() && sModules[idx] == nullptr);
    sModules[idx] = mod.get();
    return mod;
}

void Module::addHeader(uint64_t addr, uint64_t len)
{
    mRanges.push_back(std::make_pair(mAddr + addr, mAddr + addr + len));
}

Address Module::resolveAddress(uint64_t addr)
{
    struct ModuleCallback
    {
        Module* module;
        Address resolvedAddr;
    } moduleCallback = {
        this,
        Address {}
    };

    backtrace_pcinfo(
        mState, addr,
        [](void* data, uintptr_t /*addr*/, const char* file, int line, const char* function) -> int {
            // printf("pc frame %s %s %d\n", demangle(function).c_str(), file ? file : "(no file)", line);
            auto resolved = reinterpret_cast<ModuleCallback*>(data);
            Indexer<std::string>* indexer = &resolved->module->mIndexer;
            Frame frame { indexer->index(demangle(function)), indexer->index(file ? std::string(file) : std::string {}), line };
            if (!resolved->resolvedAddr.valid()) {
                resolved->resolvedAddr.frame = std::move(frame);
            } else {
                resolved->resolvedAddr.inlined.push_back(std::move(frame));
            }
            return 0;
        },
        [](void* /*data*/, const char* msg, int errnum) {
            printf("pc frame bad %s %d\n", msg, errnum);
        },
        &moduleCallback);

    if (!moduleCallback.resolvedAddr.valid()) {
        backtrace_syminfo(
            mState, addr,
            [](void* data, uintptr_t /*pc*/, const char* symname, uintptr_t /*symval*/, uintptr_t /*symsize*/) {
                // printf("syminfo instead %s\n", demangle(symname).c_str());
                auto resolved = reinterpret_cast<ModuleCallback*>(data);
                Indexer<std::string>* indexer = &resolved->module->mIndexer;
                if (!resolved->resolvedAddr.valid()) {
                    resolved->resolvedAddr.frame.function = indexer->index(demangle(symname));
                }
            },
            [](void* /*data*/, const char* msg, int errnum) {
                printf("syminfo frame bad %s %d\n", msg, errnum);
            },
            &moduleCallback);
    }

    return moduleCallback.resolvedAddr;
}
