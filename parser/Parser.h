#pragma once

#include "FileEmitter.h"
#include "Module.h"
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

} // namespace std

class Parser
{
public:
    Parser(const std::string& file);
    ~Parser();

    void feed(const uint8_t* data, uint32_t size);
    void shutdown();

    void setFileSize(size_t size, size_t maxEventCount);

    // size_t eventCount() const;
    // size_t recordCount() const;
    // size_t stringCount() const;
    // size_t stringHits() const;
    // size_t stringMisses() const;
    // size_t stackCount() const;
    // size_t stackHits() const;
    // size_t stackMisses() const;

private:
    void parsePacket(const uint8_t* data, uint32_t size);
    void parseThread();

private:
    size_t mPacketNo {};
    size_t mHashOffset {};
    std::vector<uint8_t> mHashData;
    Indexer<Hashable> mHashIndexer;
    Indexer<std::string> mStackAddrIndexer;
    std::unordered_map<uint64_t, Address> mAddressCache;

    std::vector<Library> mLibraries;
    std::vector<PageFault> mPageFaults;
    std::vector<Malloc> mMallocs;

    std::map<uint64_t, ModuleEntry> mModuleCache;
    std::vector<std::shared_ptr<Module>> mModules;

    uint64_t mPageFaultSize {}, mMallocSize {};
    size_t mFileSize {};
    size_t mMaxEvents {};
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

// inline size_t Parser::stringCount() const
// {
//     return mStringIndexer.size();
// }

// inline size_t Parser::stringHits() const
// {
//     return mStringIndexer.hits();
// }

// inline size_t Parser::stringMisses() const
// {
//     return mStringIndexer.misses();
// }

// inline size_t Parser::stackCount() const
// {
//     return mStackIndexer.size();
// }

// inline size_t Parser::stackHits() const
// {
//     return mStackIndexer.hits();
// }

// inline size_t Parser::stackMisses() const
// {
//     return mStackIndexer.misses();
// }
