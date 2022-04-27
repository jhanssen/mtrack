#pragma once

#include "Indexer.h"
#include "Module.h"
#include "json.h"
#include <common/RecordType.h>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

class Event
{
public:
    Event(RecordType type);
    virtual ~Event() = default;

    const RecordType type;

    virtual json to_json() const = 0;
};

class StackEvent : public Event
{
public:
    StackEvent(RecordType type);

    uint64_t stack {};
};

class PageFaultEvent : public StackEvent
{
public:
    PageFaultEvent(uint64_t a, uint64_t s, uint32_t t);

    const uint64_t addr;
    const uint64_t size;
    const uint32_t thread;

    virtual json to_json() const override;
};

class MmapEvent : public StackEvent
{
public:
    MmapEvent(RecordType type, uint64_t a, uint64_t s, uint64_t al, int32_t p, int32_t f, int32_t file, uint64_t o, uint32_t t);

    const uint64_t addr;
    const uint64_t size;
    const uint64_t allocated;
    const int32_t prot;
    const int32_t flags;
    const int32_t fd;
    const uint64_t offset;
    const uint32_t thread;

    virtual json to_json() const override;
};

class MunmapEvent : public Event
{
public:
    MunmapEvent(RecordType type, uint64_t a, uint64_t s, uint64_t d);

    const uint64_t addr;
    const uint64_t size;
    const uint64_t deallocated;

    virtual json to_json() const override;
};

class MadviseEvent : public Event
{
public:
    MadviseEvent(RecordType type, uint64_t a, uint64_t s, int32_t ad, uint64_t d);

    const uint64_t addr;
    const uint64_t size;
    const int32_t advice;
    const uint64_t deallocated;

    virtual json to_json() const override;
};

class TimeEvent : public Event
{
public:
    TimeEvent(uint32_t t);

    // ms since app start
    const uint32_t time;

    virtual json to_json() const override;
};

class Parser
{
public:
    Parser() = default;

    bool parse(const uint8_t* data, size_t size);
    std::string finalize() const;

private:
    inline void handleExe();
    inline void handleLibrary();
    inline void handleLibraryHeader();
    inline void handleMadvise(RecordType type);
    inline void handleMmap(RecordType type);
    inline void handleMunmap(RecordType type);
    inline void handlePageFault();
    inline void handleStack();
    inline void handleThreadName();
    inline void handleTime();
    inline void handleWorkingDirectory();

    inline int32_t hashStack();

    inline void updateModuleCache();

    template<typename T>
    T readData();

private:
    std::string mExe;
    std::string mCwd;
    std::shared_ptr<Module> mCurrentModule;
    std::unordered_set<std::shared_ptr<Module>> mModules;
    bool mModulesDirty { false };

    const uint8_t* mData { nullptr };
    const uint8_t* mEnd { nullptr };
    bool mError { false };

    std::vector<Address> mCurrentStack;

    Indexer<std::string> mStringIndexer;
    Indexer<std::vector<Address>> mStackIndexer;

    struct ModuleEntry
    {
        uint64_t end;
        Module* module;
    };
    std::map<uint64_t, ModuleEntry> mModuleCache;
    std::unordered_map<uint64_t, std::string> mThreadNames;
    std::vector<std::shared_ptr<Event>> mEvents;
    size_t mRecordings {};
};

inline Event::Event(RecordType t)
    : type(t)
{
}

inline StackEvent::StackEvent(RecordType type)
    : Event(type)
{
}

inline PageFaultEvent::PageFaultEvent(uint64_t a, uint64_t s, uint32_t t)
    : StackEvent(RecordType::PageFault), addr(a), size(s), thread(t)
{
}

inline MadviseEvent::MadviseEvent(RecordType type, uint64_t a, uint64_t s, int32_t ad, uint64_t d)
    : Event(type), addr(a), size(s), advice(ad), deallocated(d)
{
}

inline MmapEvent::MmapEvent(RecordType type, uint64_t a, uint64_t s, uint64_t al, int32_t p, int32_t f, int32_t file, uint64_t o, uint32_t t)
    : StackEvent(type), addr(a), size(s), allocated(al), prot(p), flags(f), fd(file), offset(o), thread(t)
{
}

inline MunmapEvent::MunmapEvent(RecordType type, uint64_t a, uint64_t s, uint64_t d)
    : Event(type), addr(a), size(s), deallocated(d)
{
}

inline TimeEvent::TimeEvent(uint32_t t)
    : Event(RecordType::Time), time(t)
{
}

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
