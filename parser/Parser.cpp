#include "Parser.h"
#include <common/Version.h>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <limits>

static inline std::pair<uint64_t, size_t> parseNumber(const char* data, int base = 10)
{
    char* end = nullptr;
    const auto ret = strtoull(data, &end, base);
    return std::make_pair(static_cast<uint64_t>(ret), static_cast<size_t>(end - data));
}

static inline std::pair<std::string, size_t> parseString(const char* data)
{
    // skip spaces
    size_t off = 0;
    size_t qstart = std::numeric_limits<size_t>::max();
    for (;; ++off) {
        switch (*(data + off)) {
        case '\0':
            // done
            return std::make_pair(std::string {}, size_t {});
        case '"':
            if (qstart == std::numeric_limits<size_t>::max()) {
                qstart = off + 1;
            } else {
                // done
                return std::make_pair(std::string(data + qstart, data + off), off + 1);
            }
        case ' ':
        case '\t':
            break;
        default:
            if (qstart == std::numeric_limits<size_t>::max()) {
                // something bad happened, our first non-space char needs to be a '"'
                return std::make_pair(std::string {}, size_t {});
            }
            break;
        }
    }
    __builtin_unreachable();
}

void Parser::handleModule(const char* data)
{
    auto [ name, off1 ] = parseString(data);
    if (name.substr(0, 13) == "linux-vdso.so") {
        // skip this
        return;
    }

    const auto [ start, off2 ] = parseNumber(data + off1, 16);
    if (name == "s") {
        name = mExe;
    }
    if (name.size() > 0 && name[0] != '/') {
        // relative path?
        char buf[4096];
        name = realpath((mCwd + name).c_str(), buf);
    }

    printf("dlll '%s' %lx\n", name.c_str(), start);

    auto mod = Module::create(name, start);
    mCurrentModule = mod;
    mModules.insert(mod);
}

void Parser::updateModuleCache()
{
    mModuleCache.clear();

    for (const auto& m : mModules) {
        const auto& rs = m->ranges();
        for (const auto& r : rs) {
            mModuleCache.insert(std::make_pair(r.first, ModuleEntry { r.second, m.get() }));
        }
    }
}

void Parser::handleStack(const char* data)
{
    // two hex numbers
    const auto [ ip, off1 ] = parseNumber(data, 16);
    const auto [ sp, off2 ] = parseNumber(data + off1, 16);
    printf("sttt %lx %lx\n", ip, sp);

    if (mModulesDirty) {
        updateModuleCache();
        mModulesDirty = false;
    }

    // find ip in module cache
    auto it = mModuleCache.upper_bound(ip);
    if (it != mModuleCache.begin())
        --it;
    if (mModuleCache.size() == 1)
        it = mModuleCache.begin();
    if (it != mModuleCache.end() && ip >= it->first && ip <= it->second.end) {
        auto mod = it->second.module;
        printf("found module %s\n", mod->fileName().c_str());
    }
}

void Parser::handleHeaderLoad(const char* data)
{
    // two hex numbers
    const auto [ addr, off1 ] = parseNumber(data, 16);
    const auto [ size, off2 ] = parseNumber(data + off1, 16);

    assert(mCurrentModule);
    printf("phhh %lx %lx (%lx %lx)\n", addr, size, mCurrentModule->address() + addr, mCurrentModule->address() + addr + size);
    mCurrentModule->addHeader(addr, size);
    mModulesDirty = true;
}

void Parser::handleExe(const char* data)
{
    auto [ exe, off ] = parseString(data);
    mExe = std::move(exe);
}

void Parser::handleCwd(const char* data)
{
    auto [ wd, off ] = parseString(data);
    mCwd = std::move(wd) + "/";
}

bool Parser::parse(const std::string& line)
{
    printf("parsing '%s'\n", line.c_str());
    // first two bytes is the type of data
    if (line.size() < 2) {
        fprintf(stderr, "invalid line '%s'", line.c_str());
        return false;
    }
    if (line[0] == 'm' && line[1] == 't') {
        // mtrack header version
        const auto [ version, off] = parseNumber(line.data() + 3, 16);
        if (version != MTrackFileVersion) {
            fprintf(stderr, "invalid mtrack file version 0x%x (expected 0x%x)\n", static_cast<int>(version), MTrackFileVersion);
            return false;
        }
    } else if (line[0] == 's' && line[1] == 't') {
        // stack
        handleStack(line.data() + 3);
    } else if (line[0] == 'd' && line[1] == 'l') {
        // new module
        handleModule(line.data() + 3);
    } else if (line[0] == 'p' && line[1] == 'h') {
        // header for module
        handleHeaderLoad(line.data() + 3);
    } else if (line[0] == 'e' && line[1] == 'x') {
        // exe name
        handleExe(line.data() + 3);
    } else if (line[0] == 'w' && line[1] == 'd') {
        // working directory
        handleCwd(line.data() + 3);
    }
    return true;
}
