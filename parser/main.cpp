#include "Args.h"
#include "Logger.h"
#include "Parser.h"
#include <climits>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PIPE_BUF
#define PIPE_BUF 4096
#endif

#define EINTRWRAP(VAR, BLOCK)                   \
    do {                                        \
        VAR = BLOCK;                            \
    } while (VAR == -1 && errno == EINTR)

struct Options : public Parser::Options
{
    std::string input;
    std::string dumpFile;
    bool packetMode {};
};

namespace {
uint64_t parseSize(const char* str, bool* okptr = nullptr)
{
    char* endptr;
    uint64_t arg = strtoull(str, &endptr, 10);
    bool dummy = false;
    bool& ok = okptr ? *okptr : dummy;
    ok = true;
    if (*endptr) {
        ok = false;
        if (*endptr == ' ')
            ++endptr;
        if (!strcasecmp(endptr, "mb") || !strcasecmp(endptr, "m")) {
            arg *= 1024 * 1024;
            ok = true;
        } else if (!strcasecmp(endptr, "gb") || !strcasecmp(endptr, "g")) {
            arg *= 1024 * 1024 * 1024;
            ok = true;
        } else if (!strcasecmp(endptr, "kb") || !strcasecmp(endptr, "k")) {
            arg *= 1024;
            ok = true;
        } else if (!strcasecmp(endptr, "b")) {
            ok = true;
        }
    }
    return arg;
}

uint32_t timestamp()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
    return static_cast<uint32_t>((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}

bool parse(Options &&options)
{
    bool threshold = false;
    int infd = STDIN_FILENO;
    if (!options.input.empty()) {
        infd = open(options.input.c_str(), O_RDONLY);
    }
    if (infd == -1) {
        LOG("no such file {}\n", options.input);
        return true;
    }

    FILE* outfile = nullptr;
    if (!options.dumpFile.empty()) {
        outfile = ::fopen(options.dumpFile.c_str(), "w");
        if (!outfile) {
            LOG("no out file {} {}\n", errno, options.dumpFile);
            return true;
        }
    }

    if (!options.input.empty()) {
        struct stat st;
        if (!fstat(infd, &st)) {
            options.fileSize = st.st_size;
        }
    }

    uint32_t curTimestamp = 0;
    if (options.packetMode) {
        curTimestamp = timestamp();
        options.timeStamp = [curTimestamp]() {
            return timestamp() - curTimestamp;
        };
    } else {
        options.timeStamp = [&curTimestamp]() {
            return curTimestamp;
        };
    }

    if (!options.packetMode && options.timeSkipPerTimeStamp == 0) {
        options.timeSkipPerTimeStamp = 100;
    }
    Parser parser(options);

    // mFileSize = size;
    // mMaxEvents = maxEvents;
    // assert(!mDataOffset);
    // mData.resize(size);


    size_t totalRead = 0;
    uint32_t packetSize;
    uint8_t packet[PIPE_BUF];
    size_t eventIdx;
    for (eventIdx = 0; eventIdx<options.maxEventCount; ++eventIdx) {
        if (options.packetMode) {
            packetSize = ::read(infd, packet, PIPE_BUF);
            LOG("got packet {}", packetSize);

            if (!packetSize) {
                break;
            }
            totalRead += packetSize;

            if (outfile) {
                const uint32_t ts = timestamp() - curTimestamp;
                ::fwrite(&ts, sizeof(ts), 1, outfile);
                ::fwrite(&packetSize, sizeof(packetSize), 1, outfile);
                ::fwrite(packet, packetSize, 1, outfile);
                continue;
            }
        } else {
            ssize_t r;
            EINTRWRAP(r, ::read(infd, &curTimestamp, sizeof(curTimestamp)));
            if (r == 0) {
                break;
            }
            if (r != sizeof(curTimestamp)) {
                LOG("read ts size != than {}, {}\n", sizeof(curTimestamp), r);
                abort();
            }
            EINTRWRAP(r, ::read(infd, &packetSize, sizeof(packetSize)));
            if (r != sizeof(packetSize)) {
                LOG("read ps size != than {}, {}\n", sizeof(packetSize), r);
                abort();
            }
            if (packetSize > PIPE_BUF) {
                LOG("packet too large {} vs {}\n", packetSize, PIPE_BUF);
                abort();
            }
            totalRead += r;
            EINTRWRAP(r, ::read(infd, packet, packetSize));
            if (r != static_cast<ssize_t>(packetSize)) {
                LOG("packet mismatch {} {} @ {}", r, packetSize, totalRead);
                break;
            }
            if (packetSize > 0 && (packet[0] == static_cast<uint8_t>(RecordType::Invalid) || packet[0] > static_cast<uint8_t>(RecordType::Max))) {
                LOG("invalid packet? {}", packet[0]);
                break;
            }
            totalRead += r;
        }
        if (!parser.feed(packet, packetSize)) {
            // threshold reached
            threshold = true;
            break;
        }
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

    return !threshold;
}
} // anonymous namespace

int main(int argc, char** argv)
{
    auto args = args::Parser::parse(argc, argv, [](const char* msg, int offset, char* word) {
        fprintf(stderr, "%s at offset %d word %s\n", msg, offset - 1, word);
    });

    Options options;

    if (args.has<std::string>("input")) {
        options.input = args.value<std::string>("input");
    }

    if (args.has<bool>("packet-mode")) {
        options.packetMode = args.value<bool>("packet-mode");
    }

    if (args.has<std::string>("log-file")) {
        Logger::create(args.value<std::string>("log-file"));
    }

    if (args.has<std::string>("dump")) {
        options.dumpFile = args.value<std::string>("dump");
    }

    if (args.has<int64_t>("max-events")) {
        options.maxEventCount = args.value<int64_t>("max-events");
    }

    if (args.has<int64_t>("threads")) {
        options.resolverThreads = args.value<int64_t>("threads");
    }

    if (args.has<uint32_t>("time-skip")) {
        options.timeSkipPerTimeStamp = args.value<uint32_t>("time-skip");
    }

    if (args.has<bool>("uncompressed")) {
        options.gzip = !args.value<bool>("uncompressed");
    }

    if (args.has<bool>("no-bundle")) {
        options.html = !args.value<bool>("no-bundle");
    }

    if (args.has<std::string>("threshold")) {
        options.threshold = parseSize(args.value<std::string>("threshold").c_str());
    }

    pid_t pid = 0;
    if (args.has<pid_t>("pid")) {
        pid = args.value<pid_t>("pid");
    }

    if (args.has<std::string>("output")) {
        options.output = args.value<std::string>("output");
    } else {
        char buf[128];
        if (options.html) {
            snprintf(buf, sizeof(buf), "mtrackp.%u.html", pid);
        } else if (options.gzip) {
            snprintf(buf, sizeof(buf), "mtrackp.%u.out.gz", pid);
        } else {
            snprintf(buf, sizeof(buf), "mtrackp.%u.out", pid);
        }
        options.output = buf;
    }

    if (!parse(std::move(options))) {
        // threshold reached
        if (pid > 0) {
            kill(pid, SIGABRT);
        }
    }

    return 0;
}
