#include "Args.h"
#include "Parser.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static void parse(const std::string& inf, const std::string& outf)
{
    FILE* fi = fopen(inf.c_str(), "r");
    if (!fi)
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
        auto r = fread(data + off, 1, sizeof(data) - off, fi);
        if (r > 0) {
            off += r;
            size_t next = 0, last = 0;
            while ((next = scan(next))) {
                if (!parser.parse(std::string(data + last, data + next - 1))) {
                    fclose(fi);
                    return;
                }
                last = next;
            }
            if (last == 0) {
                // this is bad
                fprintf(stderr, "no newline found\n");
                fclose(fi);
                return;
            }
            // memmove from last to off
            assert(last <= off);
            if (last < off) {
                memmove(data, data + last, off - last);
            }
            off -= last;
        }
        if (feof(fi)) {
            if (off > 0) {
                fprintf(stderr, "data had trailing bytes\n");
            }
            fclose(fi);

            fi = fopen(outf.c_str(), "w");
            if (!fi) {
                fprintf(stderr, "can't open file for write '%s'\n", outf.c_str());
                return;
            }

            const std::string json = parser.finalize();

            off = 0;
            size_t rem = json.size();
            while (rem > 0) {
                const auto w = fwrite(json.data() + off, 1, std::min<size_t>(rem, 4096), fi);
                if (w == 0) {
                    fprintf(stderr, "unable to write to '%s'\n", outf.c_str());
                    fclose(fi);
                    return;
                }
                off += w;
                rem -= w;
            }
            fclose(fi);
            fprintf(stdout, "wrote '%s'\n", outf.c_str());

            return;
        }
    }
}

int main(int argc, char** argv)
{
    auto args = args::Parser::parse(argc, argv, [](const char* msg, int offset, char* word) {
        fprintf(stderr, "%s at offset %d word %s\n", msg, offset - 1, word);
    });

    if (!args.has<std::string>("input")) {
        fprintf(stderr, "missing --input\n");
        return 1;
    }
    std::string output;
    if (args.has<std::string>("output")) {
        output = args.value<std::string>("output");
    } else {
        output = "mtrack.json";
    }

    parse(args.value<std::string>("input"), output);
    return 0;
}
