#include "Parser.h"
#include <common/Version.h>
#include <cstdlib>

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
        }
    }
    __builtin_unreachable();
}

void Parser::handleModule(const char* data)
{
    const auto [ name, off1 ] = parseString(data);
    const auto [ start, off2 ] = parseNumber(data + off1, 16);
    printf("dlll '%s' %lx\n", name.c_str(), start);
}

void Parser::handleStack(const char* data)
{
    // two hex numbers
    const auto [ ip, off1 ] = parseNumber(data, 16);
    const auto [ sp, off2 ] = parseNumber(data + off1, 16);
    printf("sttt %lx %lx\n", ip, sp);
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
    }
    return true;
}
