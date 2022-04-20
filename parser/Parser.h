#pragma once

#include "Module.h"
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>

class Event
{
public:
    enum class Type {
        Allocation
    };

    Event(Type type);
    virtual ~Event() = default;

    Type type() const;
    virtual nlohmann::json to_json() const = 0;

private:
    Type mType;
};

class StackEvent : public Event
{
public:
    StackEvent(Type type);

    std::vector<Address> stack;
    nlohmann::json stack_json() const;
};

class Allocation : public StackEvent
{
public:
    Allocation(uint64_t a, uint64_t s, uint32_t t);

    uint64_t addr;
    uint64_t size;
    uint32_t thread;

    virtual nlohmann::json to_json() const override;
};

class Parser
{
public:
    Parser() = default;

    bool parse(const uint8_t* data, size_t size);
    std::string finalize() const;

private:
    void handleLibrary();
    void handleLibraryHeader();
    void handleStack();
    void handleExe();
    void handleWorkingDirectory();
    void handleThreadName();
    void handlePageFault();

    void updateModuleCache();

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

    struct ModuleEntry
    {
        uint64_t end;
        Module* module;
    };
    std::map<uint64_t, ModuleEntry> mModuleCache;
    std::unordered_map<uint64_t, std::string> mThreadNames;
    std::vector<std::shared_ptr<Event>> mEvents;
};

inline Event::Event(Type type)
    : mType(type)
{
}

inline Event::Type Event::type() const
{
    return mType;
}

inline StackEvent::StackEvent(Type type)
    : Event(type)
{
}

inline Allocation::Allocation(uint64_t a, uint64_t s, uint32_t t)
    : StackEvent(Type::Allocation), addr(a), size(s), thread(t)
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
