#include <Preload.h>
#include <sys/mman.h>
#include <cstdio>
#include <cstdint>
// brings in std::size
#include <string>
#include <variant>
#include <string.h>
#include <unistd.h>

#define PAGESIZE 4096

struct Command {
    enum class Type {
        Free,
        Madvise,
        Malloc,
        MemMove,
        Mmap,
        Munmap,
        Realloc,
        Sleep,
        Snapshot,
        Touch
    };

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
            for (size_t n = 0; n < static_cast<size_t>(cmd.arg3); ++n) {
                *(ptr + (n * PAGESIZE) + 1) = 'a';
            }
            break; }
        case Command::Type::MemMove: {
            char* ptr = nullptr;
            if (cmd.arg1 >= 0) {
                // relative to start
                ptr = reinterpret_cast<char*>(cmds[cmd.arg1].ret);
            } else {
                // relative to current
                ptr = reinterpret_cast<char*>(cmds[c + cmd.arg1].ret);
            }
            // move from arg2 to arg3, arg4 bytes
            memmove(ptr + cmd.arg2, ptr + cmd.arg3, cmd.arg4);
            break; }
        case Command::Type::Malloc:
            cmd.ret = reinterpret_cast<uintptr_t>(::malloc(cmd.arg1));
            break;
        case Command::Type::Realloc: {
            uint8_t* ptr = nullptr;
            if (cmd.arg1 >= 0) {
                // relative to start
                ptr = reinterpret_cast<uint8_t*>(cmds[cmd.arg1].ret);
            } else {
                // relative to current
                ptr = reinterpret_cast<uint8_t*>(cmds[c + cmd.arg1].ret);
            }
            cmd.ret = reinterpret_cast<uintptr_t>(::realloc(ptr, cmd.arg2));
            break; }
        case Command::Type::Free: {
            uint8_t* ptr = nullptr;
            if (cmd.arg1 >= 0) {
                // relative to start
                ptr = reinterpret_cast<uint8_t*>(cmds[cmd.arg1].ret);
            } else {
                // relative to current
                ptr = reinterpret_cast<uint8_t*>(cmds[c + cmd.arg1].ret);
            }
            ::free(ptr);
            break; }
        case Command::Type::Sleep:
            ::usleep(cmd.arg1 * 1000);
            break;
        case Command::Type::Snapshot:
            mtrack_snapshot(reinterpret_cast<const char*>(cmd.arg1));
            if (cmd.arg2 > 0) {
                ::usleep(cmd.arg2 * 1000);
            }
            break;
        }
    }
}

Command snapshotCommand(const char* name, intptr_t delay = 100)
{
    return Command { Command::Type::Snapshot, reinterpret_cast<intptr_t>(name), delay };
}

int main(int, char**)
{
    mtrack_disable_snapshots();

    Command cmds[] = {
        { Command::Type::Mmap, 0, 100 * PAGESIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS },
        { Command::Type::Touch, -1, 0, 100 },
        { snapshotCommand("first mmap") },
        { Command::Type::Munmap, -3, 10 * PAGESIZE, 50 * PAGESIZE },
        { snapshotCommand("first munmap") },
        { Command::Type::Madvise, -5, 9 * PAGESIZE, 51 * PAGESIZE, MADV_DONTNEED },
        { snapshotCommand("first madvise out") },
        { Command::Type::Mmap, -7, 100 * PAGESIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED },
        { Command::Type::Touch, -1, 0, 100 },
        { snapshotCommand("second mmap") },
        { Command::Type::Malloc, 1024 * 512 },
        { snapshotCommand("first malloc") },
        { Command::Type::Realloc, -2, 1024 * 256 },
        { snapshotCommand("first realloc") },
        { Command::Type::Free, -2 },
        { snapshotCommand("first free") }
    };

    runCommands(cmds, std::size(cmds));

    return 0;
}
