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
    ThreadName       = 13,
    Time             = 14,
    WorkingDirectory = 15
};

inline static const char *recordTypeToString(RecordType t)
{
    switch (t) {
    case RecordType::Invalid: break;
    case RecordType::Executable: return "Executable";
    case RecordType::Free: return "Free";
    case RecordType::Library: return "Library";
    case RecordType::LibraryHeader: return "LibraryHeader";
    case RecordType::MadviseTracked: return "MadviseTracked";
    case RecordType::MadviseUntracked: return "MadviseUntracked";
    case RecordType::Malloc: return "Malloc";
    case RecordType::MmapTracked: return "MmapTracked";
    case RecordType::MmapUntracked: return "MmapUntracked";
    case RecordType::MunmapTracked: return "MunmapTracked";
    case RecordType::MunmapUntracked: return "MunmapUntracked";
    case RecordType::PageFault: return "PageFault";
    case RecordType::ThreadName: return "ThreadName";
    case RecordType::Time: return "Time";
    case RecordType::WorkingDirectory: return "WorkingDirectory";
    }
    return "Invalid";
}
