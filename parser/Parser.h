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

struct Allocation : public StackEvent
{
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

    bool parse(const std::string& line);
    std::string finalize() const;

private:
    void handleModule(const char* data);
    void handleHeaderLoad(const char* data);
    void handleStack(const char* data);
    void handleExe(const char* data);
    void handleCwd(const char* data);
    void handleThreadName(const char* data);
    void handlePageFault(const char* data);

    void updateModuleCache();

private:
    std::string mExe;
    std::string mCwd;
    std::shared_ptr<Module> mCurrentModule;
    std::unordered_set<std::shared_ptr<Module>> mModules;
    bool mModulesDirty { false };

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
