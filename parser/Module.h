#pragma once

#include <common/Indexer.h>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

extern "C" {
struct backtrace_state;
};

struct Frame
{
    int32_t function { -1 };
    int32_t file { -1 };
    int32_t line { 0 };

    bool operator==(const Frame& other) const
    {
        return function == other.function && file == other.file && line == other.line;
    }
};

struct Address
{
    Frame frame;
    std::vector<Frame> inlined;

    bool valid() const;

    bool operator==(const Address&) const = default;
};

class Module : public std::enable_shared_from_this<Module>
{
public:
    static std::shared_ptr<Module> create(Indexer<std::string>& indexer, const std::string& filename, uint64_t addr);

    void addHeader(uint64_t addr, uint64_t len);

    const std::string& fileName() const;
    uint64_t address() const;
    const std::vector<std::pair<uint64_t, uint64_t>>& ranges() const;

    Address resolveAddress(uint64_t addr);

protected:
    Module(Indexer<std::string>& indexer, const std::string& filename, uint64_t addr);

private:
    static void btErrorHandler(void* data, const char* msg, int errnum);

private:
    Indexer<std::string>& mIndexer;
    std::string mFileName;
    uint64_t mAddr;
    std::vector<std::pair<uint64_t, uint64_t>> mRanges;
    backtrace_state* mState { nullptr };
    bool mFirst { true };

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

inline bool Address::valid() const
{
    return frame.function != -1;
}

namespace std {

template<>
struct hash<std::vector<uint64_t>>
{
public:
    size_t operator()(const std::vector<uint64_t>& stack) const
    {
        size_t hash = 0;

        auto hashEntry = [&hash](const uint64_t ip) {
            const uint8_t* data = reinterpret_cast<const uint8_t*>(&ip);
            for (size_t i = 0; i < sizeof(i); ++i) {
                hash = *(data + i) + (hash << 6) + (hash << 16) - hash;
            }
        };

        for (const auto ip : stack) {
            hashEntry(ip);
        }

        return hash;
    }
};

} // namespace std
