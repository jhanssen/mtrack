#include "Args.h"
#include "Parser.h"
#include <climits>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define EINTRWRAP(VAR, BLOCK)                   \
    do {                                        \
        VAR = BLOCK;                            \
    } while (VAR == -1 && errno == EINTR)

static void parse(const std::string& inf, const std::string& outf, bool packetMode)
{
    int infd = 0;
    if (!inf.empty()) {
        infd = open(inf.c_str(), O_RDONLY);
    }
    if (infd == -1) {
        fprintf(stderr, "no such file %s\n", inf.c_str());
        return;
    }

    Parser parser(outf);

    uint32_t packetSize;
    uint8_t packet[PIPE_BUF];
    for (;;) {
        if (packetMode) {
            packetSize = ::read(infd, packet, PIPE_BUF);
        } else {
            ssize_t r;
            EINTRWRAP(r, ::read(infd, &packetSize, sizeof(packetSize)));
            if (r != sizeof(packetSize)) {
                fprintf(stderr, "read size != than %zu, %zu\n", sizeof(packetSize), r);
                abort();
            }
            if (packetSize > PIPE_BUF) {
                fprintf(stderr, "packet too large %zu vs %d\n", r, PIPE_BUF);
                abort();
            }
            uint32_t rem = packetSize;
            while (rem > 0) {
                EINTRWRAP(r, ::read(infd, packet + (packetSize - rem), rem));
                if (r == -1) {
                    fprintf(stderr, "file read error %d %m\n", errno);
                    abort();
                }
                rem -= r;
            }
        }
        parser.parsePacket(packet, packetSize);
    }

    if (infd != 0) {
        int e;
        EINTRWRAP(e, ::close(infd));
    }
    // fprintf(stdout, "%zu events. %zu recordings.\n%zu strings %zu hits %zu misses. %zu stacks %zu hits %zu misses.\n",
    //         parser.eventCount(), parser.recordCount(),
    //         parser.stringCount(), parser.stringHits(), parser.stringMisses(),
    //         parser.stackCount(), parser.stackHits(), parser.stackMisses());
}

int main(int argc, char** argv)
{
    auto args = args::Parser::parse(argc, argv, [](const char* msg, int offset, char* word) {
        fprintf(stderr, "%s at offset %d word %s\n", msg, offset - 1, word);
    });

    std::string input;
    if (args.has<std::string>("input")) {
        input = args.value<std::string>("input");
    }
    bool packetMode = false;
    if (args.has<bool>("packet-mode")) {
        packetMode = args.value<bool>("packet-mode");
    }
    std::string output;
    if (args.has<std::string>("output")) {
        output = args.value<std::string>("output");
    } else {
        output = "mtrack.json";
    }

    parse(input, output, packetMode);

    return 0;
}
