#include "ResolverThread.h"
#include "Parser.h"
#include <cxxabi.h>
#include <backtrace.h>
#include <cassert>
#include <functional>
extern "C" {
#include <internal.h>
}

namespace {
inline std::string demangle(const char* function)
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

int backtrace_callback(void* data, uintptr_t addr, const char* file, int line, const char* function)
{
    //printf("pc frame 0x%lx %s %s %d\n", addr, demangle(function).c_str(), file ? file : "(no file)", line);
    Address<std::string> &address = *static_cast<Address<std::string>*>(data);
    Frame<std::string> *frame;
    if (address.frame.line == -1) {
        frame = &address.frame;
    } else {
        address.inlined.push_back({});
        frame = &address.inlined.back();
    }
    if (function) {
        frame->function = demangle(function);
    }
    if (file) {
        frame->file = file;
    }
    frame->line = line;
    return 0;
}

void backtrace_symInfoCallback(void* data, uintptr_t /*pc*/, const char* symname, uintptr_t /*symval*/, uintptr_t /*symsize*/)
{
    // printf("syminfo instead %s\n", demangle(symname).c_str());
    Address<std::string> *address = static_cast<Address<std::string>*>(data);
    assert(address->frame.function.empty());
    address->frame.function = demangle(symname);
}

void backtrace_errorCallback(void* /*data*/, const char* msg, int errnum)
{
    printf("pc frame bad %s %d\n", msg, errnum);
}
} // anonymous namespace

ResolverThread::ResolverThread(Parser* parser)
    : mParser(parser), mThread(std::bind(&ResolverThread::run, this))
{
}

void ResolverThread::stop()
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStop = true;
        mCond.notify_one();
    }
    mThread.join();
}

void ResolverThread::run()
{
    while (true) {
        std::vector<UnresolvedAddress> pending;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            while (!mStop && mPending.empty()) {
                if (mPending.empty()) {
                    mCond.wait(lock);
                }
            }
            if (!mPending.empty()) {
                pending = std::move(mPending);
            } else if (mStop) {
                break;
            }
        }

        std::vector<Address<std::string>> resolved;
        resolved.resize(pending.size());
        for (size_t idx=0; idx<pending.size(); ++idx) {
            const UnresolvedAddress &unresolved = pending[idx];
            Address<std::string> &dest = resolved[idx];
            dest.aid = unresolved.aid;
            dest.ip = unresolved.ip;
            if(unresolved.state->fileline_fn)
                unresolved.state->fileline_fn(unresolved.state, unresolved.ip, backtrace_callback, backtrace_errorCallback, &dest);
            if (unresolved.state->syminfo_fn && dest.frame.function.empty())
                unresolved.state->syminfo_fn(unresolved.state, unresolved.ip, backtrace_symInfoCallback, backtrace_errorCallback, &dest);
        }
        mParser->onResolvedAddresses(std::move(resolved));
    }
}
