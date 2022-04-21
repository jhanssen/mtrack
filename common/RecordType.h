#pragma once

enum class RecordType : uint8_t {
    Invalid,
    Executable,
    Free,
    Library,
    LibraryHeader,
    Madvise,
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
