#include "Recorder.h"
#include "Indexer.h"
#include "Module.h"
#include "MmapTracker.h"
#include "NoHook.h"
#include "Types.h"
#include <map>
#include <unistd.h>

thread_local bool Recorder::tScoped = false;

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

static bool intersects(uint64_t startA, uint64_t endA, uint64_t startB, uint64_t endB)
{
    return startA < endB && startB < endA;
}

void Recorder::process(Recorder* r)
{
    NoHook noHook;
    enum { SleepInterval = 250 };

    size_t hashOffset = 0;
    std::vector<uint8_t> hashData;
    Indexer<Hashable> hashIndexer;
    Indexer<std::string> stackAddrIndexer;

    auto timestamp = []() {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
        return static_cast<uint32_t>((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
    };

    const auto start = timestamp();

    std::vector<Library> libraries;
    std::vector<PageFault> pageFaults;
    std::vector<Malloc> mallocs;

    std::map<uint64_t, ModuleEntry> moduleCache;
    std::vector<std::shared_ptr<Module>> modules;

    uint64_t pageFaultSize { 0 }, mallocSize { 0 };
    MmapTracker mmaps;

    std::vector<uint8_t> datavec;
    size_t dataSize;

    std::string exe, cwd;
    Emitter emitter;

    auto resolveStack = [&](int32_t idx) {
        const auto prior = stackAddrIndexer.size();
        if (libraries.size() > modules.size()) {
            // create new modules
            for (size_t libIdx = modules.size(); libIdx < libraries.size(); ++libIdx) {
                const auto& lib = libraries[libIdx];
                if (lib.name.substr(0, 13) == "linux-vdso.so" || lib.name.substr(0, 13) == "linux-gate.so") {
                    // skip
                    continue;
                }
                auto name = lib.name;
                if (name == "s") {
                    name = exe;
                }
                if (name.size() > 0 && name[0] != '/') {
                    // relative path?
                    char buf[4096];
                    name = realpath((cwd + name).c_str(), buf);
                }

                auto module = Module::create(stackAddrIndexer, name, lib.addr);
                for (const auto& hdr : lib.headers) {
                    module->addHeader(hdr.addr, hdr.len);
                }
                modules.push_back(std::move(module));
            }
            moduleCache.clear();
        }
        if (moduleCache.empty()) {
            // update module cache
            for (const auto& m : modules) {
                const auto& rs = m->ranges();
                for (const auto& r : rs) {
                    moduleCache.insert(std::make_pair(r.first, ModuleEntry { r.second, m.get() }));
                }
            }
        }

        const auto& hashable = hashIndexer.value(idx);
        const uint32_t numStacks = hashable.size() / sizeof(void*);
        const uint8_t* data = hashable.data<uint8_t>();
        for (uint32_t i = 0; i < numStacks; ++i) {
            void* ipptr;
            memcpy(&ipptr, data + (i * sizeof(void*)), sizeof(void*));
            const uint64_t ip = reinterpret_cast<uint64_t>(ipptr);

            auto it = moduleCache.upper_bound(ip);
            if (it != moduleCache.begin())
                --it;
            if (moduleCache.size() == 1)
                it = moduleCache.begin();
            Address address;
            if (it != moduleCache.end() && ip >= it->first && ip <= it->second.end)
                address = it->second.module->resolveAddress(ip);
        }

        for (size_t i = prior; i < stackAddrIndexer.size(); ++i) {
            emitter.emit(EmitType::StackAddress, String(stackAddrIndexer.value(i)));
        }
    };

    auto removePageFaults = [&pageFaults, &pageFaultSize](uint64_t start, uint64_t end) {
        auto item = std::lower_bound(pageFaults.begin(), pageFaults.end(), start, [](const auto& item, auto start) {
            return item.place < start;
        });
        while (item != pageFaults.end() && intersects(start, end, item->place, item->place + Limits::PageSize)) {
            item = pageFaults.erase(item);
            assert(pageFaultSize >= Limits::PageSize);
            pageFaultSize -= Limits::PageSize;
        }
    };

    for (;;) {
        {
            // first, copy data to our local data so that other
            // threads can keep recording while we do other things
            ScopedSpinlock lock(r->mLock);
            if (datavec.size() < r->mEmitter.size())
                datavec.resize(r->mEmitter.size());
            memcpy(datavec.data(), r->mEmitter.data(), r->mEmitter.size());
            dataSize = r->mEmitter.size();
            r->mEmitter.reset();
        }

        const auto now = timestamp() - start;
        emitter.emit(EmitType::Time, now);

        uint8_t* data = datavec.data();
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
            if (hashOffset + size > hashData.size()) {
                hashData.resize(std::max(hashOffset + size, std::min<size_t>(hashData.size() * 2, 8192)));
            }
            memcpy(hashData.data() + hashOffset, data + offset, size);
            const auto [ idx, inserted ] = hashIndexer.index(Hashable(type, hashData, hashOffset, size));
            offset += size;
            hashOffset += size;
            return std::make_pair(idx, inserted);
        };

        auto readHashableString = [data, &offset, &hashData, &hashOffset]() {
            uint32_t size;
            memcpy(&size, data + offset, sizeof(size));
            offset += sizeof(size);
            if (hashOffset + size > hashData.size()) {
                hashData.resize(std::max(hashOffset + size, std::min<size_t>(hashData.size() * 2, 8192)));
            }
            memcpy(hashData.data() + hashOffset, data + offset, size);
            offset += size;
            hashOffset += size;
            return Hashable(Hashable::String, hashData, hashOffset - size, size);
        };

        while (offset < dataSize) {
            const auto type = data[offset++];
            //printf("gleh %u\n", type);
            switch (static_cast<RecordType>(type)) {
            case RecordType::Invalid:
                abort();
            case RecordType::Executable:
                exe = readString();
                break;
            case RecordType::WorkingDirectory:
                cwd = readString() + '/';
                break;
            case RecordType::Libraries:
                break;
            case RecordType::Library:
                libraries.push_back(Library{ readString(), readUint64(), {} });
                break;
            case RecordType::LibraryHeader:
                libraries.back().headers.push_back(Library::Header { readUint64(), readUint64() });
                break;
            case RecordType::PageFault: {
                const auto place = readUint64();
                const auto ptid = readUint32();
                const auto [ stackIdx, stackInserted ] = readHashable(Hashable::Stack);
                if (stackInserted) {
                    resolveStack(stackIdx);
                    emitter.emit(EmitType::Stack, hashIndexer.value(stackIdx));
                }
                auto it = std::upper_bound(pageFaults.begin(), pageFaults.end(), place, [](auto place, const auto& item) {
                    return place < item.place;
                });
                pageFaults.insert(it, PageFault { place, ptid, stackIdx });
                pageFaultSize += Limits::PageSize;
                emitter.emit(EmitType::PageFault, pageFaultSize);
                break; }
            case RecordType::Malloc: {
                const auto addr = readUint64();
                const auto size = readUint64();
                const auto ptid = readUint32();
                const auto [ stackIdx, stackInserted ] = readHashable(Hashable::Stack);
                if (stackInserted) {
                    resolveStack(stackIdx);
                    emitter.emit(EmitType::Stack, hashIndexer.value(stackIdx));
                }
                auto it = std::upper_bound(mallocs.begin(), mallocs.end(), addr, [](auto addr, const auto& item) {
                    return addr < item.addr;
                });
                assert(it == mallocs.end() || it->addr > addr);
                mallocs.insert(it, Malloc { addr, size, ptid, stackIdx });
                mallocSize += size;
                emitter.emit(EmitType::Malloc, mallocSize);
                break; }
            case RecordType::Free: {
                const auto addr = readUint64();
                auto it = std::lower_bound(mallocs.begin(), mallocs.end(), addr, [](const auto& item, auto addr) {
                    return item.addr < addr;
                });
                if (it != mallocs.end() && it->addr == addr) {
                    mallocSize -= it->size;
                    emitter.emit(EmitType::Malloc, mallocSize);
                    mallocs.erase(it);
                }
                break; }
            case RecordType::MmapUntracked:
            case RecordType::MmapTracked: {
                const auto addr = readUint64();
                const auto size = readUint64();
                const auto prot = readInt32();
                const auto flags = readInt32();
                const auto ptid = readUint32();
                const auto [ stackIdx, stackInserted ] = readHashable(Hashable::Stack);
                if (stackInserted) {
                    resolveStack(stackIdx);
                    emitter.emit(EmitType::Stack, hashIndexer.value(stackIdx));
                }
                mmaps.mmap(addr, size, prot, flags, stackIdx);
                break; }
            case RecordType::MunmapUntracked:
            case RecordType::MunmapTracked: {
                const auto addr = readUint64();
                const auto size = readUint64();
                removePageFaults(addr, addr + size);
                emitter.emit(EmitType::PageFault, pageFaultSize);
                mmaps.munmap(addr, size);
                break; }
            case RecordType::MadviseUntracked:
            case RecordType::MadviseTracked: {
                const auto addr = readUint64();
                const auto size = readUint64();
                const auto advice = readInt32();
                if (advice == MADV_DONTNEED) {
                    removePageFaults(addr, addr + size);
                    emitter.emit(EmitType::PageFault, pageFaultSize);
                }
                mmaps.madvise(addr, size);
                break; }
            case RecordType::ThreadName: {
                const auto ptid = readUint32();
                const auto name = readHashableString();
                emitter.emit(EmitType::ThreadName, ptid, name);
                break; }
            }
        }

        const char* error = nullptr;
        if (fwrite(emitter.data(), emitter.size(), 1, r->mFile) == 1) {
            fflush(r->mFile);
            emitter.reset();
        } else {
            error = "write error";
        }

        if (error || !r->mRunning.load(std::memory_order_acquire)) {
            if (error) {
                fwrite("Got error:\n", 11, 1, stderr);
                fwrite(error, strlen(error), 1, stderr);
            }
            fclose(r->mFile);
            r->mFile = nullptr;
            return;
        }

        usleep(SleepInterval * 1000);
    }

    ScopedSpinlock lock(r->mLock);
    fclose(r->mFile);
    r->mFile = nullptr;
}

void Recorder::initialize(const char* file)
{
    NoHook noHook;
    mFile = fopen(file, "w");
    if (!mFile) {
        fprintf(stderr, "Unable to open recorder file %s %d %m\n", file, errno);
        abort();
    }

    const uint32_t version = static_cast<uint32_t>(FileVersion::Current);
    if (fwrite(&version, sizeof(uint32_t), 1, mFile) != 1) {
        fprintf(stderr, "Failed to write file version to %s %d %m\n", file, errno);
        abort();
    }

    mRunning.store(true, std::memory_order_release);

    mThread = std::thread(Recorder::process, this);
}
