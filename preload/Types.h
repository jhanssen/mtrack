#pragma once

#include <cstdint>

enum class EmitType : uint8_t {
    Stack,
    StackAddress,
    Malloc,
    PageFault,
    ThreadName,
    Time
};

enum class RecordType : uint8_t {
    Invalid          = 0,
    Executable       = 1,
    Free             = 2,
    Library          = 3,
    LibraryHeader    = 4,
    Libraries        = 5,
    MadviseTracked   = 6,
    MadviseUntracked = 7,
    Malloc           = 8,
    MmapTracked      = 9,
    MmapUntracked    = 10,
    MunmapTracked    = 11,
    MunmapUntracked  = 12,
    PageFault        = 13,
    ThreadName       = 14,
    WorkingDirectory = 15
};

namespace Limits {
constexpr uint64_t PageSize = 4096;
}

inline static const char *recordTypeToString(RecordType t)
{
    switch (t) {
    case RecordType::Invalid: break;
    case RecordType::Executable: return "Executable";
    case RecordType::Free: return "Free";
    case RecordType::Library: return "Library";
    case RecordType::LibraryHeader: return "LibraryHeader";
    case RecordType::Libraries: return "Libraries";
    case RecordType::MadviseTracked: return "MadviseTracked";
    case RecordType::MadviseUntracked: return "MadviseUntracked";
    case RecordType::Malloc: return "Malloc";
    case RecordType::MmapTracked: return "MmapTracked";
    case RecordType::MmapUntracked: return "MmapUntracked";
    case RecordType::MunmapTracked: return "MunmapTracked";
    case RecordType::MunmapUntracked: return "MunmapUntracked";
    case RecordType::PageFault: return "PageFault";
    case RecordType::ThreadName: return "ThreadName";
    case RecordType::WorkingDirectory: return "WorkingDirectory";
    }
    return "Invalid";
}
