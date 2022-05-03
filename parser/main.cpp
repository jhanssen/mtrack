#include "Args.h"
#include "Logger.h"
#include "Parser.h"
#include <climits>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PIPE_BUF
#define PIPE_BUF 4096
#endif

#define EINTRWRAP(VAR, BLOCK)                   \
    do {                                        \
        VAR = BLOCK;                            \
    } while (VAR == -1 && errno == EINTR)

static void parse(const std::string& inf, const std::string& outf, const std::string& dumpFile, bool packetMode, size_t maxEvents)
{
    int infd = 0;
    if (!inf.empty()) {
        infd = open(inf.c_str(), O_RDONLY);
    }
    if (infd == -1) {
        LOG("no such file {}\n", inf);
        return;
    }

    FILE* outfile = nullptr;
    if (!dumpFile.empty()) {
        outfile = ::fopen(dumpFile.c_str(), "w");
        if (!outfile) {
            LOG("no out file {} {}\n", errno, dumpFile);
            return;
        }
    }

    Parser parser(outf);
    if (!inf.empty()) {
        struct stat st;
        if (!fstat(infd, &st)) {
            parser.setFileSize(st.st_size, maxEvents);
        }
    }


    size_t totalRead = 0;
    uint32_t packetSize;
    uint8_t packet[PIPE_BUF];
    size_t eventIdx;
    for (eventIdx = 0; eventIdx<maxEvents; ++eventIdx) {
        if (packetMode) {
            packetSize = ::read(infd, packet, PIPE_BUF);
            LOG("got packet {}", packetSize);

            if (!packetSize) {
                break;
            }
            totalRead += packetSize;

            if (outfile) {
                ::fwrite(&packetSize, sizeof(packetSize), 1, outfile);
                ::fwrite(packet, packetSize, 1, outfile);
                continue;
            }
        } else {
            ssize_t r;
            EINTRWRAP(r, ::read(infd, &packetSize, sizeof(packetSize)));
            if (r == 0) {
                break;
            }
            if (r != sizeof(packetSize)) {
                LOG("read size != than {}, {}\n", sizeof(packetSize), r);
                abort();
            }
            if (packetSize > PIPE_BUF) {
                LOG("packet too large {} vs {}\n", packetSize, PIPE_BUF);
                abort();
            }
            totalRead += r;
            EINTRWRAP(r, ::read(infd, packet, packetSize));
            if (r != packetSize) {
                LOG("packet mismatch {} {} @ {}", r, packetSize, totalRead);
                break;
            }
            if (packetSize > 0 && (packet[0] == static_cast<uint8_t>(RecordType::Invalid) || packet[0] > static_cast<uint8_t>(RecordType::Max))) {
                LOG("invalid packet? {}", packet[0]);
                break;
            }
            totalRead += r;
        }
        parser.feed(packet, packetSize);
    }

    LOG("done reading {} events\n", eventIdx);

    if (outfile) {
        int e;
        EINTRWRAP(e, fclose(outfile));
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
        output = "mtrackp.out";
    }
    if (args.has<std::string>("log-file")) {
        Logger::create(args.value<std::string>("log-file"));
    }
    std::string dump;
    if (args.has<std::string>("dump")) {
        dump = args.value<std::string>("dump");
    }

    size_t maxEvents = std::numeric_limits<size_t>::max();
    if (args.has<int64_t>("max-events")) {
        maxEvents = args.value<int64_t>("max-events");
    }

    parse(input, output, dump, packetMode, maxEvents);

    return 0;
}
