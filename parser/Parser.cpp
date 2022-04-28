#include "Parser.h"
#include <common/Version.h>
#include <fmt/core.h>
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
    ++mStats.eventCount;
    const auto addr = readData<uint64_t>();
    const auto str = fmt::format("[{},{}],", static_cast<int>(RecordType::Free), addr);
    mError = !fwrite(str.c_str(), str.size(), 1, mOutFile);
}

inline void Parser::handleLibrary()
{
    auto name = readData<std::string>();
    auto start = readData<uint64_t>();
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
    const auto addr = readData<uint64_t>();
    const auto size = readData<uint64_t>();

    assert(mCurrentModule);
    // printf("phhh %lx %lx (%lx %lx)\n", addr, size, mCurrentModule->address() + addr, mCurrentModule->address() + addr + size);
    mCurrentModule->addHeader(addr, size);
    mModulesDirty = true;
}

inline void Parser::handleMadvise(RecordType type)
{
    ++mStats.eventCount;
    const auto addr = readData<uint64_t>();
    const auto size = readData<uint64_t>();
    const auto advice = readData<int32_t>();
    const auto deallocated = readData<uint64_t>();
    const auto str = fmt::format("[{},{},{},{},{}],",
                                 static_cast<int>(RecordType::Free), addr, size, advice, deallocated);
    mError = !fwrite(str.c_str(), str.size(), 1, mOutFile);
}

inline void Parser::handleMalloc()
{
    ++mStats.eventCount;
    const auto addr = readData<uint64_t>();
    const auto size = readData<uint64_t>();
    const auto thread = readData<uint32_t>();

    const auto stack = readStack();
    const auto str = fmt::format("[{},{},{},{},{}],",
                                 static_cast<int>(RecordType::Malloc), addr, size, thread, stack);
    mError = !fwrite(str.c_str(), str.size(), 1, mOutFile);
}

inline void Parser::handleMmap(RecordType type)
{
    ++mStats.eventCount;
    const auto addr = readData<uint64_t>();
    const auto size = readData<uint64_t>();
    const auto allocated = readData<uint64_t>();
    const auto prot = readData<int32_t>();
    const auto flags = readData<int32_t>();
    const auto fd = readData<int32_t>();
    const auto off = readData<uint64_t>();
    const auto tid = readData<uint32_t>();
    const auto stack = readStack();

    std::string tname;
    auto tn = mThreadNames.find(tid);
    if (tn != mThreadNames.end()) {
        tname = tn->second;
    }

    const auto str = fmt::format("[{},{},{},{},{},{},{},{},{},{}],",
                                 static_cast<int>(type), addr, size, allocated, prot, flags, fd, off, tid, stack);
    mError = !fwrite(str.c_str(), str.size(), 1, mOutFile);
}

inline void Parser::handleMunmap(RecordType type)
{
    ++mStats.eventCount;
    const auto addr = readData<uint64_t>();
    const auto size = readData<uint64_t>();
    const auto deallocated = readData<uint64_t>();

    const auto str = fmt::format("[{},{},{},{}],",
                                 static_cast<int>(type), addr, size, deallocated);
    mError = !fwrite(str.c_str(), str.size(), 1, mOutFile);
}

inline void Parser::handlePageFault()
{
    ++mStats.eventCount;
    const auto addr = readData<uint64_t>();
    const auto tid = readData<uint32_t>();
    const auto stack = readStack();

    std::string tname;
    auto tn = mThreadNames.find(tid);
    if (tn != mThreadNames.end()) {
        tname = tn->second;
    }

    const auto str = fmt::format("[{},{},4096,{},{}],",
                                 static_cast<int>(RecordType::PageFault), addr, tid, stack);
    mError = !fwrite(str.c_str(), str.size(), 1, mOutFile);
}

inline void Parser::handleThreadName()
{
    const auto tid = readData<uint32_t>();
    mThreadNames[tid] = readData<std::string>();
}

inline void Parser::handleTime()
{
    ++mStats.eventCount;
    const auto time = readData<uint32_t>();
    const auto str = fmt::format("[{},{}],",
                                 static_cast<int>(RecordType::Time), time);
    mError = !fwrite(str.c_str(), str.size(), 1, mOutFile);
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
        ++mStats.recordCount;

        if (mStats.recordCount % 1000 == 0) {
            const auto cur = reinterpret_cast<unsigned long long>(mData) - start;
            printf("%zu %llu/%zu bytes left (%g%%)\n", mStats.recordCount, cur, total,
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
            auto str = fmt::format("[{},{},{}",
                                   saddr.frame.function, saddr.frame.file, saddr.frame.line);

            if (!saddr.inlined.empty()) {
                str += ",[";
                for (size_t i=0; i<saddr.inlined.size(); ++i) {
                    const auto &inl = saddr.inlined[i];
                    str += fmt::format("[{},{},{}]{}",
                                       inl.function, inl.file, inl.line,
                                       i + 1 == saddr.inlined.size() ? ']' : ',');
                }
            }
            str += "],";
            if (!fwrite(str.c_str(), str.size(), 1, mOutFile)) {
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
