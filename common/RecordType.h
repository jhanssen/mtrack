#pragma once


enum class CommandType : uint8_t {
    Invalid,
    DisableSnapshots,
    EnableSnapshots,
    Snapshot,
    Max = Snapshot
};

enum class RecordType : uint8_t {
    Invalid,
    Command,
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
    ThreadName,
    WorkingDirectory,
    Max = WorkingDirectory
};

inline static const char *recordTypeToString(RecordType t)
{
    switch (t) {
    case RecordType::Invalid: break;
    case RecordType::Command: return "Command";
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
    case RecordType::WorkingDirectory: return "WorkingDirectory";
    }
    return "Invalid";
}
