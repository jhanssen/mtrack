#pragma once

#include "FileEmitter.h"
#include "Module.h"
#include "Address.h"
#include <common/Limits.h>
#include <common/Indexer.h>
#include <common/MmapTracker.h>
#include <common/RecordType.h>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <signal.h>
#include <string>
#include <thread>
#include <unordered_set>

struct Library
{
    std::string name;
    uint64_t addr {};

    struct Header {
        uint64_t addr {};
        uint64_t len {};
    };

    std::vector<Header> headers;
};

struct PageFault
{
    uint64_t place {};
    uint32_t ptid {};
    int32_t stack {};
    uint32_t time {};
};

struct Malloc
{
    uint64_t addr {};
    uint64_t size {};
    uint32_t ptid {};
    int32_t stack {};
    uint32_t time {};
};

struct ModuleEntry
{
    uint64_t end {};
    Module* module {};
};

struct Hashable
{
    enum Type { String, Stack };

    Hashable() = default;
    Hashable(Type type, std::vector<uint8_t>& data, size_t offset, size_t size)
        : mType(type), mData(&data), mOffset(offset), mSize(size)
    {
    }

    Type type() const { return mType; }
    uint32_t size() const { return mSize; }
    bool empty() const { return mSize == 0; }

    template<typename T = void>
    const T* data() const { return reinterpret_cast<const T*>(mData ? mData->data() + mOffset : nullptr); }

    bool operator==(const Hashable& other) const
    {
        return mType == other.mType && mSize == other.mSize && (mData == other.mData || !memcmp(mData + mOffset, other.mData + other.mOffset, mSize));
    }

private:
    Type mType {};
    std::vector<uint8_t>* mData {};
    size_t mOffset {}, mSize {};
};

namespace std {

template<>
struct hash<Hashable>
{
public:
    size_t operator()(const Hashable& stack) const
    {
        size_t h = stack.type();

        const auto data = stack.data<uint8_t>();
        if (data == nullptr)
            return h;

        const auto size = stack.size();
        for (size_t i = 0; i < size; ++i) {
            h = *(data + i) + (h << 6) + (h << 16) - h;
        }

        return h;
    }
};

template<>
struct hash<Malloc>
{
public:
    size_t operator()(const Malloc& malloc) const
    {
        return static_cast<size_t>(malloc.addr);
    }
};

template<>
struct hash<InstructionPointer>
{
public:
    size_t operator()(const InstructionPointer& ip) const
    {
        return static_cast<size_t>(ip.aid) ^ static_cast<size_t>(ip.ip);
    }
};


} // namespace std

struct Application
{
    uint8_t id {};
    std::string exe;
    std::string cwd;
    std::vector<Library> libraries;
    uint32_t startTimestamp {};
    uint32_t lastTimestamp {};
    uint64_t mallocSize {};
    MmapTracker mmaps;
    std::vector<PageFault> pageFaults;
    std::unordered_set<Malloc> mallocs;
    std::unordered_set<int32_t> pendingStacks;
    std::map<uint64_t, ModuleEntry> moduleCache;
    std::vector<std::shared_ptr<Module>> modules;
};

class ResolverThread;
class Parser
{
public:
    struct Options {
        std::string output;
        uint8_t appId { 0xFF }; //all
        size_t fileSize { std::numeric_limits<size_t>::max() };
        size_t maxEventCount { std::numeric_limits<size_t>::max() };
        size_t resolverThreads { 2 };
        uint32_t timeSkipPerTimeStamp { 0 };
        uint64_t threshold { 0 };
        bool gzip { true };
        bool html { true };
   };
    Parser(const Options& options);
    ~Parser();

    bool feed(const uint8_t* data, uint32_t size);
    void cleanup();

    void onResolvedAddresses(std::vector<Address<std::string>>&& addresses);

    uint64_t currentMallocBytes() const {
        uint64_t result = 0;
        for(auto app = mApplications.begin(); app != mApplications.end(); ++app) {
            if(mOptions.appId & app->first)
                result += app->second.mallocSize;
        }
        return result;
    }
    uint64_t currentPageFaultBytes() const {
        uint64_t result = 0;
        for(auto app = mApplications.begin(); app != mApplications.end(); ++app) {
            if(mOptions.appId & app->first)
                result += app->second.pageFaults.size() * Limits::PageSize;
        }
        return result;
    }

private:
    void parsePacket(const uint8_t* data, uint32_t size);
    void parseThread();
    Frame<int32_t> convertFrame(Frame<std::string> &&frame);
    void emitStack(Application &app, int32_t idx);
    void emitAddress(Address<std::string> &&addr);
    void emitSnapshot(uint32_t now);

    static std::string visualizerDirectory();
    static std::string readFile(const std::string& fn);
    void writeHtmlHeader();
    void writeHtmlTrailer();

private:
    const Options mOptions;
    size_t mPacketNo {};
    size_t mHashOffset {};
    std::vector<uint8_t> mHashData;
    Indexer<Hashable> mHashIndexer;
    Indexer<std::string> mStringIndexer;
    std::unordered_map<InstructionPointer, std::optional<Address<int32_t>>> mAddressCache;
    std::mutex mResolvedAddressesMutex;
    std::vector<Address<std::string>> mResolvedAddresses;
    uint32_t mLastTimestamp {};
    size_t mStacksResolved {};

    struct {
        bool enabled { true };
        uint64_t pageFaultBytes {};
        uint64_t mallocBytes {};
        uint32_t timeThreshold { 10000 };
        uint32_t peakTimeThreshold { 1000 };
        uint32_t downTimeThreshold { 5000 };
        uint32_t maxTimeThreshold { 900000 };
        uint32_t peakThreshold { 2500 };
        double upThreshold { 0.2 };
        double downThreshold { 0.05 };
        uint32_t time {};
        uint32_t downTime {};
        uint32_t peakTime {};
        uint64_t peakBytes {};
        int64_t combinedBytes() const { return mallocBytes + pageFaultBytes; }
        bool shouldSend(uint32_t now, uint64_t mallocSize, uint64_t pageFaultSize)
        {
            if (!enabled) {
                return false;
            }
            if (mallocSize == mallocBytes && pageFaultSize == pageFaultBytes) {
                return false;
            }
            if (now - time >= timeThreshold) {
                time = now;
                pageFaultBytes = pageFaultSize;
                mallocBytes = mallocSize;
                timeThreshold = std::min(timeThreshold * 2, maxTimeThreshold);
                peakTime = 0;
                return true;
            }
            const auto combined = mallocSize + pageFaultSize;
            if (peakTime == 0) {
                const auto delta = std::fabs((combined - combinedBytes()) / static_cast<double>(combined));
                if (delta >= upThreshold) {
                    peakTime = now;
                    peakBytes = combined;
                }
            } else if (peakTime > 0 && now - time >= peakTimeThreshold) {
                time = now;
                pageFaultBytes = pageFaultSize;
                mallocBytes = mallocSize;
                peakTime = 0;
                peakTimeThreshold = std::min(peakTimeThreshold * 2, maxTimeThreshold);
                return true;
            } else if (peakTime > 0 && now - downTime >= downTimeThreshold && combined < peakBytes) {
                const auto delta = std::fabs((peakBytes - combined) / static_cast<double>(peakBytes));
                if (delta >= downThreshold) {
                    time = downTime = now;
                    pageFaultBytes = pageFaultSize;
                    mallocBytes = mallocSize;
                    peakTime = 0;
                    peakBytes = combined;
                    return true;
                }
            }
            return false;
        }

    } mLastMemory, mLastSnapshot;

    FILE* mFile {};
    FileEmitter mFileEmitter;

    std::thread mThread;
    std::mutex mMutex;
    std::condition_variable mCond;
    std::vector<uint8_t> mData;
    std::vector<uint32_t> mPacketSizes;
    size_t mDataOffset {};
    size_t mPacketSizeCount {};
    std::unique_ptr<ResolverThread> mResolverThread;
    std::map<uint8_t, Application> mApplications;

    bool mShutdown {};
    bool mThreshold {};
};

inline bool Parser::feed(const uint8_t* data, uint32_t size)
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mData.size() < mDataOffset + size) {
        mData.resize(std::max(mDataOffset + size, std::min<size_t>(mData.size() * 2, 1024 * 1024)));
    }
    memcpy(mData.data() + mDataOffset, data, size);
    mDataOffset += size;
    if (mPacketSizeCount == mPacketSizes.size()) {
        mPacketSizes.resize(mPacketSizes.size() + 100);
    }
    mPacketSizes[mPacketSizeCount++] = size;
    mCond.notify_one();
    return !mThreshold;
}

inline bool operator==(const Malloc& m1, const Malloc& m2)
{
    return m1.addr == m2.addr;
}
