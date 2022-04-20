#pragma once

enum class RecordType : uint8_t {
    Invalid,
    Executable,
    Free,
    Library,
    LibraryHeader,
    Madvise,
    Malloc,
    Mmap,
    Munmap,
    PageFault,
    Stack,
    ThreadName,
    WorkingDirectory
};
