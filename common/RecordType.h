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
    Start,
    Command,
    Executable,
    Free,
    Library,
    LibraryHeader,
    Malloc,
    Mmap,
    Mremap,
    Munmap,
    PageFault,
    PageRemap,
    PageRemove,
    ThreadName,
    WorkingDirectory,
    Max = WorkingDirectory
};

inline static const char *recordTypeToString(RecordType t)
{
    switch (t) {
    case RecordType::Invalid: break;
    case RecordType::Start: return "Start";
    case RecordType::Command: return "Command";
    case RecordType::Executable: return "Executable";
    case RecordType::Free: return "Free";
    case RecordType::Library: return "Library";
    case RecordType::LibraryHeader: return "LibraryHeader";
    case RecordType::Malloc: return "Malloc";
    case RecordType::Mmap: return "Mmap";
    case RecordType::Mremap: return "Mremap";
    case RecordType::Munmap: return "Munmap";
    case RecordType::PageFault: return "PageFault";
    case RecordType::PageRemap: return "PageRemap";
    case RecordType::PageRemove: return "PageRemove";
    case RecordType::ThreadName: return "ThreadName";
    case RecordType::WorkingDirectory: return "WorkingDirectory";
    }
    return "Invalid";
}
