#pragma once

#include <cstring>
#include <cstdint>
#include "FileEmitter.h"
#include "Module.h"
#include <common/Indexer.h>
#include <common/MmapTracker.h>
#include <common/RecordType.h>
#include <map>
#include <string>
#include <unordered_map>
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

    bool operator==(const Hashable&) const = default;

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

    void parsePacket(const uint8_t* data, uint32_t size);

    // size_t eventCount() const;
    // size_t recordCount() const;
    // size_t stringCount() const;
    // size_t stringHits() const;
    // size_t stringMisses() const;
    // size_t stackCount() const;
    // size_t stackHits() const;
    // size_t stackMisses() const;

private:
    inline void handleExe();
    inline void handleFree();
    inline void handleLibrary();
    inline void handleLibraryHeader();
    inline void handleMadvise(RecordType type);
    inline void handleMalloc();
    inline void handleMmap(RecordType type);
    inline void handleMunmap(RecordType type);
    inline void handlePageFault();
    inline void handleThreadName();
    inline void handleTime();
    inline void handleWorkingDirectory();

    inline int32_t readStack();

    inline void updateModuleCache();

    template<typename T>
    T readData();

    bool finalize() const;
    bool writeEvents();
    bool writeStacks() const;
    bool writeStrings() const;

private:
    size_t mHashOffset = 0;
    std::vector<uint8_t> mHashData;
    Indexer<Hashable> mHashIndexer;
    Indexer<std::string> mStackAddrIndexer;

    std::vector<Library> mLibraries;
    std::vector<PageFault> mPageFaults;
    std::vector<Malloc> mMallocs;

    std::map<uint64_t, ModuleEntry> mModuleCache;
    std::vector<std::shared_ptr<Module>> mModules;

    uint64_t mPageFaultSize { 0 }, mMallocSize { 0 };
    MmapTracker mMmaps;

    FileEmitter mFileEmitter;

    std::string mExe, mCwd;
};

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
