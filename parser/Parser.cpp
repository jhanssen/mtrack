#include "Parser.h"
#include "StringIndexer.h"
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
        case Event::Type::PageFault:
        case Event::Type::Mmap:
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

    mEvents.push_back(std::make_shared<PageFaultEvent>(addr, 4096, StringIndexer::instance()->index(tname)));
}

void Parser::handleMmap(bool tracked)
{
    const auto addr = readData<uint64_t>();
    const auto size = readData<uint64_t>();
    const auto prot = readData<int32_t>();
    const auto flags = readData<int32_t>();
    const auto fd = readData<int32_t>();
    const auto off = readData<uint64_t>();
    const auto tid = readData<uint32_t>();

    std::string tname;
    auto tn = mThreadNames.find(tid);
    if (tn != mThreadNames.end()) {
        tname = tn->second;
    }

    mEvents.push_back(std::make_shared<MmapEvent>(tracked, addr, size, prot, flags, fd, off, StringIndexer::instance()->index(tname)));
}

void Parser::handleMunmap(bool tracked)
{
    const auto addr = readData<uint64_t>();
    const auto size = readData<uint64_t>();

    mEvents.push_back(std::make_shared<MunmapEvent>(tracked, addr, size));
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
        // printf("hello %u\n", static_cast<std::underlying_type_t<RecordType>>(type));
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
        case RecordType::MmapTracked:
            handleMmap(true);
            break;
        case RecordType::MmapUntracked:
            handleMmap(false);
            break;
        case RecordType::MunmapTracked:
            handleMunmap(true);
            break;
        case RecordType::MunmapUntracked:
            handleMunmap(true);
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
    json root;

    root["strings"] = json(StringIndexer::instance()->strs());

    json events;
    for (const auto& event : mEvents) {
        events.push_back(event->to_json());
    }
    root["events"] = std::move(events);

    return root.dump();
}

json StackEvent::stack_json() const
{
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

json PageFaultEvent::to_json() const
{
    json jpf;
    jpf.push_back(Type::PageFault);
    jpf.push_back(addr);
    jpf.push_back(size);
    jpf.push_back(thread);
    jpf.push_back(stack_json());

    return jpf;
}

json MmapEvent::to_json() const
{
    json jmmap;
    jmmap.push_back(Type::Mmap);
    jmmap.push_back(tracked ? 1 : 0);
    jmmap.push_back(addr);
    jmmap.push_back(size);
    jmmap.push_back(prot);
    jmmap.push_back(flags);
    jmmap.push_back(fd);
    jmmap.push_back(offset);
    jmmap.push_back(thread);
    jmmap.push_back(stack_json());

    return jmmap;
}

json MunmapEvent::to_json() const
{
    json jmunmap;
    jmunmap.push_back(Type::Mmap);
    jmunmap.push_back(tracked ? 1 : 0);
    jmunmap.push_back(addr);
    jmunmap.push_back(size);

    return jmunmap;
}
