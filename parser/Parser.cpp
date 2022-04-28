#include "Parser.h"
#include <common/Version.h>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <limits>

inline void Parser::updateModuleCache()
{
    mModuleCache.clear();

    for (const auto& m : mModules) {
        const auto& rs = m->ranges();
        for (const auto& r : rs) {
            mModuleCache.insert(std::make_pair(r.first, ModuleEntry { r.second, m.get() }));
        }
    }
}

inline int32_t Parser::readStack()
{
    if (mModulesDirty) {
        updateModuleCache();
        mModulesDirty = false;
    }

    std::vector<uint64_t> stack;
    const uint32_t count = readData<uint32_t>() / sizeof(void*);
    for (uint32_t i=0; i<count; ++i) {
        stack.push_back(readData<uint64_t>() - 1);
    }

    return mStackIndexer.index(stack);
}

inline void Parser::handleExe()
{
    mExe = readData<std::string>();
}

inline void Parser::handleFree()
{
    const auto addr = readData<unsigned long long>();
    char buf[1024];
    const int w = snprintf(buf, sizeof(buf), "[%d,%llu],", static_cast<int>(RecordType::Free), addr);
    mError = !fwrite(buf, w, 1, mOutFile);
}

inline void Parser::handleLibrary()
{
    auto name = readData<std::string>();
    auto start = readData<unsigned long long>();
    if (name.substr(0, 13) == "linux-vdso.so" || name.substr(0, 13) == "linux-gate.so") {
        // skip this
        return;
    }
    if (name == "s") {
        name = mExe;
    }
    if (name.size() > 0 && name[0] != '/') {
        // relative path?
        char buf[4096];
        name = realpath((mCwd + name).c_str(), buf);
    }

    // printf("dlll '%s' %lx\n", name.c_str(), start);

    auto mod = Module::create(mStringIndexer, name, start);
    mCurrentModule = mod;
    mModules.insert(mod);
}

inline void Parser::handleLibraryHeader()
{
    // two hex numbers
    const auto addr = readData<unsigned long long>();
    const auto size = readData<unsigned long long>();

    assert(mCurrentModule);
    // printf("phhh %lx %lx (%lx %lx)\n", addr, size, mCurrentModule->address() + addr, mCurrentModule->address() + addr + size);
    mCurrentModule->addHeader(addr, size);
    mModulesDirty = true;
}

inline void Parser::handleMadvise(RecordType type)
{
    const auto addr = readData<unsigned long long>();
    const auto size = readData<unsigned long long>();
    const auto advice = readData<int32_t>();
    const auto deallocated = readData<unsigned long long>();
    char buf[1024];
    const int w = snprintf(buf, sizeof(buf), "[%d,%llu,%llu,%d,%llu],",
                           static_cast<int>(RecordType::Free), addr, size, advice, deallocated);
    mError = !fwrite(buf, w, 1, mOutFile);
}

inline void Parser::handleMalloc()
{
    const auto addr = readData<unsigned long long>();
    const auto size = readData<unsigned long long>();
    const auto thread = readData<uint32_t>();

    const auto stack = readStack();
    char buf[1024];
    const int w = snprintf(buf, sizeof(buf), "[%d,%llu,%llu,%u,%d],",
                           static_cast<int>(RecordType::Malloc), addr, size, thread, stack);
    mError = !fwrite(buf, w, 1, mOutFile);
}

inline void Parser::handleMmap(RecordType type)
{
    const auto addr = readData<unsigned long long>();
    const auto size = readData<unsigned long long>();
    const auto allocated = readData<unsigned long long>();
    const auto prot = readData<int32_t>();
    const auto flags = readData<int32_t>();
    const auto fd = readData<int32_t>();
    const auto off = readData<unsigned long long>();
    const auto tid = readData<uint32_t>();
    const auto stack = readStack();

    std::string tname;
    auto tn = mThreadNames.find(tid);
    if (tn != mThreadNames.end()) {
        tname = tn->second;
    }

    char buf[1024];
    const int w = snprintf(buf, sizeof(buf), "[%d,%llu,%llu,%llu,%d,%d,%d,%llu,%u,%d],",
                           static_cast<int>(type), addr, size, allocated, prot, flags, fd, off, tid, stack);
    mError = !fwrite(buf, w, 1, mOutFile);
}

inline void Parser::handleMunmap(RecordType type)
{
    const auto addr = readData<unsigned long long>();
    const auto size = readData<unsigned long long>();
    const auto deallocated = readData<unsigned long long>();

    char buf[1024];
    const int w = snprintf(buf, sizeof(buf), "[%d,%llu,%llu,%llu],",
                           static_cast<int>(type), addr, size, deallocated);
    mError = !fwrite(buf, w, 1, mOutFile);
}

inline void Parser::handlePageFault()
{
    const auto addr = readData<unsigned long long>();
    const auto tid = readData<uint32_t>();
    const auto stack = readStack();

    std::string tname;
    auto tn = mThreadNames.find(tid);
    if (tn != mThreadNames.end()) {
        tname = tn->second;
    }

    char buf[1024];
    const int w = snprintf(buf, sizeof(buf), "[%d,%llu,4096,%u,%d],",
                           static_cast<int>(RecordType::PageFault), addr, tid, stack);
    mError = !fwrite(buf, w, 1, mOutFile);
}

inline void Parser::handleThreadName()
{
    const auto tid = readData<uint32_t>();
    mThreadNames[tid] = readData<std::string>();
}

inline void Parser::handleTime()
{
    const auto time = readData<uint32_t>();
    char buf[1024];
    const int w = snprintf(buf, sizeof(buf), "[%d,%u],",
                           static_cast<int>(RecordType::Time), time);
    mError = !fwrite(buf, w, 1, mOutFile);
}

inline void Parser::handleWorkingDirectory()
{
    mCwd = readData<std::string>() + "/";
}

bool Parser::parse(const uint8_t* data, size_t size, FILE* f)
{
    if (size < sizeof(FileVersion)) {
        fprintf(stderr, "no version\n");
        return false;
    }

    assert(f);
    mOutFile = f;

    mData = data;
    mEnd = data + size;

    auto version = readData<FileVersion>();
    if (version != FileVersion::Current) {
        fprintf(stderr, "invalid file version (got %u vs %u)\n",
                static_cast<std::underlying_type_t<FileVersion>>(version),
                static_cast<std::underlying_type_t<FileVersion>>(FileVersion::Current));
        return false;
    }

    if (!fprintf(mOutFile, "{")) {
        return false;
    }

    if (!writeEvents()) {
        return false;
    }

    if (!writeStacks()) {
        return false;
    }

    if (!writeStrings()) {
        return false;
    }
    if (fprintf(mOutFile, "}\n") != 2) {
        return false;
    }
    return false;
}

bool Parser::writeEvents()
{
    if (fprintf(mOutFile, "\"events\":[") != 10) {
        return false;
    }

    const auto start = reinterpret_cast<unsigned long long>(mData);
    const size_t total = mEnd - mData;
    while (!mError && mData < mEnd) {
        ++mRecordings;
        if (mRecordings % 1000 == 0) {
            const auto cur = reinterpret_cast<unsigned long long>(mData) - start;
            printf("%zu %llu/%zu bytes left (%g%%)\n", mRecordings, cur, total,
                   (static_cast<double>(cur) / static_cast<double>(total)) * 100);
        }

        const auto type = readData<RecordType>();
        // printf("hello %u (%s)\n", static_cast<std::underlying_type_t<RecordType>>(type),
        //        recordTypeToString(type));
        switch (type) {
        case RecordType::Executable:
            handleExe();
            break;
        case RecordType::Free:
            handleFree();
            break;
        case RecordType::Library:
            handleLibrary();
            break;
        case RecordType::LibraryHeader:
            handleLibraryHeader();
            break;
        case RecordType::MadviseTracked:
            handleMadvise(type);
            break;
        case RecordType::MadviseUntracked:
            handleMadvise(type);
            break;
        case RecordType::Malloc:
            handleMalloc();
            break;
        case RecordType::MmapTracked:
            // printf("mmap2\n");
            handleMmap(type);
            break;
        case RecordType::MmapUntracked:
            // printf("mmap1\n");
            handleMmap(type);
            break;
        case RecordType::MunmapTracked:
            handleMunmap(type);
            break;
        case RecordType::MunmapUntracked:
            handleMunmap(type);
            break;
        case RecordType::PageFault:
            //printf("pf\n");
            handlePageFault();
            break;
        case RecordType::Time:
            handleTime();
            break;
        case RecordType::ThreadName:
            handleThreadName();
            break;
        case RecordType::WorkingDirectory:
            handleWorkingDirectory();
            break;
        default:
            fprintf(stderr, "unhandled type %u\n", static_cast<std::underlying_type_t<RecordType>>(type));
            mError = true;
            break;
        }
    }

    return !mError && fprintf(mOutFile, "null],\n") == 7;
}

bool Parser::writeStacks() const
{
    if (fprintf(mOutFile, "\"stacks\":[") != 10) {
        // printf("[Parser.cpp:%d]: return false;\n", __LINE__); fflush(stdout);
        return false;
    }

    auto resolveStack = [this](const std::vector<uint64_t>& stack) {
        std::vector<Address> astack;

        // printf("Trying to read stack at %zu\n", mReadOffset);
        for (const auto ip : stack) {
            auto it = mModuleCache.upper_bound(ip);
            if (it != mModuleCache.begin())
                --it;
            if (mModuleCache.size() == 1)
                it = mModuleCache.begin();
            Address address;
            if (it != mModuleCache.end() && ip >= it->first && ip <= it->second.end)
                address = it->second.module->resolveAddress(ip);

            astack.push_back(address);
        }

        return astack;
    };

    for (const auto& stack : mStackIndexer.values()) {
        if (!fprintf(mOutFile, "[")) {
            // printf("[Parser.cpp:%d]: return false;\n", __LINE__); fflush(stdout);
            return false;
        }
        for (const auto& saddr : resolveStack(stack)) {
            char buf[1024];
            int w = snprintf(buf, sizeof(buf), "[%d,%d,%d",
                             saddr.frame.function, saddr.frame.file, saddr.frame.line);

            if (!saddr.inlined.empty()) {
                w += snprintf(buf + w, sizeof(buf) - w, ",[");
                for (size_t i=0; i<saddr.inlined.size(); ++i) {
                    const auto &inl = saddr.inlined[i];
                    w += snprintf(buf + w, sizeof(buf) - w, "[%d,%d,%d]%c",
                                  inl.function, inl.file, inl.line,
                                  i + 1 == saddr.inlined.size() ? ']' : ',');
                }
            }
            buf[w++] = ']';
            buf[w++] = ',';
            if (!fwrite(buf, w, 1, mOutFile)) {
                // printf("[Parser.cpp:%d]: return false;\n", __LINE__); fflush(stdout);
                return false;
            }
        }
        if (fprintf(mOutFile, "null],") != 6) {
            // printf("[Parser.cpp:%d]: return false;\n", __LINE__); fflush(stdout);
            return false;
        }
    }

    if (fprintf(mOutFile, "null],\n") != 7) {
        // printf("[Parser.cpp:%d]: return false;\n", __LINE__); fflush(stdout);
        return false;
    }
    return true;
}

bool Parser::writeStrings() const
{
    if (fprintf(mOutFile, "\"strings\":[") != 11) {
        return false;
    }

    for (const auto& string : mStringIndexer.values()) {
        if (fprintf(mOutFile, "\"%s\",", string.c_str()) != string.size() + 3) {
            return false;
        }
    }
    if (fprintf(mOutFile, "null]\n") != 6) {
        return false;
    }
    return true;
}
