#include "Parser.h"
#include "Logger.h"
#include "ResolverThread.h"
#include <common/Limits.h>
#include <common/MmapTracker.h>
#include <fmt/core.h>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <functional>
#include <unistd.h>

enum class EmitType : uint8_t {
    Start,
    Memory,
    Snapshot,
    SnapshotName,
    Stack,
    StackAddr,
    StackString,
    ThreadName
};

namespace {
inline bool intersects(uint64_t startA, uint64_t endA, uint64_t startB, uint64_t endB)
{
    return startA < endB && startB < endA;
}
} // anonymous namespace

// #define DEBUG_EMITS
#ifdef DEBUG_EMITS
static std::map<int, size_t> emitted;
#define EMIT(cmd) emitted[__LINE__] += cmd
#else
#define EMIT(cmd) cmd
#endif

Parser::Parser(const Options& options)
    : mOptions(options), mResolverThread(std::make_unique<ResolverThread>(this))
{
    mThread = std::thread(std::bind(&Parser::parseThread, this));
    if (options.maxEventCount != std::numeric_limits<size_t>::max() && options.maxEventCount > 0) {
        mData.resize(options.maxEventCount * 16); // ??? ### WTF BBQ?
    } else if (options.fileSize != std::numeric_limits<size_t>::max() && options.fileSize > 0) {
        mData.resize(options.fileSize);
    }

    mFile = fopen(options.output.c_str(), "w");

    uint8_t fileEmitterFlags = FileEmitter::WriteMode::None;
    if (mOptions.html) {
        fileEmitterFlags |= FileEmitter::WriteMode::GZip | FileEmitter::WriteMode::Base64;
        writeHtmlHeader();
    } else if (mOptions.gzip) {
        fileEmitterFlags |= FileEmitter::WriteMode::GZip;
    }

    mFileEmitter.setFile(mFile, fileEmitterFlags);

    mLastMemory.upThreshold = 0.01;
    mLastMemory.downThreshold = 0.01;
    mLastMemory.timeThreshold = 25;
    mLastMemory.peakThreshold = 50;
    mLastMemory.peakTimeThreshold = 250;
    mLastMemory.maxTimeThreshold = 1000;
}

Parser::~Parser()
{
    cleanup();
}

void Parser::cleanup()
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mShutdown)
            return;
        mShutdown = true;
        mCond.notify_one();
    }
    mThread.join();

#ifdef DEBUG_EMITS
    for (const auto &ref : emitted) {
        printf("%d %lu\n", ref.first, ref.second);
    }
#endif

    mFileEmitter.cleanup();
    if (mFile) {
        if (mOptions.html) {
            writeHtmlTrailer();
        }
        fclose(mFile);
    }
}

void Parser::onResolvedAddresses(std::vector<Address<std::string>>&& addresses)
{
    std::unique_lock<std::mutex> lock(mResolvedAddressesMutex);
    mResolvedAddresses.insert(mResolvedAddresses.end(), std::make_move_iterator(addresses.begin()), std::make_move_iterator(addresses.end()));
}

void Parser::emitStack(Application &app, int32_t idx)
{
    if(!(mOptions.appId & app.id))
        return;

    ++mStacksResolved;
    // auto prior = mStackAddrIndexer.size();
    if (app.libraries.size() > app.modules.size()) {
        // create new modules
        for (size_t libIdx = app.modules.size(); libIdx < app.libraries.size(); ++libIdx) {
            const auto& lib = app.libraries[libIdx];
            if (lib.name.substr(0, 13) == "linux-vdso.so" || lib.name.substr(0, 13) == "linux-gate.so") {
                // skip
                continue;
            }
            auto name = lib.name;
            if (name == "s") {
                name = app.exe;
            }
            if (name.size() > 0 && name[0] != '/') {
                // relative path?
                char buf[4096];
                name = realpath((app.cwd + name).c_str(), buf);
            }

            auto module = Module::create(mStringIndexer, std::move(name), lib.addr);
            for (const auto& hdr : lib.headers) {
                module->addHeader(hdr.addr, hdr.len);
            }
            app.modules.push_back(std::move(module));
        }
        app.moduleCache.clear();
    }
    if (app.moduleCache.empty()) {
        // update module cache
        for (const auto& m : app.modules) {
            const auto& rs = m->ranges();
            for (const auto& r : rs) {
                app.moduleCache.insert(std::make_pair(r.first, ModuleEntry { r.second, m.get() }));
            }
        }
    }

    const auto& hashable = mHashIndexer.value(idx);
    const uint32_t numFrames = hashable.size() / sizeof(void*);
    const uint8_t* data = hashable.data<uint8_t>();
    std::vector<UnresolvedAddress> stackFrames;
    stackFrames.resize(numFrames);
    std::vector<UnresolvedAddress> unresolved;
    EMIT(mFileEmitter.emit(EmitType::Stack, app.id, idx, numFrames));
    for (uint32_t i = 0; i < numFrames; ++i) {
        void* ipptr;
        memcpy(&ipptr, data + (i * sizeof(void*)), sizeof(void*));
        const InstructionPointer ip = { app.id, reinterpret_cast<uint64_t>(ipptr) };
        auto ait = mAddressCache.find(ip);
        if (ait == mAddressCache.end()) {
            auto it = app.moduleCache.upper_bound(ip.ip);
            if (it != app.moduleCache.begin())
                --it;
            if (app.moduleCache.size() == 1)
                it = app.moduleCache.begin();
            if (it != app.moduleCache.end() && ip.ip >= it->first && ip <= InstructionPointer{app.id, it->second.end }) {
                unresolved.push_back(UnresolvedAddress{ app.id, ip.ip, it->second.module->state() });
                mAddressCache[ip] = std::nullopt;
            } else {
                mAddressCache[ip] = Address<int32_t>();
            }
        }
        EMIT(mFileEmitter.emit(static_cast<double>(ip.ip)));
    }
    if (!unresolved.empty()) {
        auto lock = mResolverThread->lock();
        lock->insert(lock->end(), unresolved.begin(), unresolved.end());
    }
}

inline Frame<int32_t> Parser::convertFrame(Frame<std::string> &&frame)
{
    Frame<int32_t> ret;
    {
        const auto [ i, inserted ] = mStringIndexer.index(std::move(frame.function));
        if (inserted) {
            EMIT(mFileEmitter.emit(EmitType::StackString, i, Emitter::String(mStringIndexer.value(i))));
        }
        ret.function = i;
    }
    if (!frame.file.empty()) {
        const auto [ i, inserted ] = mStringIndexer.index(std::move(frame.file));
        if (inserted) {
            EMIT(mFileEmitter.emit(EmitType::StackString, i, Emitter::String(mStringIndexer.value(i))));
        }
        ret.file = i;
        ret.line = frame.line;
    }
    return ret;
}

inline void Parser::emitAddress(Address<std::string> &&strAddr)
{
    if(!(mOptions.appId & strAddr.aid))
        return;

    Address<int32_t> intAddr;
    if (!strAddr.frame.function.empty()) {
        intAddr.frame = convertFrame(std::move(strAddr.frame));
    }
    intAddr.inlined.resize(strAddr.inlined.size());
    for (size_t i=0; i<strAddr.inlined.size(); ++i) {
        intAddr.inlined[i] = convertFrame(std::move(strAddr.inlined[i]));
    }
    EMIT(mFileEmitter.emit(EmitType::StackAddr, static_cast<uint8_t>(strAddr.aid), static_cast<double>(strAddr.ip), static_cast<uint32_t>(intAddr.inlined.size() + 1)));

    EMIT(mFileEmitter.emit(intAddr.frame.function, intAddr.frame.file, intAddr.frame.line));
    for (const Frame<int32_t> &frame : intAddr.inlined) {
        EMIT(mFileEmitter.emit(frame.function, frame.file, frame.line));
    }
}

inline void Parser::emitSnapshot(uint32_t now)
{
    //printf("emitting snapshot\n");
    // send snapshot
    for(auto app = mApplications.begin(); app != mApplications.end(); ++app) {
        if(!(mOptions.appId & app->first))
            continue;

        std::vector<int32_t> newStacks;
        newStacks.reserve(std::min<size_t>(std::max<size_t>((app->second.pageFaults.size() + app->second.mallocs.size() + app->second.mmaps.size()) / 50, 10), 1000));
        auto checkStack = [this, app, &newStacks](int32_t stack) {
            auto it = app->second.pendingStacks.find(stack);
            if (it == app->second.pendingStacks.end())
                return;
            app->second.pendingStacks.erase(it);
            newStacks.push_back(stack);
        };

        // emit a memory as well to ease parsing this in javascript
        EMIT(mFileEmitter.emit(EmitType::Snapshot, app->first, now, static_cast<double>(mLastSnapshot.pageFaultBytes), static_cast<double>(mLastSnapshot.mallocBytes),
                               static_cast<uint32_t>(app->second.pageFaults.size()), static_cast<uint32_t>(app->second.mallocs.size()), static_cast<uint32_t>(app->second.mmaps.size())));

        for (const auto& pf : app->second.pageFaults) {
            EMIT(mFileEmitter.emit(static_cast<double>(pf.place), pf.ptid, pf.stack, pf.time));
            checkStack(pf.stack);
        }
        for (const auto& m : app->second.mallocs) {
            EMIT(mFileEmitter.emit(static_cast<double>(m.addr), static_cast<double>(m.size), m.ptid, m.stack, m.time));
            checkStack(m.stack);
        }
        app->second.mmaps.forEach([this, &checkStack](uintptr_t start, uintptr_t end, int32_t /*prot*/, int32_t /*flags*/, int32_t stack) {
            EMIT(mFileEmitter.emit(static_cast<double>(start), static_cast<double>(end), stack));
            checkStack(stack);
        });

        for (const int32_t stack : newStacks) {
            emitStack(app->second, stack);
        }
    }
}

void Parser::parseThread()
{
    size_t packetSizeCount = 0;
    std::vector<uint8_t> data;
    std::vector<uint32_t> packetSizes;

    size_t totalPacketNo = 0;
    size_t bytesConsumed = 0;

    bool done = false;
    while (!done) {
        // LOG("loop.");
        {
            std::unique_lock<std::mutex> lock(mMutex);
            while (mPacketSizeCount == 0 && !mShutdown) {
                mCond.wait(lock);
            }
            if (mShutdown && mPacketSizeCount == 0) {
                mResolverThread->stop();
                LOG("hepp stacks {}", mStacksResolved);
                done = true;
            } else {
                if (mDataOffset > data.size()) {
                    data.resize(mDataOffset);
                }
                memcpy(data.data(), mData.data(), mDataOffset);
                mDataOffset = 0;

                if (mPacketSizeCount > packetSizes.size()) {
                    packetSizes.resize(mPacketSizeCount);
                }
                memcpy(packetSizes.data(), mPacketSizes.data(), mPacketSizeCount * sizeof(uint32_t));
                packetSizeCount = mPacketSizeCount;
                mPacketSizeCount = 0;
            }
        }

        size_t dataOffset = 0;
        for (size_t packetNo = 0; packetNo < packetSizeCount; ++packetNo) {
            if (!(totalPacketNo % 100000)) {
                if (mOptions.maxEventCount != std::numeric_limits<size_t>::max()) {
                    LOG("parsing packet {}/{} {:.1f}%",
                        totalPacketNo, mOptions.maxEventCount,
                        (static_cast<double>(totalPacketNo) / static_cast<double>(mOptions.maxEventCount)) * 100.0);

                } else if (mOptions.fileSize) {
                    LOG("parsing packet {} {}/{} {:.1f}%",
                        totalPacketNo, bytesConsumed, mOptions.fileSize,
                        (static_cast<double>(bytesConsumed) / static_cast<double>(mOptions.fileSize)) * 100.0);
                } else {
                    LOG("parsing packet {} {}", totalPacketNo, bytesConsumed);
                }
            }
            parsePacket(data.data() + dataOffset, packetSizes[packetNo]);
            dataOffset += packetSizes[packetNo];
            bytesConsumed += packetSizes[packetNo];
            ++totalPacketNo;
        }
        packetSizeCount = 0;

        std::vector<Address<std::string>> resolved;
        {
            std::unique_lock<std::mutex> lock(mResolvedAddressesMutex);
            std::swap(resolved, mResolvedAddresses);
        }
        for (Address<std::string> &strAddress : resolved) {
            emitAddress(std::move(strAddress));
        }
    }

    if (mLastSnapshot.enabled) {
        emitSnapshot(mLastTimestamp);
    }

    LOG("Finished parsing {} events in {}ms", totalPacketNo, mLastTimestamp);
}

static bool comparePageFaultItem(const PageFault& item, uint64_t start)
{
    return item.place < start;
}

void Parser::parsePacket(const uint8_t* data, uint32_t dataSize)
{
    ++mPacketNo;

    auto removePageFaults = [this](Application &app, uint64_t start, uint64_t end) {
        auto item = std::lower_bound(app.pageFaults.begin(), app.pageFaults.end(), start, comparePageFaultItem);
        while (item != app.pageFaults.end() && intersects(start, end, item->place, item->place + Limits::PageSize)) {
            item = app.pageFaults.erase(item);
        }
    };

    auto remapPageFaults = [this](Application &app, uint64_t from, uint64_t to, uint64_t len) {
        const auto fromEnd = from + len;
        std::vector<PageFault> removed;
        // is 5 a good number?
        removed.reserve(5);

        auto item = std::lower_bound(app.pageFaults.begin(), app.pageFaults.end(), from, comparePageFaultItem);
        while (item != app.pageFaults.end() && intersects(from, fromEnd, item->place, item->place + Limits::PageSize)) {
            // remove item at 'from'
            item->place -= from;
            removed.push_back(*item);
            item = app.pageFaults.erase(item);
        }

        for (auto& i : removed) {
            // reinsert item at 'to'
            i.place += to;
            auto newit = std::lower_bound(app.pageFaults.begin(), app.pageFaults.end(), i.place, comparePageFaultItem);
            if (newit == app.pageFaults.end() || newit->place > i.place) {
                app.pageFaults.insert(newit, i);
            }
        }
    };

    // mFileEmitter.emit(EmitType::Time, now);

    size_t offset = 0;

    auto readString = [data, &offset]() {
        uint32_t size;
        memcpy(&size, data + offset, sizeof(size));
        offset += sizeof(size);
        std::string str;
        str.resize(size);
        memcpy(&str[0], data + offset, size);
        offset += size;
        return str;
    };

    auto readUint8 = [data, &offset]() {
        return data[offset++];
    };

    auto readUint32 = [data, &offset]() {
        uint32_t ret;
        memcpy(&ret, data + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        return ret;
    };

    auto readInt32 = [data, &offset]() {
        int32_t ret;
        memcpy(&ret, data + offset, sizeof(int32_t));
        offset += sizeof(int32_t);
        return ret;
    };

    auto readUint64 = [data, &offset]() {
        uint64_t ret;
        memcpy(&ret, data + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        return ret;
    };

    auto readHashable = [&](Hashable::Type type) {
        uint32_t size;
        memcpy(&size, data + offset, sizeof(size));
        offset += sizeof(size);
        if (mHashOffset + size > mHashData.size()) {
            mHashData.resize(std::max(mHashOffset + size, std::min<size_t>(mHashData.size() * 2, 8192)));
        }
        memcpy(mHashData.data() + mHashOffset, data + offset, size);
        const auto [ idx, inserted ] = mHashIndexer.index(Hashable(type, mHashData, mHashOffset, size));
        offset += size;
        mHashOffset += size;
        return std::make_pair(idx, inserted);
    };

    auto readHashableString = [data, &offset, this]() {
        uint32_t size;
        memcpy(&size, data + offset, sizeof(size));
        offset += sizeof(size);
        if (mHashOffset + size > mHashData.size()) {
            mHashData.resize(std::max(mHashOffset + size, std::min<size_t>(mHashData.size() * 2, 8192)));
        }
        memcpy(mHashData.data() + mHashOffset, data + offset, size);
        offset += size;
        mHashOffset += size;
        return Hashable(Hashable::String, mHashData, mHashOffset - size, size);
    };

    bool growth = false;
    const auto type = data[offset++];
    //LOG("gleh {} {} packetno {}\n", type, offset - 1, mPacketNo);
    switch (static_cast<RecordType>(type)) {
    case RecordType::Start: {
        const auto appId = readUint8();
        assert(mApplications.find(appId) == mApplications.end());
        Application app;
        app.id = appId;
        app.startTimestamp = app.lastTimestamp = readUint32();
        if(!mApplications.size())
            mLastMemory.time = mLastSnapshot.time = app.lastTimestamp;
        mLastTimestamp = app.startTimestamp;
        mApplications[appId] = std::move(app);
        if(mOptions.appId & app.id)
            EMIT(mFileEmitter.emit(EmitType::Start, app.id));
        break; }
    case RecordType::Executable: {
        const auto appId = readUint8();
        const auto app = mApplications.find(appId);
        assert(app != mApplications.end());
        app->second.exe = readString();
        break; }
    case RecordType::Command: {
        bool handled = false;
        const CommandType t = static_cast<CommandType>(readUint8());
        switch (t) {
        case CommandType::Invalid:
            break;
        case CommandType::DisableSnapshots:
            mLastSnapshot.enabled = false;
            handled = true;
            break;
        case CommandType::EnableSnapshots:
            mLastSnapshot.enabled = true;
            handled = true;
            break;
        case CommandType::Snapshot: {
            const auto snapshotTime = mLastTimestamp;
            mLastSnapshot.time = snapshotTime;
            mLastSnapshot.pageFaultBytes = currentPageFaultBytes();
            mLastSnapshot.mallocBytes = currentMallocBytes();
            emitSnapshot(snapshotTime);
            const auto name = readHashableString();
            EMIT(mFileEmitter.emit(EmitType::SnapshotName, name));
            handled = true;
            break; }
        }
        if (!handled) {
            LOG("INVALID command type {}", static_cast<int>(t));
            abort();
        }
        break; }
    case RecordType::WorkingDirectory: {
        const auto appId = readUint8();
        const auto app = mApplications.find(appId);
        assert(app != mApplications.end());
        app->second.cwd = readString() + '/';
        break; }
    case RecordType::Library: {
        const auto appId = readUint8();
        const auto app = mApplications.find(appId);
        assert(app != mApplications.end());
        app->second.libraries.push_back(Library{ readString(), readUint64(), {} });
        break; }
    case RecordType::LibraryHeader: {
        const auto appId = readUint8();
        const auto app = mApplications.find(appId);
        assert(app != mApplications.end());
        app->second.libraries.back().headers.push_back(Library::Header { readUint64(), readUint64() });
        break; }
    case RecordType::PageFault: {
        growth = true;
        const auto appId = readUint8();
        const auto app = mApplications.find(appId);
        assert(app != mApplications.end());
        const uint32_t now = app->second.lastTimestamp = readUint32() - app->second.startTimestamp;
        mLastTimestamp = app->second.lastTimestamp = now;
        const auto place = readUint64();
        const auto ptid = readUint32();
        const auto [ stackIdx, stackInserted ] = readHashable(Hashable::Stack);
        //EMIT(mFileEmitter.emit(EmitType::Stack, static_cast<uint32_t>(stackIdx)));
        if (stackInserted) {
            app->second.pendingStacks.insert(stackIdx);
            // resolveStack(stackIdx);
        }
        auto it = std::lower_bound(app->second.pageFaults.begin(), app->second.pageFaults.end(), place, [](const auto& item, auto p) {
            return item.place < p;
        });
        if (it != app->second.pageFaults.end() && it->place == place) {
            // already got this fault?
            break;
        }
        assert(it == app->second.pageFaults.end() || it->place > place);
        app->second.pageFaults.insert(it, PageFault { place, ptid, stackIdx, now });
        //EMIT(mFileEmitter.emit(EmitType::PageFault, static_cast<double>(place), ptid));
        break; }
    case RecordType::PageRemap: {
        const auto appId = readUint8();
        const auto app = mApplications.find(appId);
        assert(app != mApplications.end());
        const auto from = readUint64();
        const auto to = readUint64();
        const auto len = readUint64();
        remapPageFaults(app->second, from, to, len);
        break; }
    case RecordType::PageRemove: {
        const auto appId = readUint8();
        const auto app = mApplications.find(appId);
        assert(app != mApplications.end());
        const auto start = readUint64();
        const auto end = readUint64();
        removePageFaults(app->second, start, end);
        break; }
    case RecordType::Malloc: {
        growth = true;
        const auto appId = readUint8();
        const auto app = mApplications.find(appId);
        assert(app != mApplications.end());
        const uint32_t now = readUint32() - app->second.startTimestamp;
        mLastTimestamp = app->second.lastTimestamp = now;
        const auto addr = readUint64();
        const auto size = readUint64();
        const auto ptid = readUint32();
        const auto [ stackIdx, stackInserted ] = readHashable(Hashable::Stack);
        //EMIT(mFileEmitter.emit(EmitType::Stack, static_cast<uint32_t>(stackIdx)));
        if (stackInserted) {
            app->second.pendingStacks.insert(stackIdx);
            // resolveStack(stackIdx);
        } else {
            // LOG("not inserted");
        }
        app->second.mallocs.insert(Malloc { addr, size, ptid, stackIdx, now });
        app->second.mallocSize += size;
        //printf("[%d] Found malloc(%zu) 0x%lx %ld [%ld] @ %d\n", appId, app->second.mallocs.size(), addr, size, app->second.mallocSize, now);
        //EMIT(mFileEmitter.emit(EmitType::Malloc, ptid));
        break; }
    case RecordType::Free: {
        const auto appId = readUint8();
        const auto app = mApplications.find(appId);
        assert(app != mApplications.end());
        const auto addr = readUint64();
        auto it = app->second.mallocs.find(Malloc { addr, static_cast<uint64_t>(0), static_cast<uint32_t>(0), static_cast<int32_t>(0) });
        if (it != app->second.mallocs.end()) {
            assert(it->addr == addr);
            app->second.mallocSize -= it->size;
            //EMIT(mFileEmitter.emit(EmitType::Malloc, static_cast<double>(app->second.mallocSize)));
            app->second.mallocs.erase(it);
            //printf("[%d] Found free(%zu) 0x%lx %ld [%ld]\n", appId, app->second.mallocs.size(), addr, it->size, app->second.mallocSize);
        }
        break; }
    case RecordType::Mmap: {
        const auto appId = readUint8();
        const auto app = mApplications.find(appId);
        assert(app != mApplications.end());
        const auto addr = readUint64();
        const auto size = readUint64();
        const auto prot = readInt32();
        const auto flags = readInt32();
        const auto ptid = readUint32();
        static_cast<void>(ptid);
        const auto [ stackIdx, stackInserted ] = readHashable(Hashable::Stack);
        //EMIT(mFileEmitter.emit(EmitType::Stack, static_cast<uint32_t>(stackIdx)));
        if (stackInserted) {
            app->second.pendingStacks.insert(stackIdx);
            // resolveStack(stackIdx);
        }
        app->second.mmaps.mmap(addr, size, prot, flags, stackIdx);
        //EMIT(mFileEmitter.emit(EmitType::Mmap, static_cast<double>(addr), static_cast<double>(size)));
        break; }
    case RecordType::Mremap: {
        const auto appId = readUint8();
        const auto app = mApplications.find(appId);
        assert(app != mApplications.end());
        const auto oldAddr = readUint64();
        const auto oldSize = readUint64();
        const auto newAddr = readUint64();
        const auto newSize = readUint64();
        const auto flags = readInt32();
        const auto ptid = readUint64();
        static_cast<void>(flags);
        static_cast<void>(ptid);
        const auto [ stackIdx, stackInserted ] = readHashable(Hashable::Stack);
        if (stackInserted) {
            app->second.pendingStacks.insert(stackIdx);
        }
        app->second.mmaps.mremap(oldAddr, newAddr, oldSize, newSize, stackIdx);
        break; }
    case RecordType::Munmap: {
        const auto appId = readUint8();
        const auto app = mApplications.find(appId);
        assert(app != mApplications.end());
        const auto addr = readUint64();
        const auto size = readUint64();
        // EMIT(mFileEmitter.emit(EmitType::PageFault));
        app->second.mmaps.munmap(addr, size);
        removePageFaults(app->second, addr, addr + size);
        break; }
    case RecordType::ThreadName: {
        const auto appId = readUint8();
        const auto ptid = readUint32();
        const auto name = readHashableString();
        if(mOptions.appId & appId)
            EMIT(mFileEmitter.emit(EmitType::ThreadName, appId, ptid, name));
        break; }
    default:
        LOG("INVALID type {}", type);
        abort();
    }

    if (offset < dataSize) {
        LOG("unexpected remaining bytes {} vs {}", offset, dataSize);
        abort();
    }

    if (growth) {
        const uint64_t mallocBytes = currentMallocBytes();
        const uint64_t pageFaultBytes = currentPageFaultBytes();
        if (mLastMemory.shouldSend(mLastTimestamp, mallocBytes, pageFaultBytes)) {
            // LOG("emitting memory");
            EMIT(mFileEmitter.emit(EmitType::Memory, mLastTimestamp, static_cast<double>(mLastMemory.pageFaultBytes),
                                   static_cast<double>(mLastMemory.mallocBytes)));
        }

        if (mLastSnapshot.shouldSend(mLastTimestamp, mallocBytes, pageFaultBytes)) {
            emitSnapshot(mLastTimestamp);
        }

        if (mOptions.threshold > 0 && mallocBytes + pageFaultBytes >= mOptions.threshold) {
            std::lock_guard<std::mutex> lock(mMutex);
            mThreshold = true;
            emitSnapshot(mLastTimestamp);
        }
    }
}

std::string Parser::visualizerDirectory()
{
    char buf[4096];
    ssize_t l = readlink("/proc/self/exe", buf, sizeof(buf));
    if (l == sizeof(buf))
        return {};

    // find the previous slash
    while (l - 1 >= 0 && buf[l - 1] != '/')
        --l;

    if (l < 0)
        return {};

    return std::string(buf, l) + "../visualizer/";
}

std::string Parser::readFile(const std::string& fn)
{
    FILE* f = fopen(fn.c_str(), "r");
    if (!f)
        return {};

    fseek(f, 0, SEEK_END);
    const size_t sz = static_cast<size_t>(ftell(f));
    fseek(f, 0, SEEK_SET);

    std::string data;
    data.resize(sz);
    if (fread(&data[0], sz, 1, f) != 1)
        return {};

    return data;
}

void Parser::writeHtmlHeader()
{
    if (!mFile)
        return;

    const auto dir = visualizerDirectory();

    const auto htmlData = readFile(dir + "index.html");
    const auto jsData = readFile(dir + "graph.js");
    if (htmlData.empty() || jsData.empty())
        return;

    // write html header
    auto hidx = htmlData.find("// inline js goes here");
    if (hidx == std::string::npos)
        return;

    auto jidx = jsData.find("$DATA_GOES_HERE$");
    if (jidx == std::string::npos)
        return;

    fwrite(htmlData.c_str(), hidx, 1, mFile);
    fwrite(jsData.c_str(), jidx, 1, mFile);
}

void Parser::writeHtmlTrailer()
{
    if (!mFile)
        return;

    const auto dir = visualizerDirectory();

    const auto htmlData = readFile(dir + "index.html");
    const auto jsData = readFile(dir + "graph.js");
    if (htmlData.empty() || jsData.empty())
        return;

    // write html header
    auto hidx = htmlData.find("// inline js goes here");
    if (hidx == std::string::npos)
        return;

    auto jidx = jsData.find("$DATA_GOES_HERE$");
    if (jidx == std::string::npos)
        return;

    fwrite(jsData.c_str() + jidx + 16, jsData.size() - (jidx + 16), 1, mFile);
    fwrite(htmlData.c_str() + hidx + 22, htmlData.size() - (hidx + 22), 1, mFile);
}
