#include <Preload.h>
#include <sys/mman.h>
#include <cstdio>
#include <cstdint>
// brings in std::size
#include <string>
#include <variant>
#include <unistd.h>

#define PAGESIZE 4096

struct Command {
    enum class Type { Mmap, Munmap, Madvise, Touch, Sleep, Snapshot };

    Type type {};
    intptr_t arg1 {};
    intptr_t arg2 {};
    intptr_t arg3 {};
    intptr_t arg4 {};

    uintptr_t ret {};
};

static void runCommands(Command* cmds, size_t numCommands)
{
    for (size_t c = 0; c < numCommands; ++c) {
        Command& cmd = cmds[c];
        switch (cmd.type) {
        case Command::Type::Mmap: {
            uint8_t* ptr = nullptr;
            if (cmd.arg1 >= 0) {
                // relative to start
                ptr = reinterpret_cast<uint8_t*>(cmds[cmd.arg1].ret);
            } else {
                // relative to current
                ptr = reinterpret_cast<uint8_t*>(cmds[c + cmd.arg1].ret);
            }
            cmd.ret = reinterpret_cast<uintptr_t>(::mmap(ptr, cmd.arg2, cmd.arg3, cmd.arg4, -1, 0));
            break; }
        case Command::Type::Munmap: {
            uint8_t* ptr = nullptr;
            if (cmd.arg1 >= 0) {
                // relative to start
                ptr = reinterpret_cast<uint8_t*>(cmds[cmd.arg1].ret);
            } else {
                // relative to current
                ptr = reinterpret_cast<uint8_t*>(cmds[c + cmd.arg1].ret);
            }
            cmd.ret = ::munmap(ptr + cmd.arg2, cmd.arg3);
            break; }
        case Command::Type::Madvise: {
            uint8_t* ptr = nullptr;
            if (cmd.arg1 >= 0) {
                // relative to start
                ptr = reinterpret_cast<uint8_t*>(cmds[cmd.arg1].ret);
            } else {
                // relative to current
                ptr = reinterpret_cast<uint8_t*>(cmds[c + cmd.arg1].ret);
            }
            cmd.ret = ::madvise(ptr + cmd.arg2, cmd.arg3, cmd.arg4);
            break; }
        case Command::Type::Touch: {
            char* ptr = nullptr;
            if (cmd.arg1 >= 0) {
                // relative to start
                ptr = reinterpret_cast<char*>(cmds[cmd.arg1].ret);
            } else {
                // relative to current
                ptr = reinterpret_cast<char*>(cmds[c + cmd.arg1].ret);
            }
            // touch n (arg3) pages from arg1 + arg2
            ptr += cmd.arg2;
            for (size_t n = 0; n < cmd.arg3; ++n) {
                *(ptr + (n * PAGESIZE) + 1) = 'a';
            }
            break; }
        case Command::Type::Sleep:
            ::usleep(cmd.arg1 * 1000);
            break;
        case Command::Type::Snapshot:
            mtrack_snapshot();
            if (cmd.arg1 > 0) {
                ::usleep(cmd.arg1 * 1000);
            }
            break;
        }
    }
}

int main(int, char**)
{
    mtrack_disable_snapshots();

    Command cmds[] = {
        { Command::Type::Mmap, 0, 100 * PAGESIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS },
        { Command::Type::Touch, -1, 0, 100 },
        { Command::Type::Snapshot, 100 },
        { Command::Type::Munmap, -3, 10 * PAGESIZE, 50 * PAGESIZE },
        { Command::Type::Snapshot, 100 },
        { Command::Type::Madvise, -5, 9 * PAGESIZE, 51 * PAGESIZE, MADV_DONTNEED },
        { Command::Type::Snapshot, 100 },
        { Command::Type::Mmap, -7, 100 * PAGESIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED },
        { Command::Type::Touch, -1, 0, 100 },
        { Command::Type::Snapshot, 100 }
    };

    runCommands(cmds, std::size(cmds));

    return 0;
}
