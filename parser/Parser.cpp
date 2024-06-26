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

inline uint32_t timestamp()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
    return static_cast<uint32_t>((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
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

void Parser::emitStack(int32_t idx)
{
    ++mStacksResolved;
    // auto prior = mStackAddrIndexer.size();
    if (mLibraries.size() > mModules.size()) {
        // create new modules
        for (size_t libIdx = mModules.size(); libIdx < mLibraries.size(); ++libIdx) {
            const auto& lib = mLibraries[libIdx];
            if (lib.name.substr(0, 13) == "linux-vdso.so" || lib.name.substr(0, 13) == "linux-gate.so") {
                // skip
                continue;
            }
            auto name = lib.name;
            if (name == "s") {
                name = mExe;
            }
            if (name.size() > 0 && name[0] != '/') {
                // relative path?
                char buf[4096];
                name = realpath((mCwd + name).c_str(), buf);
            }

            auto module = Module::create(mStringIndexer, std::move(name), lib.addr);
            for (const auto& hdr : lib.headers) {
                module->addHeader(hdr.addr, hdr.len);
            }
            mModules.push_back(std::move(module));
        }
        mModuleCache.clear();
    }
    if (mModuleCache.empty()) {
        // update module cache
        for (const auto& m : mModules) {
            const auto& rs = m->ranges();
            for (const auto& r : rs) {
                mModuleCache.insert(std::make_pair(r.first, ModuleEntry { r.second, m.get() }));
            }
        }
    }

    const auto& hashable = mHashIndexer.value(idx);
    const uint32_t numFrames = hashable.size() / sizeof(void*);
    const uint8_t* data = hashable.data<uint8_t>();
    std::vector<UnresolvedAddress> stackFrames;
    stackFrames.resize(numFrames);
    std::vector<UnresolvedAddress> unresolved;
    EMIT(mFileEmitter.emit(EmitType::Stack, idx, numFrames));
    for (uint32_t i = 0; i < numFrames; ++i) {
        void* ipptr;
        memcpy(&ipptr, data + (i * sizeof(void*)), sizeof(void*));
        const uint64_t ip = reinterpret_cast<uint64_t>(ipptr);

        auto ait = mAddressCache.find(ip);
        if (ait == mAddressCache.end()) {
            auto it = mModuleCache.upper_bound(ip);
            if (it != mModuleCache.begin())
                --it;
            if (mModuleCache.size() == 1)
                it = mModuleCache.begin();
            if (it != mModuleCache.end() && ip >= it->first && ip <= it->second.end) {
                unresolved.push_back({ ip, it->second.module->state() });
                mAddressCache[ip] = std::nullopt;
            } else {
                mAddressCache[ip] = Address<int32_t>();
            }
        }
        EMIT(mFileEmitter.emit(static_cast<double>(ip)));
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
    Address<int32_t> intAddr;
    if (!strAddr.frame.function.empty()) {
        intAddr.frame = convertFrame(std::move(strAddr.frame));
    }
    intAddr.inlined.resize(strAddr.inlined.size());
    for (size_t i=0; i<strAddr.inlined.size(); ++i) {
        intAddr.inlined[i] = convertFrame(std::move(strAddr.inlined[i]));
    }
    EMIT(mFileEmitter.emit(EmitType::StackAddr, static_cast<double>(strAddr.ip), static_cast<uint32_t>(intAddr.inlined.size() + 1)));

    EMIT(mFileEmitter.emit(intAddr.frame.function, intAddr.frame.file, intAddr.frame.line));
    for (const Frame<int32_t> &frame : intAddr.inlined) {
        EMIT(mFileEmitter.emit(frame.function, frame.file, frame.line));
    }
}

inline void Parser::emitSnapshot(uint32_t now)
{
    // LOG("emitting snapshot");
    // send snapshot

    std::vector<int32_t> newStacks;
    newStacks.reserve(std::min<size_t>(std::max<size_t>((mPageFaults.size() + mMallocs.size() + mMmaps.size()) / 50, 10), 1000));
    auto checkStack = [this, &newStacks](int32_t stack) {
        auto it = mPendingStacks.find(stack);
        if (it == mPendingStacks.end())
            return;
        mPendingStacks.erase(it);
        newStacks.push_back(stack);
    };

    // emit a memory as well to ease parsing this in javascript
    EMIT(mFileEmitter.emit(EmitType::Snapshot, now, static_cast<double>(mLastSnapshot.pageFaultBytes), static_cast<double>(mLastSnapshot.mallocBytes),
                           static_cast<uint32_t>(mPageFaults.size()), static_cast<uint32_t>(mMallocs.size()),
                           static_cast<uint32_t>(mMmaps.size())));

    for (const auto& pf : mPageFaults) {
        EMIT(mFileEmitter.emit(static_cast<double>(pf.place), pf.ptid, pf.stack, pf.time));
        checkStack(pf.stack);
    }
    for (const auto& m : mMallocs) {
        EMIT(mFileEmitter.emit(static_cast<double>(m.addr), static_cast<double>(m.size), m.ptid, m.stack, m.time));
        checkStack(m.stack);
    }
    mMmaps.forEach([this, &checkStack](uintptr_t start, uintptr_t end, int32_t /*prot*/, int32_t /*flags*/, int32_t stack) {
        EMIT(mFileEmitter.emit(static_cast<double>(start), static_cast<double>(end), stack));
        checkStack(stack);
    });

    for (const int32_t stack : newStacks) {
        emitStack(stack);
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

    const auto lastTime = timestamp() - mStartTimestamp;
    if (mLastSnapshot.enabled) {
        emitSnapshot(lastTime);
    }

    LOG("Finished parsing {} events in {}ms", totalPacketNo, lastTime);
}

static bool comparePageFaultItem(const PageFault& item, uint64_t start)
{
    return item.place < start;
}

void Parser::parsePacket(const uint8_t* data, uint32_t dataSize)
{
    ++mPacketNo;

    auto removePageFaults = [this](uint64_t start, uint64_t end) {
        auto item = std::lower_bound(mPageFaults.begin(), mPageFaults.end(), start, comparePageFaultItem);
        while (item != mPageFaults.end() && intersects(start, end, item->place, item->place + Limits::PageSize)) {
            item = mPageFaults.erase(item);
        }
    };

    auto remapPageFaults = [this](uint64_t from, uint64_t to, uint64_t len) {
        const auto fromEnd = from + len;
        std::vector<PageFault> removed;
        // is 5 a good number?
        removed.reserve(5);

        auto item = std::lower_bound(mPageFaults.begin(), mPageFaults.end(), from, comparePageFaultItem);
        while (item != mPageFaults.end() && intersects(from, fromEnd, item->place, item->place + Limits::PageSize)) {
            // remove item at 'from'
            item->place -= from;
            removed.push_back(*item);
            item = mPageFaults.erase(item);
        }

        for (auto& i : removed) {
            // reinsert item at 'to'
            i.place += to;
            auto newit = std::lower_bound(mPageFaults.begin(), mPageFaults.end(), i.place, comparePageFaultItem);
            if (newit == mPageFaults.end() || newit->place > i.place) {
                mPageFaults.insert(newit, i);
            }
        }
    };

    uint32_t now = std::numeric_limits<uint32_t>::max();
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

    const auto type = data[offset++];
    //LOG("gleh {} {} packetno {}\n", type, offset - 1, mPacketNo);
    switch (static_cast<RecordType>(type)) {
    case RecordType::Start:
        mStartTimestamp = readUint32();
        mLastMemory.time = mLastSnapshot.time = timestamp() - mStartTimestamp;
        break;
    case RecordType::Executable:
        mExe = readString();
        break;
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
            const auto snapshotTime = timestamp() - mStartTimestamp;
            mLastSnapshot.time = snapshotTime;
            mLastSnapshot.pageFaultBytes = mPageFaults.size() * Limits::PageSize;
            mLastSnapshot.mallocBytes = mMallocSize;
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
    case RecordType::WorkingDirectory:
        mCwd = readString() + '/';
        break;
    case RecordType::Library:
        mLibraries.push_back(Library{ readString(), readUint64(), {} });
        break;
    case RecordType::LibraryHeader:
        mLibraries.back().headers.push_back(Library::Header { readUint64(), readUint64() });
        break;
    case RecordType::PageFault: {
        now = readUint32() - mStartTimestamp;
        const auto place = readUint64();
        const auto ptid = readUint32();
        const auto [ stackIdx, stackInserted ] = readHashable(Hashable::Stack);
        //EMIT(mFileEmitter.emit(EmitType::Stack, static_cast<uint32_t>(stackIdx)));
        if (stackInserted) {
            mPendingStacks.insert(stackIdx);
            // resolveStack(stackIdx);
        }
        auto it = std::lower_bound(mPageFaults.begin(), mPageFaults.end(), place, [](const auto& item, auto p) {
            return item.place < p;
        });
        if (it != mPageFaults.end() && it->place == place) {
            // already got this fault?
            break;
        }
        assert(it == mPageFaults.end() || it->place > place);
        mPageFaults.insert(it, PageFault { place, ptid, stackIdx, now });
        //EMIT(mFileEmitter.emit(EmitType::PageFault, static_cast<double>(place), ptid));
        break; }
    case RecordType::PageRemap: {
        const auto from = readUint64();
        const auto to = readUint64();
        const auto len = readUint64();
        remapPageFaults(from, to, len);
        break; }
    case RecordType::PageRemove: {
        const auto start = readUint64();
        const auto end = readUint64();
        removePageFaults(start, end);
        break; }
    case RecordType::Malloc: {
        now = readUint32() - mStartTimestamp;
        const auto addr = readUint64();
        const auto size = readUint64();
        const auto ptid = readUint32();
        const auto [ stackIdx, stackInserted ] = readHashable(Hashable::Stack);
        //EMIT(mFileEmitter.emit(EmitType::Stack, static_cast<uint32_t>(stackIdx)));
        if (stackInserted) {
            mPendingStacks.insert(stackIdx);
            // resolveStack(stackIdx);
        } else {
            // LOG("not inserted");
        }
        mMallocs.insert(Malloc { addr, size, ptid, stackIdx, now });
        mMallocSize += size;
        //EMIT(mFileEmitter.emit(EmitType::Malloc, ptid));
        break; }
    case RecordType::Free: {
        const auto addr = readUint64();
        auto it = mMallocs.find(Malloc { addr, static_cast<uint64_t>(0), static_cast<uint32_t>(0), static_cast<int32_t>(0) });
        if (it != mMallocs.end()) {
            assert(it->addr == addr);
            mMallocSize -= it->size;
            //EMIT(mFileEmitter.emit(EmitType::Malloc, static_cast<double>(mMallocSize)));
            mMallocs.erase(it);
        }
        break; }
    case RecordType::Mmap: {
        const auto addr = readUint64();
        const auto size = readUint64();
        const auto prot = readInt32();
        const auto flags = readInt32();
        const auto ptid = readUint32();
        static_cast<void>(ptid);
        const auto [ stackIdx, stackInserted ] = readHashable(Hashable::Stack);
        //EMIT(mFileEmitter.emit(EmitType::Stack, static_cast<uint32_t>(stackIdx)));
        if (stackInserted) {
            mPendingStacks.insert(stackIdx);
            // resolveStack(stackIdx);
        }
        mMmaps.mmap(addr, size, prot, flags, stackIdx);
        //EMIT(mFileEmitter.emit(EmitType::Mmap, static_cast<double>(addr), static_cast<double>(size)));
        break; }
    case RecordType::Mremap: {
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
            mPendingStacks.insert(stackIdx);
        }
        mMmaps.mremap(oldAddr, newAddr, oldSize, newSize, stackIdx);
        break; }
    case RecordType::Munmap: {
        const auto addr = readUint64();
        const auto size = readUint64();
        // EMIT(mFileEmitter.emit(EmitType::PageFault));
        mMmaps.munmap(addr, size);
        removePageFaults(addr, addr + size);
        break; }
    case RecordType::ThreadName: {
        const auto ptid = readUint32();
        const auto name = readHashableString();
        EMIT(mFileEmitter.emit(EmitType::ThreadName, ptid, name));
        break; }
    default:
        LOG("INVALID type {}", type);
        abort();
    }

    if (offset < dataSize) {
        LOG("unexpected remaining bytes {} vs {}", offset, dataSize);
        abort();
    }

    if (now != std::numeric_limits<uint32_t>::max()) {
        const uint64_t pageFaultSize = mPageFaults.size() * Limits::PageSize;
        if (mLastMemory.shouldSend(now, mMallocSize, pageFaultSize)) {
            // LOG("emitting memory");
            EMIT(mFileEmitter.emit(EmitType::Memory, now, static_cast<double>(mLastMemory.pageFaultBytes),
                                   static_cast<double>(mLastMemory.mallocBytes)));
        }

        if (mLastSnapshot.shouldSend(now, mMallocSize, pageFaultSize)) {
            emitSnapshot(now);
        }

        if (mOptions.threshold > 0 && mMallocSize + pageFaultSize >= mOptions.threshold) {
            std::lock_guard<std::mutex> lock(mMutex);
            mThreshold = true;

            emitSnapshot(now);
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
