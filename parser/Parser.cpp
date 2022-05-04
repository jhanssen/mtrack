#include "Parser.h"
#include "Logger.h"
#include "ResolverThread.h"
#include <common/Limits.h>
#include <common/Version.h>
#include <common/MmapTracker.h>
#include <fmt/core.h>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <limits>
#include <functional>

enum class EmitType : uint8_t {
    Stack,
    StackString,
    StackAddr,
    StackFrames,
    ThreadName,
    Time,
    Memory,
    Malloc,
    PageFault,
    Mmap
};

namespace {
bool intersects(uint64_t startA, uint64_t endA, uint64_t startB, uint64_t endB)
{
    return startA < endB && startB < endA;
}

uint32_t timestamp()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
    return static_cast<uint32_t>((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}
} // anonymous namespace

#define DEBUG_EMITS
#ifdef DEBUG_EMITS
static std::map<int, size_t> emitted;
#define EMIT(cmd) emitted[__LINE__] += cmd
#else
#define EMIT(cmd) cmd
#endif

Parser::Parser(const Options& options)
    : mOptions(options), mFileEmitter(options.output), mResolverThread(std::make_unique<ResolverThread>(this))
{
    mThread = std::thread(std::bind(&Parser::parseThread, this));
    if (options.maxEventCount != std::numeric_limits<size_t>::max() && options.maxEventCount > 0) {
        mData.resize(options.maxEventCount * 16); // ??? ### WTF BBQ?
    } else if (options.fileSize != std::numeric_limits<size_t>::max() && options.fileSize > 0) {
        mData.resize(options.fileSize);
    }
}

Parser::~Parser()
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mShutdown = true;
        mCond.notify_one();
    }
    mThread.join();
    mResolverThread->stop();

#ifdef DEBUG_EMITS
    for (const auto &ref : emitted) {
        printf("%d %lu\n", ref.first, ref.second);
    }
#endif
}

void Parser::onResolvedAddresses(std::vector<Address<std::string>>&& addresses)
{
    std::unique_lock<std::mutex> lock(mResolvedAddressesMutex);
    mResolvedAddresses.insert(mResolvedAddresses.end(), std::make_move_iterator(addresses.begin()), std::make_move_iterator(addresses.end()));
}

void Parser::resolveStack(int32_t idx)
{
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
    EMIT(mFileEmitter.emit(EmitType::StackFrames, numFrames));
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
            EMIT(mFileEmitter.emit(EmitType::StackString, Emitter::String(mStringIndexer.value(i))));
        }
        ret.function = i;
    }
    if (!frame.file.empty()) {
        const auto [ i, inserted ] = mStringIndexer.index(std::move(frame.file));
        if (inserted) {
            EMIT(mFileEmitter.emit(EmitType::StackString, Emitter::String(mStringIndexer.value(i))));
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

void Parser::parseThread()
{
    const uint32_t started = timestamp();
    size_t packetSizeCount = 0;
    std::vector<uint8_t> data;
    std::vector<uint32_t> packetSizes;

    size_t totalPacketNo = 0;
    size_t bytesConsumed = 0;
    mStart = timestamp();
    mLastSnapshot.time = mStart;

    for (;;) {
        // LOG("loop.");
        {
            std::unique_lock<std::mutex> lock(mMutex);
            while (mPacketSizeCount == 0 && !mShutdown) {
                mCond.wait(lock);
            }
            if (mShutdown && mPacketSizeCount == 0)
                break;

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

        std::vector<Address<std::string>> resolved;
        {
            std::unique_lock<std::mutex> lock(mResolvedAddressesMutex);
            std::swap(resolved, mResolvedAddresses);
        }
        for (Address<std::string> &strAddress : resolved) {
            emitAddress(std::move(strAddress));
        }
    }

    const uint32_t now = timestamp();
    LOG("Finished parsing {} events in {}ms", totalPacketNo, now - started);
}

void Parser::parsePacket(const uint8_t* data, uint32_t dataSize)
{
    ++mPacketNo;

    auto removePageFaults = [this](uint64_t start, uint64_t end) {
        auto item = std::lower_bound(mPageFaults.begin(), mPageFaults.end(), start, [](const auto& item, auto start) {
            return item.place < start;
        });
        while (item != mPageFaults.end() && intersects(start, end, item->place, item->place + Limits::PageSize)) {
            item = mPageFaults.erase(item);
            // assert(mPageFaultSize >= Limits::PageSize);
            // mPageFaultSize -= Limits::PageSize;
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
    case RecordType::Executable:
        mExe = readString();
        break;
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
        const auto place = readUint64();
        const auto ptid = readUint32();
        const auto [ stackIdx, stackInserted ] = readHashable(Hashable::Stack);
        EMIT(mFileEmitter.emit(EmitType::Stack, static_cast<uint32_t>(stackIdx)));
        if (stackInserted) {
            mPendingStacks.insert(stackIdx);
            resolveStack(stackIdx);
        }
        auto it = std::upper_bound(mPageFaults.begin(), mPageFaults.end(), place, [](auto place, const auto& item) {
            return place < item.place;
        });
        now = timestamp();
        mPageFaults.insert(it, PageFault { place, ptid, stackIdx, now - mStart });
        EMIT(mFileEmitter.emit(EmitType::PageFault, static_cast<double>(place), ptid));
        break; }
    case RecordType::Malloc: {
        const auto addr = readUint64();
        const auto size = readUint64();
        const auto ptid = readUint32();
        const auto [ stackIdx, stackInserted ] = readHashable(Hashable::Stack);
        EMIT(mFileEmitter.emit(EmitType::Stack, static_cast<uint32_t>(stackIdx)));
        if (stackInserted) {
            mPendingStacks.insert(stackIdx);
            resolveStack(stackIdx);
        } else {
            // LOG("not inserted");
        }
        now = timestamp();
        mMallocs.insert(Malloc { addr, size, ptid, stackIdx, now - mStart });
        mMallocSize += size;
        EMIT(mFileEmitter.emit(EmitType::Malloc, ptid));
        break; }
    case RecordType::Free: {
        const auto addr = readUint64();
        auto it = mMallocs.find(Malloc { addr, static_cast<uint64_t>(0), static_cast<uint32_t>(0), static_cast<int32_t>(0) });
        if (it != mMallocs.end()) {
            assert(it->addr == addr);
            mMallocSize -= it->size;
            EMIT(mFileEmitter.emit(EmitType::Malloc, static_cast<double>(mMallocSize)));
            mMallocs.erase(it);
        }
        break; }
    case RecordType::MmapUntracked:
    case RecordType::MmapTracked: {
        const auto addr = readUint64();
        const auto size = readUint64();
        const auto prot = readInt32();
        const auto flags = readInt32();
        const auto ptid = readUint32();
        static_cast<void>(ptid);
        const auto [ stackIdx, stackInserted ] = readHashable(Hashable::Stack);
        EMIT(mFileEmitter.emit(EmitType::Stack, static_cast<uint32_t>(stackIdx)));
        if (stackInserted) {
            mPendingStacks.insert(stackIdx);
            resolveStack(stackIdx);
        }
        mMmaps.mmap(addr, size, prot, flags, stackIdx);
        EMIT(mFileEmitter.emit(EmitType::Mmap, static_cast<double>(addr), static_cast<double>(size)));
        break; }
    case RecordType::MunmapUntracked:
    case RecordType::MunmapTracked: {
        const auto addr = readUint64();
        const auto size = readUint64();
        removePageFaults(addr, addr + size);
        // EMIT(mFileEmitter.emit(EmitType::PageFault));
        mMmaps.munmap(addr, size);
        break; }
    case RecordType::MadviseUntracked:
    case RecordType::MadviseTracked: {
        const auto addr = readUint64();
        const auto size = readUint64();
        const auto advice = readInt32();
        if (advice == MADV_DONTNEED) {
            removePageFaults(addr, addr + size);
            // EMIT(mFileEmitter.emit(EmitType::PageFault));
        }
        mMmaps.madvise(addr, size);
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

    if (now) {
        const uint64_t pageFaultSize = mPageFaults.size() * Limits::PageSize;
        if (mLastMemory.shouldSend(now, 250, mMallocSize, pageFaultSize, .1)) {
            mFileEmitter.emit(EmitType::Memory, now, mLastMemory.mallocBytes, mLastMemory.pageFaultBytes);
        }

        if (mLastSnapshot.shouldSend(now, 10000, mMallocSize, pageFaultSize, .2)) {
            // send snapshot
        }
    }
    // if (mLastMemoryTime
}

