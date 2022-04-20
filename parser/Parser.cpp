#include "Parser.h"
#include "StringIndexer.h"
#include <common/RecordType.h>
#include <common/Version.h>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <limits>

void Parser::handleLibrary()
{
    auto name = readData<std::string>();
    auto start = readData<uint64_t>();
    if (name.substr(0, 13) == "linux-vdso.so") {
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

    auto mod = Module::create(name, start);
    mCurrentModule = mod;
    mModules.insert(mod);
}

void Parser::updateModuleCache()
{
    mModuleCache.clear();

    for (const auto& m : mModules) {
        const auto& rs = m->ranges();
        for (const auto& r : rs) {
            mModuleCache.insert(std::make_pair(r.first, ModuleEntry { r.second, m.get() }));
        }
    }
}

void Parser::handleStack()
{
    // two hex numbers
    const auto ip = readData<uint64_t>();
    // printf("sttt %lx %lx\n", ip, sp);

    if (mModulesDirty) {
        updateModuleCache();
        mModulesDirty = false;
    }

    // find ip in module cache
    auto it = mModuleCache.upper_bound(ip);
    if (it != mModuleCache.begin())
        --it;
    if (mModuleCache.size() == 1)
        it = mModuleCache.begin();
    if (it != mModuleCache.end() && ip >= it->first && ip <= it->second.end) {
        auto mod = it->second.module;
        // printf("found module %s\n", mod->fileName().c_str());
        assert(!mEvents.empty());
        switch (mEvents.back()->type()) {
        case Event::Type::Allocation:
            static_cast<StackEvent*>(mEvents.back().get())->stack.push_back(mod->resolveAddress(ip));
            break;
        default:
            fprintf(stderr, "invalid event for stack %u\n", static_cast<uint32_t>(mEvents.back()->type()));
            assert(false && "invalid event for stack");
        }
    }
}

void Parser::handleLibraryHeader()
{
    // two hex numbers
    const auto addr = readData<uint64_t>();
    const auto size = readData<uint64_t>();

    assert(mCurrentModule);
    // printf("phhh %lx %lx (%lx %lx)\n", addr, size, mCurrentModule->address() + addr, mCurrentModule->address() + addr + size);
    mCurrentModule->addHeader(addr, size);
    mModulesDirty = true;
}

void Parser::handleExe()
{
    mExe = readData<std::string>();
}

void Parser::handleWorkingDirectory()
{
    mCwd = readData<std::string>() + "/";
}

void Parser::handleThreadName()
{
    const auto tid = readData<uint32_t>();
    mThreadNames[tid] = readData<std::string>();
}

void Parser::handlePageFault()
{
    const auto addr = readData<uint64_t>();
    const auto tid = readData<uint32_t>();

    std::string tname;
    auto tn = mThreadNames.find(tid);
    if (tn != mThreadNames.end()) {
        tname = tn->second;
    }

    mEvents.push_back(std::make_shared<Allocation>(addr, 4096, StringIndexer::instance()->index(tname)));
}

bool Parser::parse(const uint8_t* data, size_t size)
{
    if (size < sizeof(FileVersion)) {
        fprintf(stderr, "no version\n");
        return false;
    }

    mData = data;
    mEnd = data + size;

    auto version = readData<FileVersion>();
    if (version != FileVersion::Current) {
        fprintf(stderr, "invalid file version (got %u vs %u)\n",
                static_cast<std::underlying_type_t<FileVersion>>(version),
                static_cast<std::underlying_type_t<FileVersion>>(FileVersion::Current));
        return false;
    }


    while (!mError && mData < mEnd) {
        const auto type = readData<RecordType>();
        //printf("hello %u\n", static_cast<std::underlying_type_t<RecordType>>(type));
        switch (type) {
        case RecordType::Executable:
            handleExe();
            break;
        case RecordType::Free:
            break;
        case RecordType::Library:
            handleLibrary();
            break;
        case RecordType::LibraryHeader:
            handleLibraryHeader();
            break;
        case RecordType::Madvise:
            break;
        case RecordType::Malloc:
            break;
        case RecordType::Mmap:
            break;
        case RecordType::Munmap:
            break;
        case RecordType::PageFault:
            handlePageFault();
            break;
        case RecordType::Stack:
            handleStack();
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

    return !mError;
}

std::string Parser::finalize() const
{
    using json = nlohmann::json;

    json root;

    root["strings"] = json(StringIndexer::instance()->strs());

    json events;
    for (const auto& event : mEvents) {
        events.push_back(event->to_json());
    }
    root["events"] = std::move(events);

    return root.dump();
}

nlohmann::json StackEvent::stack_json() const
{
    using json = nlohmann::json;

    auto makeFrame = [](const Frame& frame) {
        json jframe;
        jframe.push_back(frame.function);
        jframe.push_back(frame.file);
        jframe.push_back(frame.line);
        return jframe;
    };

    json jstack;
    for (const auto& saddr : stack) {
        auto frame = makeFrame(saddr.frame);
        if (!saddr.inlined.empty()) {
            json inlined;
            for (const auto& inl : saddr.inlined) {
                inlined.push_back(makeFrame(inl));
            }
            frame.push_back(std::move(inlined));
        }
        jstack.push_back(std::move(frame));
    }
    return jstack;
}

nlohmann::json Allocation::to_json() const
{
    using json = nlohmann::json;

    json jalloc;
    jalloc.push_back(Type::Allocation);
    jalloc.push_back(addr);
    jalloc.push_back(size);
    jalloc.push_back(thread);
    jalloc.push_back(stack_json());

    return jalloc;
}
