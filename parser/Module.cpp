#include "Module.h"
#include "Parser.h"
#include "Creatable.h"
#include <cassert>
#include <cstring>
#include <backtrace.h>
#include <functional>
extern "C" {
#include <internal.h>
}

std::vector<Module*> Module::sModules;
namespace {
void backtrace_moduleErrorCallback(void* /*data*/, const char* msg, int errnum)
{
    printf("pc frame bad %s %d\n", msg, errnum);
}
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
        fileline_initialize(state, backtrace_moduleErrorCallback, this);
    }

    mState = state;
}

void Module::btErrorHandler(void* data, const char* msg, int errnum)
{
    auto module = reinterpret_cast<const Module*>(data);
    fprintf(stderr, "libbacktrace error %s: %s - %s(%d)\n", module->mFileName.c_str(), msg, strerror(errnum), errnum);
};

std::shared_ptr<Module> Module::create(Indexer<std::string>& indexer, std::string&& filename, uint64_t addr)
{
    // assume we'll never have a hash collision?
    const auto [ idx, inserted ] = indexer.index(std::move(filename));
    if (static_cast<size_t>(idx) < sModules.size() && sModules[idx] != nullptr)
        return sModules[idx]->shared_from_this();
    auto mod = Creatable<Module>::create(indexer, filename, addr);
    if (static_cast<size_t>(idx) >= sModules.size()) {
        const auto num = idx - sModules.size() + 1;
        sModules.reserve(sModules.size() + num);
        for (size_t i = 0; i < num; ++i) {
            sModules.push_back(nullptr);
        }
    }
    assert(static_cast<size_t>(idx) < sModules.size() && sModules[idx] == nullptr);
    sModules[idx] = mod.get();
    return mod;
}

void Module::addHeader(uint64_t addr, uint64_t len)
{
    mRanges.push_back(std::make_pair(mAddr + addr, mAddr + addr + len));
}
