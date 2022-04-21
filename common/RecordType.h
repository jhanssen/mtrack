#pragma once

enum class RecordType : uint8_t {
    Invalid,
    Executable,
    Free,
    Library,
    LibraryHeader,
    MadviseTracked,
    MadviseUntracked,
    Malloc,
    MmapTracked,
    MmapUntracked,
    MunmapTracked,
    MunmapUntracked,
    PageFault,
    Stack,
    ThreadName,
    WorkingDirectory
};
