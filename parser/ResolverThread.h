#pragma once

#include "Address.h"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <utility>

class Parser;
class ResolverThread
{
public:
    class AddressScope
    {
    public:
        ~AddressScope();

        std::vector<UnresolvedAddress> *operator->();
    private:
        AddressScope(ResolverThread* thread);
        AddressScope(const AddressScope&) = delete;
        AddressScope &operator=(AddressScope&) = delete;

        ResolverThread* mThread;
        friend class ResolverThread;
    };
    ResolverThread(Parser* parser);
    void stop();
    AddressScope lock();
    void run();
private:
    Parser *const mParser;
    bool mStop {};
    std::thread mThread;
    std::mutex mMutex;
    std::condition_variable mCond;
    std::vector<UnresolvedAddress> mPending;
};

inline ResolverThread::AddressScope::AddressScope(ResolverThread* thread)
    : mThread(thread)
{
    mThread->mMutex.lock();
}

inline ResolverThread::AddressScope::~AddressScope()
{
    mThread->mCond.notify_one();
    mThread->mMutex.unlock();
}

inline std::vector<UnresolvedAddress> *ResolverThread::AddressScope::operator->()
{
    return &mThread->mPending;
}


inline ResolverThread::AddressScope ResolverThread::lock()
{
    return AddressScope(this);
}
