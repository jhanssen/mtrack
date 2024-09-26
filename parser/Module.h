#pragma once

#include <common/Indexer.h>
#include <common/RecordType.h>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
struct backtrace_state;
};

class Module : public std::enable_shared_from_this<Module>
{
public:
    static std::shared_ptr<Module> create(ApplicationType type,
                                          Indexer<std::string>& indexer,
                                          std::string&& filename,
                                          uint64_t addr);

    void addHeader(uint64_t addr, uint64_t len);

    const std::string& fileName() const;
    uint64_t address() const;
    backtrace_state* state() const { return mState; }
    const std::vector<std::pair<uint64_t, uint64_t>>& ranges() const;

protected:
    Module(ApplicationType type, Indexer<std::string>& indexer,
           const std::string& filename,
           uint64_t addr);

private:
    static void btErrorHandler(void* data, const char* msg, int errnum);

private:
    Indexer<std::string>& mIndexer;
    std::string mFileName;
    uint64_t mAddr;
    std::vector<std::pair<uint64_t, uint64_t>> mRanges;
    backtrace_state* mState { nullptr };

private:
    static std::vector<Module*> sModules;
};

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

namespace std {

template<>
struct hash<std::vector<uint64_t>>
{
public:
    size_t operator()(const std::vector<uint64_t>& stack) const
    {
        size_t h = 0;

        auto hashEntry = [&h](const uint64_t ip) {
            const uint8_t* data = reinterpret_cast<const uint8_t*>(&ip);
            for (size_t i = 0; i < sizeof(i); ++i) {
                h = *(data + i) + (h << 6) + (h << 16) - h;
            }
        };

        for (const auto ip : stack) {
            hashEntry(ip);
        }

        return h;
    }
};

} // namespace std
