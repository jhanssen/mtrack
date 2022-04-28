#include "Args.h"
#include "Parser.h"
#include <sys/mman.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static void parse(const std::string& inf, const std::string& outf)
{
    auto fd = open(inf.c_str(), O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "no such file %s\n", inf.c_str());
        return;
    }

    struct stat stat;
    void* data = MAP_FAILED;

    if (!fstat(fd, &stat)) {
        data = mmap(nullptr, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);;
    }
    close(fd);

    if (data == MAP_FAILED)
        return;

    Parser parser;
    FILE* fi = fopen(outf.c_str(), "w");
    if (!fi) {
        munmap(data, stat.st_size);
        fprintf(stderr, "can't open file for write '%s'\n", outf.c_str());
        return;
    }

    const bool ok = parser.parse(static_cast<uint8_t*>(data), stat.st_size, fi);

    fclose(fi);

    fprintf(stdout, "%zu events. %zu recordings.\n%zu strings %zu hits %zu misses. %zu stacks %zu hits %zu misses.\n",
            parser.eventCount(), parser.recordCount(),
            parser.stringCount(), parser.stringHits(), parser.stringMisses(),
            parser.stackCount(), parser.stackHits(), parser.stackMisses());

    if (!ok)
        return;

    // const std::string json = parser.finalize();

    // size_t off = 0;
    // size_t rem = json.size();
    // while (rem > 0) {
    //     const auto w = fwrite(json.data() + off, 1, std::min<size_t>(rem, 4096), fi);
    //     if (w == 0) {
    //         fprintf(stderr, "unable to write to '%s'\n", outf.c_str());
    //         fclose(fi);
    //         return;
    //     }
    //     off += w;
    //     rem -= w;
    // }

    // fclose(fi);
    fprintf(stdout, "wrote '%s'.\n", outf.c_str());
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
