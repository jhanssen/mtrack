#pragma once

#include "Module.h"
#include <map>
#include <string>
#include <unordered_set>

class Parser
{
public:
    Parser() = default;

    bool parse(const std::string& line);

private:
    void handleModule(const char* data);
    void handleHeaderLoad(const char* data);
    void handleStack(const char* data);
    void handleExe(const char* data);
    void handleCwd(const char* data);

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
};
