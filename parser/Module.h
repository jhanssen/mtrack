#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
struct backtrace_state;
};

class Module : public std::enable_shared_from_this<Module>
{
public:
    static std::shared_ptr<Module> create(const char* filename, uint64_t addr);
    static std::shared_ptr<Module> create(const std::string& filename, uint64_t addr);

    void addHeader(uint64_t addr, uint64_t len);

    const std::string& fileName() const;
    uint64_t address() const;
    const std::vector<std::pair<uint64_t, uint64_t>>& ranges() const;

    void resolveAddress(uint64_t addr);

protected:
    Module(const char* filename, uint64_t addr);

private:
    static void btErrorHandler(void* data, const char* msg, int errnum);

private:
    std::string mFileName;
    uint64_t mAddr;
    std::vector<std::pair<uint64_t, uint64_t>> mRanges;
    backtrace_state* mState { nullptr };

private:
    static std::unordered_map<uint64_t, std::weak_ptr<Module>> sModuleByName;
};

inline std::shared_ptr<Module> Module::create(const std::string& filename, uint64_t addr)
{
    return create(filename.c_str(), addr);
}

inline const std::string& Module::fileName() const
{
    return mFileName;
}

inline uint64_t Module::address() const
{
    return mAddr;
}

inline const std::vector<std::pair<uint64_t, uint64_t>>& Module::ranges() const
{
    return mRanges;
}
