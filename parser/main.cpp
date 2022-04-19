#include "Args.h"
#include "Parser.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static void parse(const std::string& fn)
{
    FILE* f = fopen(fn.c_str(), "r");
    if (!f)
        return;

    char data[8192];
    size_t off = 0;

    auto scan = [&](size_t from) -> size_t {
        for (size_t i = from; i < off; ++i) {
            if (data[i] == '\n') {
                return i + 1;
            }
        }
        return 0;
    };

    Parser parser;
    for (;;) {
        auto r = fread(data + off, 1, sizeof(data) - off, f);
        if (r > 0) {
            off += r;
            size_t next = 0, last = 0;
            while ((next = scan(next))) {
                if (!parser.parse(std::string(data + last, data + next - 1))) {
                    fclose(f);
                    return;
                }
                last = next;
            }
            if (last == 0) {
                // this is bad
                fprintf(stderr, "no newline found\n");
                fclose(f);
                return;
            }
            // memmove from last to off
            assert(last <= off);
            if (last < off) {
                memmove(data, data + last, off - last);
            }
            off -= last;
        }
        if (feof(f)) {
            if (off > 0) {
                fprintf(stderr, "data had trailing bytes\n");
            }
            fclose(f);
            return;
        }
    }
}

int main(int argc, char** argv)
{
    auto args = args::Parser::parse(argc, argv, [](const char* msg, int offset, char* word) {
        fprintf(stderr, "%s at offset %d word %s\n", msg, offset - 1, word);
    });

    if (!args.has<std::string>("file")) {
        fprintf(stderr, "missing --file\n");
        return 1;
    }

    parse(args.value<std::string>("file"));
    return 0;
}
