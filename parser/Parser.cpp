#include "Parser.h"
#include "Logger.h"
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
    StackAddress,
    Malloc,
    PageFault,
    ThreadName,
    Time
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


Parser::Parser(const std::string& file)
    : mFileEmitter(file)
{
    mThread = std::thread(std::bind(&Parser::parseThread, this));
}

Parser::~Parser()
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mShutdown = true;
        mCond.notify_one();
    }
    mThread.join();
}

void Parser::parseThread()
{
    const uint32_t started = timestamp();
    size_t packetSizeCount = 0;
    std::vector<uint8_t> data;
    std::vector<uint32_t> packetSizes;

    size_t totalPacketNo = 0;
    size_t bytesConsumed = 0;

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
                if (mMaxEvents != std::numeric_limits<size_t>::max()) {
                    LOG("parsing packet {}/{} {:.1f}%",
                        totalPacketNo, mMaxEvents,
                        (static_cast<double>(totalPacketNo) / static_cast<double>(mMaxEvents)) * 100.0);

                } else if (mFileSize) {
                    LOG("parsing packet {} {}/{} {:.1f}%",
                        totalPacketNo, bytesConsumed, mFileSize,
                        (static_cast<double>(bytesConsumed) / static_cast<double>(mFileSize)) * 100.0);
                } else {
                    LOG("parsing packet {} {}", totalPacketNo, bytesConsumed);
                }
            }
            parsePacket(data.data() + dataOffset, packetSizes[packetNo]);
            dataOffset += packetSizes[packetNo];
            bytesConsumed += packetSizes[packetNo];
            ++totalPacketNo;
        }
    }

    const uint32_t now = timestamp();
    LOG("Finished parsing {} events in {}ms", totalPacketNo, now - started);
}

void Parser::parsePacket(const uint8_t* data, uint32_t dataSize)
{
    ++mPacketNo;

    // const auto start = timestamp();

    auto resolveStack = [this](int32_t idx) {
        // const auto prior = mStackAddrIndexer.size();
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

                auto module = Module::create(mStackAddrIndexer, name, lib.addr);
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
        const uint32_t numStacks = hashable.size() / sizeof(void*);
        const uint8_t* data = hashable.data<uint8_t>();
        for (uint32_t i = 0; i < numStacks; ++i) {
            Address address;
            void* ipptr;
            memcpy(&ipptr, data + (i * sizeof(void*)), sizeof(void*));
            const uint64_t ip = reinterpret_cast<uint64_t>(ipptr);

            auto ait = mAddressCache.find(ip);
            if (ait != mAddressCache.end()) {
                address = ait->second;
            } else {
                auto it = mModuleCache.upper_bound(ip);
                if (it != mModuleCache.begin())
                    --it;
                if (mModuleCache.size() == 1)
                    it = mModuleCache.begin();
                if (it != mModuleCache.end() && ip >= it->first && ip <= it->second.end) {
                    address = it->second.module->resolveAddress(ip);
                    mAddressCache[ip] = address;
                }
            }
        }

        // for (size_t i = prior; i < mStackAddrIndexer.size(); ++i) {
        //     mFileEmitter.emit(EmitType::StackAddress, Emitter::String(mStackAddrIndexer.value(i)));
        // }
    };

    auto removePageFaults = [this](uint64_t start, uint64_t end) {
        auto item = std::lower_bound(mPageFaults.begin(), mPageFaults.end(), start, [](const auto& item, auto start) {
            return item.place < start;
        });
        while (item != mPageFaults.end() && intersects(start, end, item->place, item->place + Limits::PageSize)) {
            item = mPageFaults.erase(item);
            assert(mPageFaultSize >= Limits::PageSize);
            mPageFaultSize -= Limits::PageSize;
        }
    };

    // const auto now = timestamp() - start;
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
        if (stackInserted) {
            resolveStack(stackIdx);
            // mFileEmitter.emit(EmitType::Stack, mHashIndexer.value(stackIdx));
        }
        auto it = std::upper_bound(mPageFaults.begin(), mPageFaults.end(), place, [](auto place, const auto& item) {
            return place < item.place;
        });
        mPageFaults.insert(it, PageFault { place, ptid, stackIdx });
        mPageFaultSize += Limits::PageSize;
        // mFileEmitter.emit(EmitType::PageFault, mPageFaultSize);
        break; }
    case RecordType::Malloc: {
        const auto addr = readUint64();
        const auto size = readUint64();
        const auto ptid = readUint32();
        const auto [ stackIdx, stackInserted ] = readHashable(Hashable::Stack);
        if (stackInserted) {
            resolveStack(stackIdx);
            // mFileEmitter.emit(EmitType::Stack, mHashIndexer.value(stackIdx));
        } else {
            // LOG("not inserted");
        }
        mMallocs.insert(Malloc { addr, size, ptid, stackIdx });
        mMallocSize += size;
        // mFileEmitter.emit(EmitType::Malloc, mMallocSize);
        break; }
    case RecordType::Free: {
        const auto addr = readUint64();
        auto it = mMallocs.find(Malloc { addr, static_cast<uint64_t>(0), static_cast<uint32_t>(0), static_cast<int32_t>(0) });
        if (it != mMallocs.end()) {
            assert(it->addr == addr);
            mMallocSize -= it->size;
            // mFileEmitter.emit(EmitType::Malloc, mMallocSize);
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
        if (stackInserted) {
            resolveStack(stackIdx);
            // mFileEmitter.emit(EmitType::Stack, mHashIndexer.value(stackIdx));
        }
        mMmaps.mmap(addr, size, prot, flags, stackIdx);
        break; }
    case RecordType::MunmapUntracked:
    case RecordType::MunmapTracked: {
        const auto addr = readUint64();
        const auto size = readUint64();
        removePageFaults(addr, addr + size);
        // mFileEmitter.emit(EmitType::PageFault, mPageFaultSize);
        mMmaps.munmap(addr, size);
        break; }
    case RecordType::MadviseUntracked:
    case RecordType::MadviseTracked: {
        const auto addr = readUint64();
        const auto size = readUint64();
        const auto advice = readInt32();
        if (advice == MADV_DONTNEED) {
            removePageFaults(addr, addr + size);
            // mFileEmitter.emit(EmitType::PageFault, mPageFaultSize);
        }
        mMmaps.madvise(addr, size);
        break; }
    case RecordType::ThreadName: {
        const auto ptid = readUint32();
        const auto name = readHashableString();
        static_cast<void>(ptid);
        static_cast<void>(name);
        // mFileEmitter.emit(EmitType::ThreadName, ptid, name);
        break; }
    default:
        LOG("INVALID type {}", type);
        abort();
    }

    if (offset < dataSize) {
        LOG("unexpected remaining bytes {} vs {}", offset, dataSize);
        abort();
    }
}

void Parser::setFileSize(size_t size, size_t maxEvents)
{
    mFileSize = size;
    mMaxEvents = maxEvents;
    assert(!mDataOffset);
    mData.resize(size);
}

