#pragma once

#include "FileEmitter.h"
#include "Module.h"
#include "Address.h"
#include <common/Indexer.h>
#include <common/MmapTracker.h>
#include <common/RecordType.h>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

struct Library
{
    std::string name;
    uint64_t addr;

    struct Header {
        uint64_t addr;
        uint64_t len;
    };

    std::vector<Header> headers;
};

struct PageFault
{
    uint64_t place;
    uint32_t ptid;
    int32_t stack;
};

struct Malloc
{
    uint64_t addr;
    uint64_t size;
    uint32_t ptid;
    int32_t stack;
};

struct ModuleEntry
{
    uint64_t end;
    Module* module;
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
        size_t hash = stack.type();

        const auto data = stack.data<uint8_t>();
        if (data == nullptr)
            return hash;

        const auto size = stack.size();
        for (size_t i = 0; i < size; ++i) {
            hash = *(data + i) + (hash << 6) + (hash << 16) - hash;
        }

        return hash;
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

} // namespace std

class ResolverThread;
class Parser
{
public:
    struct Options {
        std::string output;
        size_t fileSize { std::numeric_limits<size_t>::max() };
        size_t maxEventCount { std::numeric_limits<size_t>::max() };
        size_t resolverThreads { 2 };
   };
    Parser(const Options& options);
    ~Parser();

    void feed(const uint8_t* data, uint32_t size);
    void shutdown();

    void onResolvedAddresses(std::vector<Address<std::string>>&& addresses);

private:
    void parsePacket(const uint8_t* data, uint32_t size);
    void parseThread();
    void resolveStack(int32_t idx);
    inline Frame<int32_t> convertFrame(Frame<std::string> &&frame);
    inline void emitAddress(Address<std::string> &&addr);

private:
    const Options mOptions;
    size_t mPacketNo {};
    size_t mHashOffset {};
    std::vector<uint8_t> mHashData;
    Indexer<Hashable> mHashIndexer;
    Indexer<std::string> mStringIndexer;
    std::unordered_map<uint64_t, std::optional<Address<int32_t>>> mAddressCache;
    std::mutex mResolvedAddressesMutex;
    std::vector<Address<std::string>> mResolvedAddresses;

    std::vector<Library> mLibraries;
    std::vector<PageFault> mPageFaults;
    std::unordered_set<Malloc> mMallocs;

    std::map<uint64_t, ModuleEntry> mModuleCache;
    std::vector<std::shared_ptr<Module>> mModules;

    uint64_t mPageFaultSize {}, mMallocSize {};
    MmapTracker mMmaps;

    FileEmitter mFileEmitter;

    std::string mExe, mCwd;

    std::thread mThread;
    std::mutex mMutex;
    std::condition_variable mCond;
    std::vector<uint8_t> mData;
    std::vector<uint32_t> mPacketSizes;
    size_t mDataOffset {};
    size_t mPacketSizeCount {};
    bool mShutdown {};

    std::unique_ptr<ResolverThread> mResolverThread;
};

inline void Parser::feed(const uint8_t* data, uint32_t size)
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
}

inline bool operator==(const Malloc& m1, const Malloc& m2)
{
    return m1.addr == m2.addr;
}
