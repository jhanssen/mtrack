#pragma once

enum class RecordType : uint8_t {
    Invalid          = 0,
    Executable       = 1,
    Free             = 2,
    Library          = 3,
    LibraryHeader    = 4,
    MadviseTracked   = 5,
    MadviseUntracked = 6,
    Malloc           = 7,
    MmapTracked      = 8,
    MmapUntracked    = 9,
    MunmapTracked    = 10,
    MunmapUntracked  = 11,
    PageFault        = 12,
    Stack            = 13,
    ThreadName       = 14,
    Time             = 15,
    WorkingDirectory = 16
};
