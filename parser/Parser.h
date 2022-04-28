#pragma once

#include <cstring>
#include "Indexer.h"
#include "Module.h"
#include <common/RecordType.h>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

class Parser
{
public:
    Parser() = default;

    bool parse(const uint8_t* data, size_t size, FILE* f);
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
    std::string mExe;
    std::string mCwd;
    std::shared_ptr<Module> mCurrentModule;
    std::unordered_set<std::shared_ptr<Module>> mModules;
    bool mModulesDirty { false };

    const uint8_t* mData { nullptr };
    const uint8_t* mEnd { nullptr };
    bool mError { false };

    FILE *mOutFile { nullptr };

    Indexer<std::string> mStringIndexer;
    Indexer<std::vector<uint64_t>> mStackIndexer;

    struct ModuleEntry
    {
        uint64_t end;
        Module* module;
    };
    std::map<uint64_t, ModuleEntry> mModuleCache;
    std::unordered_map<uint64_t, std::string> mThreadNames;
    size_t mRecordings {};
};

template<typename T>
inline T Parser::readData()
{
    if constexpr (std::is_same_v<T, std::string>) {
        if (mEnd - mData < sizeof(uint32_t)) {
            fprintf(stderr, "short read of string size\n");
            mError = true;
            return {};
        }

        uint32_t size;
        memcpy(&size, mData, sizeof(uint32_t));
        mData += sizeof(uint32_t);

        if (size > 1024 * 1024) {
            fprintf(stderr, "string too large (%u)\n", size);
            mError = true;
            return {};
        }

        if (mEnd - mData < size) {
            fprintf(stderr, "short read of string data (%u)\n", size);
            mError = true;
            return {};
        }

        std::string str;
        str.resize(size);
        memcpy(str.data(), mData, size);
        mData += size;
        return str;
    } else {
        if (mEnd - mData < sizeof(T)) {
            fprintf(stderr, "short read of int (%zu)\n", sizeof(T));
            mError = true;
            return {};
        }

        T t;
        memcpy(&t, mData, sizeof(T));
        mData += sizeof(T);
        return t;
    }
}
